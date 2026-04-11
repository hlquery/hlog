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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <unordered_set>

#include "core/modules.h"
#include "core/hlcore.h"
#include "core/logmanager.h"
#include "core/socketengine.h"
#include "utils/tools.h"

namespace
{

/* Parsed pieces of the target hlquery HTTP endpoint. */

struct ParsedHttpEndpoint
{
     std::string Host;
     int Port = 80;
     std::string BasePath;
};

/* Read one string attribute from the IRC module config with fallback. */

std::string GetModuleAttribute(const ModuleConfig& config, const std::string& key, const std::string& fallback = "")
{
     const auto it = config.Attributes.find(key);
     if (it == config.Attributes.end())
     {
          return fallback;
     }

     return it->second;
}

/* Sanitize outbound IRC lines so control bytes do not leak to the channel. */

std::string SanitizeIRCLine(std::string line)
{
     line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
     line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

     for (char& ch : line)
     {
          if (static_cast<unsigned char>(ch) < 0x20 && ch != '\t')
          {
               ch = ' ';
          }
     }

     if (line.size() > 380)
     {
          line.resize(377);
          line += "...";
     }

     return line;
}

/* Keep log-file copies readable without truncating long raw server lines. */

std::string SanitizeIRCLogLine(std::string line)
{
     line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
     line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

     for (char& ch : line)
     {
          if (static_cast<unsigned char>(ch) < 0x20 && ch != '\t')
          {
               ch = ' ';
          }
     }

     if (line.size() > 380)
     {
          return line;
     }

     return line;
}

/* Normalize attribute comparisons and protocol matches to lower-case. */

std::string ToLowerCopy(std::string value)
{
     std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
     });

     return value;
}

/* Parse boolean-like config values used by posting and logging toggles. */

bool ParseBoolAttribute(const ModuleConfig& config, const std::string& key, bool fallback)
{
     const auto it = config.Attributes.find(key);
     if (it == config.Attributes.end())
     {
          return fallback;
     }

     const std::string value = ToLowerCopy(it->second);
     if (value == "true" || value == "yes" || value == "1" || value == "on")
     {
          return true;
     }

     if (value == "false" || value == "no" || value == "0" || value == "off")
     {
          return false;
     }

     return fallback;
}

/* Extract the nick portion from an IRC prefix like nick!user@host. */

std::string ExtractNickFromPrefix(const std::string& prefix)
{
     const size_t bangPos = prefix.find('!');
     return bangPos == std::string::npos ? prefix : prefix.substr(0, bangPos);
}

/* Parse human-readable log size values such as 64MB. */

size_t ParseSizeValue(const std::string& raw, size_t fallback)
{
     if (raw.empty())
     {
          return fallback;
     }

     std::string value = ToLowerCopy(raw);
     size_t multiplier = 1;

     if (value.size() >= 2 && value.substr(value.size() - 2) == "kb")
     {
          multiplier = 1024;
          value.resize(value.size() - 2);
     }
     else if (value.size() >= 2 && value.substr(value.size() - 2) == "mb")
     {
          multiplier = 1024 * 1024;
          value.resize(value.size() - 2);
     }
     else if (value.size() >= 2 && value.substr(value.size() - 2) == "gb")
     {
          multiplier = 1024ULL * 1024ULL * 1024ULL;
          value.resize(value.size() - 2);
     }

     try
     {
          return static_cast<size_t>(std::stoull(value) * multiplier);
     }
     catch (...)
     {
          return fallback;
     }
}

/* Encode collection and document paths for hlquery HTTP requests. */

std::string UrlEncode(const std::string& value)
{
     std::ostringstream escaped;
     escaped.fill('0');
     escaped << std::hex;

     for (unsigned char c : value)
     {
          if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
          {
               escaped << c;
          }
          else
          {
               escaped << '%' << std::setw(2) << static_cast<int>(c);
          }
     }

     return escaped.str();
}

/* Expand {strftime} blocks used in collection naming templates. */

std::string ResolveCollectionTemplate(const std::string& value)
{
     if (value.find('{') == std::string::npos)
     {
          return value;
     }

     std::string resolved;
     resolved.reserve(value.size() + 16);

     for (size_t i = 0; i < value.size(); ++i)
     {
          if (value[i] != '{')
          {
               resolved.push_back(value[i]);
               continue;
          }

          const size_t close = value.find('}', i + 1);
          if (close == std::string::npos)
          {
               resolved.push_back(value[i]);
               continue;
          }

          const std::string format = value.substr(i + 1, close - i - 1);
          if (!format.empty())
          {
               resolved += Tools::GetTimestamp(format);
               i = close;
               continue;
          }

          resolved.push_back(value[i]);
     }

     return resolved;
}

/* Restrict generated document ids to hlquery-safe characters. */

std::string SanitizeIdPart(std::string value)
{
     for (char& ch : value)
     {
          const unsigned char uch = static_cast<unsigned char>(ch);
          if (std::isalnum(uch) || ch == '-' || ch == '_' || ch == '.')
          {
               continue;
          }

          ch = '_';
     }

     return value;
}

/* Parse a simple http://host[:port][/base] endpoint definition. */

ParsedHttpEndpoint ParseHttpEndpoint(const std::string& endpoint)
{
     if (endpoint.empty())
     {
          return ParsedHttpEndpoint{};
     }

     if (endpoint.rfind("http://", 0) != 0)
     {
          throw std::runtime_error("IRC module only supports http:// endpoints");
     }

     std::string raw = endpoint.substr(7);
     ParsedHttpEndpoint parsed;

     const size_t slashPos = raw.find('/');
     const std::string hostPort = slashPos == std::string::npos ? raw : raw.substr(0, slashPos);
     parsed.BasePath = slashPos == std::string::npos ? "" : raw.substr(slashPos);

     const size_t colonPos = hostPort.rfind(':');
     if (colonPos != std::string::npos)
     {
          parsed.Host = hostPort.substr(0, colonPos);
          parsed.Port = std::stoi(hostPort.substr(colonPos + 1));
     }
     else
     {
          parsed.Host = hostPort;
     }

     if (parsed.Host.empty())
     {
          throw std::runtime_error("IRC module endpoint host is empty");
     }

     return parsed;
}

/* Parse interval strings such as 10s, 1m, or 500ms into milliseconds. */

int ParseIntervalMilliseconds(const std::string& raw, int fallback)
{
     if (raw.empty())
     {
          return fallback;
     }

     std::string value = ToLowerCopy(raw);
     size_t multiplier = 1000;

     if (value.size() >= 2 && value.substr(value.size() - 2) == "ms")
     {
          multiplier = 1;
          value.resize(value.size() - 2);
     }
     else if (!value.empty() && value.back() == 's')
     {
          value.pop_back();
     }
     else if (!value.empty() && value.back() == 'm')
     {
          multiplier = 60 * 1000;
          value.pop_back();
     }
     else if (!value.empty() && value.back() == 'h')
     {
          multiplier = 60 * 60 * 1000;
          value.pop_back();
     }

     try
     {
          const long long parsed = std::stoll(value);
          if (parsed < 0)
          {
               return fallback;
          }

          const long long millis = parsed * static_cast<long long>(multiplier);
          if (millis > std::numeric_limits<int>::max())
          {
               return std::numeric_limits<int>::max();
          }

          return static_cast<int>(millis);
     }
     catch (...)
     {
          return fallback;
     }
}

}

/* IRC bridge module that forwards pipeline events and posts IRC activity. */

class IRCModule final : public HLogModule
{
   public:

     /* Construct the IRC bridge module. */

     IRCModule()
         : HLogModule("irc")
     {
     }

     /* Stop background workers before module destruction. */

     ~IRCModule() override
     {
          Stop();
     }

     /* Parse config, initialize local logging, and start worker threads. */

     bool Start(const ModuleConfig& config, std::string& error) override
     {
          /* Load connection, posting, and local-log settings in one place. */

          Server = GetModuleAttribute(config, "server", "irc.libera.chat");
          Channel = GetModuleAttribute(config, "channel", "#ubuntu");
          Nick = GetModuleAttribute(config, "nick", "blabla");
          User = GetModuleAttribute(config, "user", Nick);
          RealName = GetModuleAttribute(config, "realname", "hlog irc bridge");
          Password = GetModuleAttribute(config, "pass", "");
          QueueLimit = ParsePositiveInt(GetModuleAttribute(config, "queue_limit", "1000"), 1000);
          ReconnectDelayMs = ParsePositiveInt(GetModuleAttribute(config, "reconnect_ms", "5000"), 5000);
          Port = ParsePositiveInt(GetModuleAttribute(config, "port", "6667"), 6667);
          StartupDropCount = ParsePositiveInt(GetModuleAttribute(config, "startup_drop_count", "250"), 250);
          PostEndpoint = GetModuleAttribute(config, "endpoint", "http://127.0.0.1:9200");
          PostCollection = GetModuleAttribute(config, "collection", "");
          PostAuthMethod = GetModuleAttribute(config, "auth_method", "bearer");
          PostAuthToken = GetModuleAttribute(config, "auth_token", "");
          PostTimeoutSeconds = ParsePositiveInt(GetModuleAttribute(config, "timeout", "5"), 5);
          PostIntervalMs = ParseIntervalMilliseconds(GetModuleAttribute(config, "post_interval", "0"), 0);
          PostPrivmsgs = ParseBoolAttribute(config, "post_privmsgs", true);
          PostChannels = ParseBoolAttribute(config, "post_channels", true);
          PostConnect = ParseBoolAttribute(config, "post_connect", true);
          LogAllIrcEvents = ParseBoolAttribute(config, "log_all_irc_events", false);
          LocalLoggingEnabled = ParseBoolAttribute(config, "logging", true);

          LogConfig fileConfig;
          fileConfig.method = "file";
          fileConfig.type = "irc";
          fileConfig.level = LogLevel::LOG_NORMAL;
          fileConfig.target = "irc.log";
          fileConfig.max_size = ParseSizeValue("64MB", 64 * 1024 * 1024);
          fileConfig.rotation_interval = -1;
          fileConfig.max_rotated_files = 3;
          fileConfig.max_age_days = static_cast<size_t>(ParsePositiveInt(GetModuleAttribute(config, "log_retention_days", "3"), 3));

          std::filesystem::path targetPath(fileConfig.target);
          if (!targetPath.is_absolute())
          {
               targetPath = std::filesystem::path("run/logs") / targetPath;
               fileConfig.target = targetPath.lexically_normal().string();
          }

          LogType = fileConfig.type;
          if (LocalLoggingEnabled && fileConfig.method == "file")
          {
               FileLog = std::make_unique<LogStream>(fileConfig);
          }

          if (Server.empty())
          {
               error = "IRC module requires a non-empty server attribute.";
               return false;
          }

          if (Channel.empty() || Channel[0] != '#')
          {
               error = "IRC module requires a channel attribute beginning with #.";
               return false;
          }

          Running.store(true);
          Registered = false;
          JoinRequested = false;
          WelcomeSeen = false;

          /* The post worker is optional and only runs when a destination exists. */

          PostingActive = !PostCollection.empty() && (PostPrivmsgs || PostChannels || PostConnect);
          PostingThreadRunning.store(PostingActive);
          Worker = std::thread(&IRCModule::RunWorker, this);
          if (PostingActive)
          {
               PostWorker = std::thread(&IRCModule::RunPostQueue, this);
          }

          Log("normal",
              "IRC module configured server=" + Server +
              " port=" + std::to_string(Port) +
              " channel=" + Channel +
              " nick=" + Nick + ".");

          return true;
     }

     /* Stop worker threads and close the active IRC socket. */

     void Stop() override
     {
          bool expected = true;
          if (!Running.compare_exchange_strong(expected, false))
          {
               return;
          }

          {
               std::lock_guard<std::mutex> lock(Mutex);
               Stopping = true;
          }
          PostingThreadRunning.store(false);

          Condition.notify_all();
          PostCondition.notify_all();

          if (Worker.joinable())
          {
               Worker.join();
          }

          if (PostWorker.joinable())
          {
               PostWorker.join();
          }

          CloseSocket();
     }

     /* Forward one processed pipeline event into the IRC send queue. */

     void ProcessEvent(PipelineEvent& event, const FileState& state) override
     {
          if (!Running.load() || event.Dropped)
          {
               return;
          }

          std::string line = event.RawLine.empty() ? event.Document.dump() : event.RawLine;
          line = "[" + state.Label + "] " + SanitizeIRCLine(std::move(line));

          if (line.empty())
          {
               return;
          }

          {
               std::lock_guard<std::mutex> lock(Mutex);
               if (!StartupDrainComplete)
               {
                    ++DroppedOnStartup;
                    if (DroppedOnStartup <= static_cast<size_t>(StartupDropCount))
                    {
                         return;
                    }

                    StartupDrainComplete = true;
                    Queue.push_back("[hlog] dropped " + std::to_string(DroppedOnStartup - 1) +
                                    " startup lines before IRC forwarding began");
               }

               if (Queue.size() >= static_cast<size_t>(QueueLimit))
               {
                    Queue.pop_front();
               }

               Queue.push_back(std::move(line));
          }

          TraceIrc("queue_event queued=" + std::to_string(Queue.size()) + ".");

          Condition.notify_one();
     }

   private:

     /* Parse a positive integer config field with fallback. */

     static int ParsePositiveInt(const std::string& raw, int fallback)
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

     /* Own the lifetime of the reconnecting IRC session worker. */

     void RunWorker()
     {
          /* Keep one reconnecting IRC session alive for the lifetime of the module. */

          while (Running.load())
          {
               if (!EnsureConnected())
               {
                    WaitBeforeReconnect();
                    continue;
               }

               DriveConnection();
          }
     }

     /* Pause between reconnect attempts. */

     void WaitBeforeReconnect()
     {
          TraceIrc("wait_reconnect reconnect_ms=" + std::to_string(ReconnectDelayMs) + ".");

          /* Sleep between reconnect attempts but wake early when shutting down. */

          std::unique_lock<std::mutex> lock(Mutex);
          Condition.wait_for(lock, std::chrono::milliseconds(ReconnectDelayMs), [this]() {
               return !Running.load() || !Queue.empty();
          });
     }

     /* Open the IRC socket and complete registration if disconnected. */

     bool EnsureConnected()
     {
          if (IrcSocket.IsOpen())
          {
               return true;
          }

          if (!Running.load())
          {
               return false;
          }

          /* Open the socket first, then finish IRC registration over the wire. */

          std::string connectError;
          if (!IrcSocket.Connect(Server, Port, true, 5000, connectError))
          {
               Log("critical", "IRC module failed to connect to " + Server + ":" + std::to_string(Port) +
                    (connectError.empty() ? "." : ": " + connectError));
               return false;
          }

          TraceIrc("connect_ok host=" + Server + " port=" + std::to_string(Port) + ".");

          JoinedChannel = false;
          Registered = false;
          JoinRequested = false;
          WelcomeSeen = false;
          ReceiveBuffer.clear();

          if (!Password.empty())
          {
               SendRaw("PASS " + Password);
          }
          SendRaw("NICK " + Nick);
          SendRaw("USER " + User + " 0 * :" + RealName);

          LogConnect("IRC module socket connected to " + Server + ":" + std::to_string(Port) + " nick=" + Nick + ".");
          return true;
     }

     /* Alternate between reading server traffic and draining queued sends. */

     void DriveConnection()
     {
          /* Interleave incoming reads with queued outbound PRIVMSG flushing. */

          while (Running.load() && IrcSocket.IsOpen())
          {
               std::string waitError;
               const SocketWaitResult waitResult = IrcSocket.WaitForRead(250, waitError);
               if (waitResult == SocketWaitResult::Error)
               {
                    Log("critical", "IRC module socket wait failed" + std::string(waitError.empty() ? "." : ": " + waitError));
                    ResetConnection();
                    return;
               }

               if (waitResult == SocketWaitResult::Ready)
               {
                    TraceIrc("socket_read_ready.");

                    if (!ReadIncoming())
                    {
                         ResetConnection();
                         return;
                    }
               }

               FlushQueue();
          }
     }

     /* Read all immediately available IRC socket data. */

     bool ReadIncoming()
     {
          /* Drain every available recv() chunk before yielding back to poll/select. */

          char buffer[4096];

          for (;;)
          {
               long bytes = 0;
               std::string recvError;
               const SocketIoResult recvResult = IrcSocket.Receive(buffer, sizeof(buffer), bytes, recvError);
               if (recvResult == SocketIoResult::Success)
               {
                    TraceIrc("recv_ok bytes=" + std::to_string(bytes) + ".");

                    ReceiveBuffer.append(buffer, static_cast<size_t>(bytes));
                    ProcessServerLines();
                    continue;
               }

               if (recvResult == SocketIoResult::Closed)
               {
                    return false;
               }

               if (recvResult == SocketIoResult::WouldBlock)
               {
                    return true;
               }

               if (!recvError.empty())
               {
                    Log("critical", "IRC module receive failed: " + recvError);
               }

               return false;
          }
     }

     /* Split the receive buffer into complete IRC protocol lines. */

     void ProcessServerLines()
     {
          /* IRC frames are CRLF-delimited and may arrive in partial chunks. */

          size_t pos = 0;
          while ((pos = ReceiveBuffer.find("\r\n")) != std::string::npos)
          {
               std::string line = ReceiveBuffer.substr(0, pos);
               ReceiveBuffer.erase(0, pos + 2);

               WriteIRCEvent("irc_recv", line);

               if (line.rfind("PING :", 0) == 0)
               {
                    SendRaw("PONG :" + line.substr(6));
                    continue;
               }

               HandleServerMessage(line);
          }
     }

     /* Interpret server messages that affect state, logging, or posting. */

     void HandleServerMessage(const std::string& line)
     {
          /* Decode only the commands that affect channel state or activity posting. */

          if (line.empty() || line[0] != ':')
          {
               return;
          }

          const size_t firstSpace = line.find(' ');
          if (firstSpace == std::string::npos)
          {
               return;
          }

          const std::string prefix = line.substr(1, firstSpace - 1);
          size_t cursor = firstSpace + 1;
          const size_t secondSpace = line.find(' ', cursor);
          if (secondSpace == std::string::npos)
          {
               return;
          }

          const std::string command = line.substr(cursor, secondSpace - cursor);
          cursor = secondSpace + 1;

          if (command == "JOIN")
          {
               const std::string joiningNick = ExtractNickFromPrefix(prefix);
               const std::string channel = line.substr(cursor);
               const std::string normalizedChannel = !channel.empty() && channel[0] == ':' ? channel.substr(1) : channel;
               if (normalizedChannel == Channel)
               {
                    WriteChannelActivity(joiningNick + " joined " + normalizedChannel + ".");
               }

               const std::string loweredJoiningNick = ToLowerCopy(joiningNick);
               if (loweredJoiningNick == ToLowerCopy(Nick))
               {
                    JoinedChannel = true;
                    LogConnect("IRC module joined " + Channel + ".");
               }
              return;
          }

          /* Keep only the target token and trailing payload for later checks. */

          const size_t trailingPos = line.find(" :", cursor);
          const std::string target =
               trailingPos == std::string::npos ? line.substr(cursor) : line.substr(cursor, trailingPos - cursor);
          const std::string message =
               trailingPos == std::string::npos ? std::string() : line.substr(trailingPos + 2);

          if (command == "001")
          {
               Registered = true;
               WelcomeSeen = true;
               WriteServerActivity(message.empty() ? "welcome." : message);
               RequestJoin();
               return;
          }

          if (command == "376" || command == "422")
          {
               Registered = true;
               if (!message.empty())
               {
                    WriteServerActivity(message);
               }
               RequestJoin();
               return;
          }

          if (command == "372" || command == "375")
          {
               if (!message.empty())
               {
                    WriteServerActivity(message);
               }
               return;
          }

          if (command == "NOTICE" && target == "*")
          {
               if (!message.empty())
               {
                    WriteServerActivity(message);
               }
               return;
          }

          if (command == "433")
          {
               HandleNicknameInUse();
               return;
          }

          if (command == "PART" && target == Channel)
          {
               WriteChannelActivity(ExtractNickFromPrefix(prefix) + " left " + Channel + ".");
               return;
          }

          if (command == "KICK" && target == Channel)
          {
               const size_t kickedPos = message.find(' ');
               const std::string kickedNick = kickedPos == std::string::npos ? message : message.substr(0, kickedPos);
               WriteChannelActivity(ExtractNickFromPrefix(prefix) + " kicked " + kickedNick + " from " + Channel + ".");
               return;
          }

          if (command == "TOPIC" && target == Channel)
          {
               WriteChannelActivity(ExtractNickFromPrefix(prefix) + " changed topic in " + Channel + ": " + SanitizeIRCLine(message));
               return;
          }

          if (command == "NOTICE" && target == Channel)
          {
               WriteChannelActivity("notice from " + ExtractNickFromPrefix(prefix) + " in " + Channel + ": " + SanitizeIRCLine(message));
               return;
          }

          if (command != "PRIVMSG")
          {
               return;
          }

          if (target == Channel)
          {
               WritePrivmsgActivity(ExtractNickFromPrefix(prefix) + ": " + SanitizeIRCLine(message));
          }

          const std::string loweredMessage = ToLowerCopy(message);
          const std::string loweredNick = ToLowerCopy(Nick);

          if (loweredMessage.find(loweredNick) == std::string::npos)
          {
               return;
          }

          Log("normal",
              "IRC mention from " + ExtractNickFromPrefix(prefix) +
              " target=" + target +
              " message=" + SanitizeIRCLine(message));
     }

     /* Flush queued outbound bridge lines into the joined IRC channel. */

     void FlushQueue()
     {
          /* Send one queued pipeline line at a time once the channel join completes. */

          for (;;)
          {
               std::string line;
               {
                    std::unique_lock<std::mutex> lock(Mutex);
                    if (Queue.empty())
                    {
                         Condition.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                              return !Running.load() || !Queue.empty();
                         });
                    }

                    if (Queue.empty())
                    {
                         return;
                    }

                    if (!Running.load())
                    {
                         return;
                    }

                    line = std::move(Queue.front());
                    Queue.pop_front();
               }

               TraceIrc("flush_queue remaining=" + std::to_string(Queue.size()) + ".");

               if (!JoinedChannel)
               {
                    RequestJoin();
                    {
                         std::lock_guard<std::mutex> lock(Mutex);
                         Queue.push_front(std::move(line));
                    }
                    return;
               }

               if (!SendRaw("PRIVMSG " + Channel + " :" + line))
               {
                    {
                         std::lock_guard<std::mutex> lock(Mutex);
                         Queue.push_front(std::move(line));
                    }

                    ResetConnection();
                    return;
               }
          }
     }

     /* Send one raw IRC line over the live socket. */

     bool SendRaw(const std::string& line)
     {
          if (!IrcSocket.IsOpen() || !Running.load())
          {
               return false;
          }

          WriteIRCEvent("irc_send", line);

          std::string sendError;
          const bool sent = IrcSocket.SendAll(line + "\r\n", 250, sendError);
          if (!sent && !sendError.empty())
          {
               Log("critical", "IRC module send failed: " + sendError);
          }
          else if (sent)
          {
               TraceIrc("send_ok bytes=" + std::to_string(line.size()) + ".");
          }

          return sent;
     }

     /* Reset socket and registration state after any connection failure. */

     void ResetConnection()
     {
          /* Drop protocol state so the next connect starts from a clean session. */

          IrcSocket.Close();

          JoinedChannel = false;
          Registered = false;
          JoinRequested = false;
          WelcomeSeen = false;
          ReceiveBuffer.clear();
          LogConnect("IRC module disconnected; retrying.");
     }

     /* Join the configured channel once registration completes. */

     void RequestJoin()
     {
          if (!Registered || JoinRequested || JoinedChannel || !IrcSocket.IsOpen())
          {
               return;
          }

          JoinRequested = SendRaw("JOIN " + Channel);
     }

     /* Pick a fallback nick when the server rejects the preferred one. */

     void HandleNicknameInUse()
     {
          if (Nick.size() >= 30)
          {
               Nick = Nick.substr(0, 29);
          }

          Nick.push_back('_');
          SendRaw("NICK " + Nick);
          LogConnect("IRC nick in use; retrying as " + Nick + ".");
     }

     /* Close the current IRC socket if one is open. */

     void CloseSocket()
     {
          IrcSocket.Close();
     }

     /* Write module status logs to the host logger and local file log. */

     void Log(const std::string& level, const std::string& message)
     {
          if (!Instance || !Instance->Logs)
          {
               WriteLogFile(level, message);
               return;
          }

          if (level == "critical")
          {
               Instance->Logs->Critical("modules", message);
               WriteLogFile(level, message);
               return;
          }

          Instance->Logs->Normal("modules", message);
          WriteLogFile(level, message);
     }

     /* Write one connection-state message through the module logger. */

     void LogConnect(const std::string& message)
     {
          Log("normal", message);
     }

     /* Write one message into the local IRC module log file. */

     void WriteLogFile(const std::string& level, const std::string& message)
     {
          if (!FileLog)
          {
               return;
          }

          std::lock_guard<std::mutex> lock(FileMutex);
          const LogLevel logLevel = level == "critical" ? LogLevel::LOG_CRITICAL : LogLevel::LOG_NORMAL;
          FileLog->WriteLog(logLevel, LogType, message);
     }

     /* Mirror raw IRC traffic to runtime logs and the local log file. */

     void WriteIRCEvent(const std::string& type, const std::string& message)
     {
          const std::string sanitized = SanitizeIRCLogLine(message);

          if (Instance && Instance->Logs)
          {
               Instance->Logs->Normal(type, sanitized);
          }

          if (!FileLog)
          {
               return;
          }

          std::lock_guard<std::mutex> lock(FileMutex);
          FileLog->WriteLog(LogLevel::LOG_NORMAL, type, sanitized);
     }

     /* Record one channel-activity event and queue it for posting. */

     void WriteChannelActivity(const std::string& message)
     {
          WriteIRCEvent("irc_channel", message);
          EnqueueActivity("channel", message);
     }

     /* Record one PRIVMSG activity event and queue it for posting. */

     void WritePrivmsgActivity(const std::string& message)
     {
          WriteIRCEvent("irc_channel", message);
          EnqueueActivity("privmsg", message);
     }

     /* Record one connect-state event and queue it for posting. */

     void WriteServerActivity(const std::string& message)
     {
          WriteIRCEvent("irc_server", SanitizeIRCLine(message));
          EnqueueActivity("connect", SanitizeIRCLine(message));
     }

     /* Queue one IRC activity item for the batch posting worker. */

     void EnqueueActivity(const std::string& kind, const std::string& message)
     {
          /* Feed only the enabled activity kinds into the batch posting queue. */

          if (!PostingActive)
          {
               return;
          }

          if (kind == "privmsg" && !PostPrivmsgs)
          {
               return;
          }

          if (kind == "channel" && !PostChannels)
          {
               return;
          }

          if (kind == "connect" && !PostConnect)
          {
               return;
          }

          {
               std::lock_guard<std::mutex> lock(PostMutex);
               if (PostQueue.size() >= static_cast<size_t>(QueueLimit))
               {
                    PostQueue.pop_front();
               }

               PostQueue.push_back(ActivityEvent{kind, message, Tools::GetTimestamp("%Y-%m-%dT%H:%M:%S")});
               TraceIrc("enqueue_activity kind=" + kind + " queued=" + std::to_string(PostQueue.size()) + ".");
          }
          PostCondition.notify_one();
     }

     /* Collect queued activity into batch documents for hlquery posting. */

     void RunPostQueue()
     {
          /* Batch IRC activity into one hlquery document per configured window. */

          for (;;)
          {
               std::vector<ActivityEvent> batch;
               {
                    std::unique_lock<std::mutex> lock(PostMutex);
                    if (PostQueue.empty())
                    {
                         PostCondition.wait_for(lock, std::chrono::milliseconds(250), [this]() {
                              return !PostingThreadRunning.load() || !PostQueue.empty();
                         });
                    }

                    if (PostQueue.empty())
                    {
                         if (!PostingThreadRunning.load())
                         {
                              return;
                         }

                         continue;
                    }

                    if (PostIntervalMs <= 0)
                    {
                         batch.push_back(std::move(PostQueue.front()));
                         PostQueue.pop_front();
                    }
                    else
                    {
                         const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(PostIntervalMs);
                         while (PostingThreadRunning.load() && std::chrono::steady_clock::now() < deadline)
                         {
                              PostCondition.wait_until(lock, deadline, [this]() {
                                   return !PostingThreadRunning.load();
                              });
                         }

                         while (!PostQueue.empty())
                         {
                              batch.push_back(std::move(PostQueue.front()));
                              PostQueue.pop_front();
                         }
                    }
               }

               if (batch.empty())
               {
                    continue;
               }

               try
               {
                    TraceIrc("post_batch size=" + std::to_string(batch.size()) + ".");

                    const std::string batchFrom = batch.front().CreatedAt;
                    const std::string batchTo = batch.back().CreatedAt;
                    const std::string batchId = SanitizeIdPart(
                         "irc:" + Channel + ":" + batchFrom + ":" + batchTo);

                    /* Keep both structured events and a readable joined text copy. */

                    json document = json::object();
                    document["id"] = batchId;
                    document["source"] = "irc";
                    document["channel"] = Channel;
                    document["server"] = Server;
                    document["nick"] = Nick;
                    document["created_at"] = Tools::GetTimestamp("%Y-%m-%dT%H:%M:%S");
                    document["batch_from"] = batchFrom;
                    document["batch_to"] = batchTo;
                    document["event_count"] = batch.size();
                    document["post_interval_ms"] = PostIntervalMs;

                    json events = json::array();
                    std::unordered_set<std::string> kinds;
                    std::string messagesText;

                    for (size_t i = 0; i < batch.size(); ++i)
                    {
                         const auto& event = batch[i];
                         json item = json::object();
                         item["kind"] = event.Kind;
                         item["message"] = event.Message;
                         item["created_at"] = event.CreatedAt;
                         events.push_back(std::move(item));
                         kinds.insert(event.Kind);

                         if (i != 0)
                         {
                              messagesText += "\n";
                         }

                         messagesText += event.Message;
                    }

                    document["events"] = std::move(events);
                    document["messages_text"] = messagesText;
                    if (batch.size() == 1)
                    {
                         document["kind"] = batch.front().Kind;
                         document["message"] = batch.front().Message;
                    }
                    else
                    {
                         json kindList = json::array();
                         for (const auto& kind : kinds)
                         {
                              kindList.push_back(kind);
                         }
                         document["kinds"] = std::move(kindList);
                    }

                    const std::string collection = ResolveCollectionTemplate(PostCollection);
                    HttpPostJson("/collections/" + UrlEncode(collection) + "/documents?distributed=off", document.dump());
                    TraceIrc("post_batch_ok collection=" + collection + " size=" + std::to_string(batch.size()) + ".");
               }
               catch (const std::exception& ex)
               {
                    Log("critical", "IRC module failed to post activity to collection '" + PostCollection + "': " + ex.what());
               }
          }
     }

     /* Send one JSON document through a short-lived HTTP connection. */

     void HttpPostJson(const std::string& path, const std::string& body)
     {
          /* Use a short-lived HTTP socket so posting failures do not poison IRC state. */

          ParsedHttpEndpoint endpoint = ParseHttpEndpoint(PostEndpoint);
          SocketEngine httpSocket;
          std::string socketError;
          if (!httpSocket.Connect(endpoint.Host, endpoint.Port, false, PostTimeoutSeconds * 1000, socketError))
          {
               throw std::runtime_error(socketError.empty() ? "connect failed for " + endpoint.Host : socketError);
          }

          TraceIrc("http_connect host=" + endpoint.Host + " port=" + std::to_string(endpoint.Port) + ".");

          if (!httpSocket.SetTimeoutSeconds(PostTimeoutSeconds, socketError))
          {
               throw std::runtime_error(socketError);
          }

          const std::string requestPath = endpoint.BasePath + path;
          std::ostringstream request;
          request << "POST " << requestPath << " HTTP/1.1\r\n";
          request << "Host: " << endpoint.Host << "\r\n";
          request << "Content-Type: application/json\r\n";
          request << "Content-Length: " << body.size() << "\r\n";
          request << "Connection: close\r\n";
          if (!PostAuthToken.empty())
          {
               if (ToLowerCopy(PostAuthMethod) == "api-key")
               {
                    request << "X-API-Key: " << PostAuthToken << "\r\n";
               }
               else
               {
                    request << "Authorization: Bearer " << PostAuthToken << "\r\n";
               }
          }
          request << "\r\n";
          request << body;

          const std::string payload = request.str();
          if (!httpSocket.SendAll(payload, PostTimeoutSeconds * 1000, socketError))
          {
               throw std::runtime_error(socketError.empty() ? "send failed" : socketError);
          }

          char buffer[1024];
          long bytes = 0;
          const SocketIoResult recvResult = httpSocket.Receive(buffer, sizeof(buffer) - 1, bytes, socketError);
          httpSocket.Close();
          if (recvResult != SocketIoResult::Success || bytes <= 0)
          {
               throw std::runtime_error(socketError.empty() ? "empty HTTP response" : socketError);
          }

          buffer[bytes] = '\0';
          const std::string response(buffer);
          if (response.find(" 200 ") == std::string::npos &&
              response.find(" 201 ") == std::string::npos)
          {
               throw std::runtime_error("HTTP post failed");
          }
     }

     /* Emit verbose internal IRC trace lines when enabled in config. */

     void TraceIrc(const std::string& message)
     {
          if (!LogAllIrcEvents)
          {
               return;
          }

          Log("normal", "IRC trace " + message);
     }

     /* IRC connection identity and reconnect policy. */

     std::string Server;
     std::string Channel;
     std::string Nick;
     std::string User;
     std::string RealName;
     std::string Password;
     int Port = 6667;
     int QueueLimit = 1000;
     int ReconnectDelayMs = 5000;

     /* hlquery posting destination and batch behavior. */

     std::string PostEndpoint = "http://127.0.0.1:9200";
     std::string PostCollection;
     std::string PostAuthMethod = "bearer";
     std::string PostAuthToken;
     int PostTimeoutSeconds = 5;
     int PostIntervalMs = 0;
     bool PostPrivmsgs = true;
     bool PostChannels = true;
     bool PostConnect = true;
     bool LogAllIrcEvents = false;

     /* Local module logging configuration and output stream. */

     bool LocalLoggingEnabled = true;
     std::string LogType = "irc";
     std::unique_ptr<LogStream> FileLog;

     /* Runtime threads, queues, and synchronization primitives. */

     std::atomic<bool> Running{false};
     std::thread Worker;
     std::thread PostWorker;
     std::mutex Mutex;
     std::mutex FileMutex;
     std::mutex PostMutex;
     std::condition_variable Condition;
     std::condition_variable PostCondition;
     std::deque<std::string> Queue;

     /* One queued IRC activity item waiting to be posted. */

     struct ActivityEvent
     {
          std::string Kind;
          std::string Message;
          std::string CreatedAt;
     };

     std::deque<ActivityEvent> PostQueue;

     /* Connection and posting state that changes during runtime. */

     bool Stopping = false;
     bool PostingActive = false;
     std::atomic<bool> PostingThreadRunning{false};
     SocketEngine IrcSocket;
     bool JoinedChannel = false;
     bool Registered = false;
     bool JoinRequested = false;
     bool WelcomeSeen = false;
     bool StartupDrainComplete = false;
     size_t DroppedOnStartup = 0;
     int StartupDropCount = 250;
     std::string ReceiveBuffer;
};

MODULE_LOAD(IRCModule)
