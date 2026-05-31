/*
 * hlog - Live log pipeline for hlquery.
 *
 * Copyright (C) 2021-2026, Carlos F. Ferry <carlos.ferry@gmail.com>
 *
 * This file is part of hlog, released under the BSD License version 3.
 * You are free to redistribute and/or modify this software
 * under the terms of the BSD License.
 * For more details, please visit: https://docs.hlquery.com
 */

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cctype>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/modules.h"
#include "core/hlcore.h"
#include "core/logmanager.h"
#include "core/pipeline.h"
#include "core/socketengine.h"


/* Process-wide signal flag used by the Redis source loop. */

volatile std::sig_atomic_t Running = 1;

/* Stop the source loop when the process receives a shutdown signal. */

void HandleSignal(int)
{
     Running = 0;
}

/* Read one module attribute and fall back to a default value when missing. */

std::string GetModuleAttribute(const ModuleConfig& config, const std::string& key, const std::string& fallback = "")
{
     const auto it = config.Attributes.find(key);
     if (it == config.Attributes.end())
     {
          return fallback;
     }

     return it->second;
}

/* Normalize Redis command names and RESP markers for case-insensitive matching. */

std::string ToLowerCopy(std::string value)
{
     std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
     {
          return static_cast<char>(std::tolower(ch));
     });

     return value;
}

/* Parse strictly positive integer attributes such as ports and timeouts. */

int ParsePositiveInt(const std::string& raw, int fallback)
{
     try
     {
          const int value = std::stoi(raw);
          return value > 0 ? value : fallback;
     }
     catch (...)
     {
          return fallback;
     }
}

/* Trim leading and trailing whitespace from module attributes. */

std::string TrimCopy(const std::string& value)
{
     const size_t start = value.find_first_not_of(" \t\r\n");
     if (start == std::string::npos)
     {
          return "";
     }

     const size_t end = value.find_last_not_of(" \t\r\n");
     return value.substr(start, end - start + 1);
}

/* Find the RESP line terminator that closes the current frame header. */

size_t FindRespLineEnd(const std::string& buffer, size_t start)
{
     return buffer.find("\r\n", start);
}

/* RESP frame kinds returned by the Redis server. */

enum class RespType
{
     SimpleString,
     Error,
     Integer,
     BulkString,
     Array,
     Null
};

/* Parsed RESP frame payload used by the subscribe loop. */

struct RespValue
{
     RespType Type = RespType::Null;
     std::string Text;
     long long Integer = 0;
     std::vector<RespValue> Array;
};

/* Parse one RESP value from the buffered Redis socket stream. */

bool ParseRespValue(const std::string& buffer, size_t& cursor, RespValue& out)
{
     const size_t value_start = cursor;
     if (cursor >= buffer.size())
     {
          return false;
     }

     const char prefix = buffer[cursor++];

     if (prefix == '+' || prefix == '-')
     {
          const size_t end = FindRespLineEnd(buffer, cursor);
          if (end == std::string::npos)
          {
               cursor = value_start;
               return false;
          }

          out.Type = prefix == '+' ? RespType::SimpleString : RespType::Error;
          out.Text = buffer.substr(cursor, end - cursor);
          cursor = end + 2;
          return true;
     }

     if (prefix == ':')
     {
          const size_t end = FindRespLineEnd(buffer, cursor);
          if (end == std::string::npos)
          {
               cursor = value_start;
               return false;
          }

          try
          {
               out.Type = RespType::Integer;
               out.Integer = std::stoll(buffer.substr(cursor, end - cursor));
          }
          catch (...)
          {
               throw std::runtime_error("Redis module received an invalid RESP integer.");
          }

          cursor = end + 2;
          return true;
     }

     if (prefix == '$')
     {
          const size_t end = FindRespLineEnd(buffer, cursor);
          if (end == std::string::npos)
          {
               cursor = value_start;
               return false;
          }

          long long length = -1;
          try
          {
               length = std::stoll(buffer.substr(cursor, end - cursor));
          }
          catch (...)
          {
               throw std::runtime_error("Redis module received an invalid RESP bulk length.");
          }

          cursor = end + 2;
          if (length < 0)
          {
               out.Type = RespType::Null;
               out.Text.clear();
               return true;
          }

          if (cursor + static_cast<size_t>(length) + 2 > buffer.size())
          {
               cursor = value_start;
               return false;
          }

          out.Type = RespType::BulkString;
          out.Text = buffer.substr(cursor, static_cast<size_t>(length));
          cursor += static_cast<size_t>(length);
          if (buffer[cursor] != '\r' || buffer[cursor + 1] != '\n')
          {
               throw std::runtime_error("Redis module received a malformed RESP bulk string.");
          }

          cursor += 2;
          return true;
     }

     if (prefix == '*')
     {
          const size_t end = FindRespLineEnd(buffer, cursor);
          if (end == std::string::npos)
          {
               cursor = value_start;
               return false;
          }

          long long count = -1;
          try
          {
               count = std::stoll(buffer.substr(cursor, end - cursor));
          }
          catch (...)
          {
               throw std::runtime_error("Redis module received an invalid RESP array length.");
          }

          cursor = end + 2;
          if (count < 0)
          {
               out.Type = RespType::Null;
               out.Array.clear();
               return true;
          }

          out.Type = RespType::Array;
          out.Array.clear();
          out.Array.reserve(static_cast<size_t>(count));

          for (long long i = 0; i < count; ++i)
          {
               RespValue child;
               if (!ParseRespValue(buffer, cursor, child))
               {
                    cursor = value_start;
                    return false;
               }

               out.Array.push_back(std::move(child));
          }

          return true;
     }

     throw std::runtime_error("Redis module received an unsupported RESP frame type.");
}

/* Convert a RESP scalar into text for command and response checks. */

std::string RespText(const RespValue& value)
{
     if (value.Type == RespType::BulkString || value.Type == RespType::SimpleString || value.Type == RespType::Error)
     {
          return value.Text;
     }

     if (value.Type == RespType::Integer)
     {
          return std::to_string(value.Integer);
     }

     return "";
}

/* Serialize one Redis command in RESP array format. */

std::string BuildRespCommand(const std::vector<std::string>& parts)
{
     std::string payload = "*" + std::to_string(parts.size()) + "\r\n";

     for (const auto& part : parts)
     {
          payload += "$" + std::to_string(part.size()) + "\r\n";
          payload += part;
          payload += "\r\n";
     }

     return payload;
}


/* Redis-backed source module that forwards pub/sub messages into the pipeline. */

class RedisInputModule final : public HLogModule
{
   private:

     /* Open the Redis socket, authenticate, select the database, and subscribe. */

     void ConnectAndSubscribe()
     {
          std::string socket_error;
          if (!Socket.Connect(Host, Port, true, TimeoutSeconds * 1000, socket_error))
          {
               throw std::runtime_error(socket_error.empty() ? "connect failed" : socket_error);
          }

          if (!Socket.SetTimeoutSeconds(TimeoutSeconds, socket_error))
          {
               throw std::runtime_error(socket_error.empty() ? "set timeout failed" : socket_error);
          }

          if (!Password.empty())
          {
               std::vector<std::string> auth_command;
               auth_command.push_back("AUTH");

               if (!Username.empty())
               {
                    auth_command.push_back(Username);
               }

               auth_command.push_back(Password);

               SendCommand(auth_command);

               const RespValue auth_response = ReadOneResponse();
               if (auth_response.Type == RespType::Error)
               {
                    throw std::runtime_error("AUTH failed: " + auth_response.Text + ".");
               }
          }

          if (Database >= 0)
          {
               SendCommand({ "SELECT", std::to_string(Database) });

               const RespValue select_response = ReadOneResponse();
               if (select_response.Type == RespType::Error)
               {
                    throw std::runtime_error("SELECT failed: " + select_response.Text + ".");
               }
          }

          SendCommand({ "SUBSCRIBE", Channel });

          const RespValue subscribe_response = ReadOneResponse();
          if (subscribe_response.Type == RespType::Error)
          {
               throw std::runtime_error("SUBSCRIBE failed: " + subscribe_response.Text + ".");
          }

          if (subscribe_response.Type != RespType::Array || subscribe_response.Array.size() < 3 || ToLowerCopy(RespText(subscribe_response.Array[0])) != "subscribe")
          {
               throw std::runtime_error("Redis module received an unexpected SUBSCRIBE response.");
          }
     }

     /* Read available socket data, append it to the buffer, and decode frames. */

     void ReadLoop(const Pipeline& pipeline)
     {
          while (Running && Socket.IsOpen())
          {
               std::string wait_error;

               const SocketWaitResult wait_result = Socket.WaitForRead(250, wait_error);
               if (wait_result == SocketWaitResult::Timeout)
               {
                    continue;
               }

               if (wait_result == SocketWaitResult::Error)
               {
                    throw std::runtime_error(wait_error.empty() ? "socket wait failed" : wait_error);
               }

               char buffer[4096];

               for (;;)
               {
                    long bytes_read = 0;

                    std::string recv_error;
                    const SocketIoResult receive_result = Socket.Receive(buffer, sizeof(buffer), bytes_read, recv_error);
                    if (receive_result == SocketIoResult::Success)
                    {
                         ReceiveBuffer.append(buffer, static_cast<size_t>(bytes_read));
                         DrainResponses(pipeline);
                         continue;
                    }

                    if (receive_result == SocketIoResult::WouldBlock)
                    {
                         break;
                    }

                    if (receive_result == SocketIoResult::Closed)
                    {
                         throw std::runtime_error("Connection closed by Redis server.");
                    }

                    throw std::runtime_error(recv_error.empty() ? "receive failed" : recv_error);
               }
          }
     }

     /* Parse as many RESP frames as possible from the current receive buffer. */

     void DrainResponses(const Pipeline& pipeline)
     {
          size_t cursor = 0;

          while (cursor < ReceiveBuffer.size())
          {
               RespValue value;
               if (!ParseRespValue(ReceiveBuffer, cursor, value))
               {
                    ReceiveBuffer.erase(0, cursor);
                    return;
               }

               HandleResponse(value, pipeline);
          }

          ReceiveBuffer.clear();
     }

     /* Forward pub/sub payload frames into the pipeline and ignore control frames. */

     void HandleResponse(const RespValue& value, const Pipeline& pipeline)
     {
          if (value.Type == RespType::Error)
          {
               throw std::runtime_error(value.Text + ".");
          }

          if (value.Type != RespType::Array || value.Array.empty())
          {
               return;
          }

          const std::string kind = ToLowerCopy(RespText(value.Array[0]));
          if (kind == "subscribe" || kind == "unsubscribe" || kind == "pong")
          {
               return;
          }

          if (kind == "message" && value.Array.size() >= 3)
          {
               pipeline.ProcessLine(State, RespText(value.Array[2]));
               return;
          }

          if (kind == "pmessage" && value.Array.size() >= 4)
          {
               pipeline.ProcessLine(State, RespText(value.Array[3]));
          }
     }

     /* Send one RESP command and fail fast when the socket write does not complete. */

     void SendCommand(const std::vector<std::string>& parts)
     {
          std::string send_error;
          if (!Socket.SendAll(BuildRespCommand(parts), TimeoutSeconds * 1000, send_error))
          {
               throw std::runtime_error(send_error.empty() ? "send failed" : send_error);
          }
     }

     /* Read exactly one RESP response, blocking until a full frame is available. */

     RespValue ReadOneResponse()
     {
          for (;;)
          {
               size_t cursor = 0;

               RespValue value;
               if (ParseRespValue(ReceiveBuffer, cursor, value))
               {
                    ReceiveBuffer.erase(0, cursor);
                    return value;
               }

               std::string wait_error;
               const SocketWaitResult wait_result = Socket.WaitForRead(TimeoutSeconds * 1000, wait_error);
               if (wait_result == SocketWaitResult::Timeout)
               {
                    throw std::runtime_error("Timed out waiting for Redis response.");
               }

               if (wait_result == SocketWaitResult::Error)
               {
                    throw std::runtime_error(wait_error.empty() ? "socket wait failed" : wait_error);
               }

               char buffer[4096];

               long bytes_read = 0;

               std::string recv_error;
               const SocketIoResult receive_result = Socket.Receive(buffer, sizeof(buffer), bytes_read, recv_error);
               if (receive_result == SocketIoResult::Success)
               {
                    ReceiveBuffer.append(buffer, static_cast<size_t>(bytes_read));
                    continue;
               }

               if (receive_result == SocketIoResult::Closed)
               {
                    throw std::runtime_error("Connection closed by Redis server.");
               }

               if (receive_result == SocketIoResult::WouldBlock)
               {
                    continue;
               }

               throw std::runtime_error(recv_error.empty() ? "receive failed" : recv_error);
          }
     }

     /* Persistent socket and pipeline state reused across reconnects. */

     SocketEngine Socket;
     FileState State;

     /* Connection attributes loaded from the module configuration. */

     std::string Host;
     std::string Channel;
     std::string Password;
     std::string Username;
     std::string Label;
     std::string ReceiveBuffer;

     /* Runtime tuning values and selected Redis database number. */

     int Port = 6379;
     int Database = -1;
     int TimeoutSeconds = 5;
     int ReconnectDelayMs = 5000;

   public:

     /* Construct the module with the fixed runtime name used in config tags. */

     RedisInputModule()
          : HLogModule("redis")
     {

     }

     /* Read configuration attributes and prepare the pipeline file-state label. */

     bool Start(const ModuleConfig& config, std::string& error_message) override
     {
          Host = GetModuleAttribute(config, "host", "127.0.0.1");
          Port = ParsePositiveInt(GetModuleAttribute(config, "port", "6379"), 6379);
          Channel = TrimCopy(GetModuleAttribute(config, "channel", ""));
          Password = GetModuleAttribute(config, "password", GetModuleAttribute(config, "pass", ""));
          Username = GetModuleAttribute(config, "username", "");
          Label = TrimCopy(GetModuleAttribute(config, "label", ""));
          ReconnectDelayMs = ParsePositiveInt(GetModuleAttribute(config, "reconnect_ms", "5000"), 5000);
          TimeoutSeconds = ParsePositiveInt(GetModuleAttribute(config, "timeout", "5"), 5);
          Database = -1;

          const std::string database_value = TrimCopy(GetModuleAttribute(config, "db", ""));
          if (!database_value.empty())
          {
               try
               {
                    Database = std::stoi(database_value);
               }
               catch (...)
               {
                    error_message = "Redis module db must be a non-negative integer.";
                    return false;
               }

               if (Database < 0)
               {
                    error_message = "Redis module db must be a non-negative integer.";
                    return false;
               }
          }

          if (Host.empty())
          {
               error_message = "Redis module requires a non-empty host attribute.";
               return false;
          }

          if (Channel.empty())
          {
               error_message = "Redis module requires a non-empty channel attribute.";
               return false;
          }

          State.PathValue = "redis://" + Host + ":" + std::to_string(Port) + "/" + Channel;
          State.Label = Label.empty() ? ("redis:" + Channel) : Label;

          if (Instance && Instance->Logs)
          {
               Instance->Logs->Normal("modules", "Redis module configured host=" + Host + " port=" + std::to_string(Port) + " channel=" + Channel + ".");
          }

          return true;
     }

     /* Mark this module as a source that drives the pipeline loop itself. */

     bool IsSourceModule() const override
     {
          return true;
     }

     /* Connect, subscribe, and keep retrying until the process stops. */

     bool Run(const Pipeline& pipeline, WatchMode, int, std::string&) override
     {
          Running = 1;
          std::signal(SIGINT, HandleSignal);
#ifdef SIGTERM
          std::signal(SIGTERM, HandleSignal);
#endif

          while (Running)
          {
               try
               {
                    ConnectAndSubscribe();
                    ReadLoop(pipeline);
               }
               catch (const std::exception& ex)
               {
                    if (Instance && Instance->Logs)
                    {
                         Instance->Logs->Critical("modules", "Redis module error: " + std::string(ex.what()) + ".");
                    }
               }

               Socket.Close();
               ReceiveBuffer.clear();

               if (!Running)
               {
                    return true;
               }

               if (Instance && Instance->Logs)
               {
                    Instance->Logs->Normal("modules", "Redis module disconnected; retrying.");
               }

               std::this_thread::sleep_for(std::chrono::milliseconds(ReconnectDelayMs));
          }

          return true;
     }

     /* Stop the loop and force any blocking socket operations to unwind. */

     void Stop() override
     {
          Running = 0;
          Socket.Close();
     }
};

MODULE_LOAD(RedisInputModule)
