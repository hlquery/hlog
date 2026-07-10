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

#include "core/pipeline.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "core/hlcore.h"
#include "core/logmanager.h"
#include "core/modulemanager.h"
#include "utils/tools.h"


LogManager* GetPipelineLogger()
{
     return (Instance && Instance->Logs) ? Instance->Logs.get() : nullptr;
}

bool IsForegroundOutputEnabled()
{
     const char* foregroundEnv = std::getenv("HLOG_FOREGROUND");
     return foregroundEnv && std::string(foregroundEnv) == "1";
}

std::string JsonToString(const json& value)
{
     return value.is_string() ? value.get<std::string>() : value.dump();
}

bool TryGetFieldText(const json& document, const std::string& field, std::string& out)
{
     if (!document.contains(field))
     {
          return false;
     }

     out = JsonToString(document[field]);
     return true;
}

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle InvalidSocket = INVALID_SOCKET;

void CloseSocket(SocketHandle sock)
{
     closesocket(sock);
}

void InitializeSockets()
{
     static const bool initialized = []() {
          WSADATA data{};
          const int rc = WSAStartup(MAKEWORD(2, 2), &data);
          if (rc != 0)
          {
               throw std::runtime_error("WSAStartup failed: " + std::to_string(rc));
          }
          return true;
     }();

     (void)initialized;
}
#else
using SocketHandle = int;
constexpr SocketHandle InvalidSocket = -1;

void CloseSocket(SocketHandle sock)
{
     ::close(sock);
}

void InitializeSockets()
{
}
#endif

void MergeObjectIntoDocument(json& document, const json& value)
{
     if (!value.is_object())
     {
          return;
     }

     for (auto it = value.begin(); it != value.end(); ++it)
     {
          document[it.key()] = it.value();
     }
}

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

class HlqueryHttpOutput
{
   public:
     explicit HlqueryHttpOutput(HlqueryOutputConfig config, EventConfig eventConfig,
                                std::vector<AddFieldFilterConfig> addFieldFilters)
         : Config(std::move(config)),
           Event(std::move(eventConfig)),
           AddFieldFilters(std::move(addFieldFilters))
     {
     }

     bool Enabled() const
     {
          return Config.Enabled;
     }

     void EnsureCollection() const
     {
          CreateCollectionIfMissing(ResolveCollectionTemplate(Config.Collection));
     }

     bool Emit(const PipelineEvent& event, std::string* outError) const
     {
          try
          {
               const std::string collection = ResolveCollectionTemplate(Config.Collection);
               const std::string path = "/collections/" + UrlEncode(collection) + "/documents?distributed=off";
               HttpResponse response = PostJson(path, event.Document.dump());
               if (response.StatusCode == 404 || response.Body.find("not found") != std::string::npos ||
                   response.Body.find("Not found") != std::string::npos || response.Body.find("Collection") != std::string::npos)
               {
                    CreateCollectionIfMissing(collection);
                    response = PostJson(path, event.Document.dump());
               }

               if (response.StatusCode < 200 || response.StatusCode >= 300)
               {
                    if (outError)
                    {
                         *outError = "status=" + std::to_string(response.StatusCode) + " body=" + response.Body;
                    }
                    return false;
               }
               return true;
          }
          catch (const std::exception& e)
          {
               if (outError)
               {
                    *outError = e.what();
               }
               return false;
          }
     }

     const HlqueryOutputConfig& GetConfig() const
     {
          return Config;
     }

     const EventConfig& GetEventConfig() const
     {
          return Event;
     }

   private:
     struct ParsedEndpoint
     {
          std::string Host;
          int Port = 80;
          std::string BasePath;
     };

     struct HttpResponse
     {
          int StatusCode = -1;
          std::string Body;
     };

     ParsedEndpoint ParseEndpoint() const
     {
          if (Config.Endpoint.rfind("http://", 0) != 0)
          {
               throw std::runtime_error("Only http:// endpoints are supported by output_hlquery");
          }

          std::string raw = Config.Endpoint.substr(7);
          ParsedEndpoint parsed;

          const size_t slashPos = raw.find('/');
          const std::string hostPort = slashPos == std::string::npos ? raw : raw.substr(0, slashPos);
          parsed.BasePath = slashPos == std::string::npos ? "" : raw.substr(slashPos);

          if (hostPort.empty())
          {
               throw std::runtime_error("output_hlquery endpoint is missing a host");
          }

          if (hostPort.front() == '[')
          {
               const size_t closeBracket = hostPort.find(']');
               if (closeBracket == std::string::npos)
               {
                    throw std::runtime_error("Invalid bracketed IPv6 host in output_hlquery endpoint");
               }

               parsed.Host = hostPort.substr(1, closeBracket - 1);
               if (closeBracket + 1 < hostPort.size())
               {
                    if (hostPort[closeBracket + 1] != ':')
                    {
                         throw std::runtime_error("Invalid output_hlquery endpoint after IPv6 host");
                    }

                    parsed.Port = std::stoi(hostPort.substr(closeBracket + 2));
               }
          }
          else
          {
               const size_t firstColon = hostPort.find(':');
               const size_t lastColon = hostPort.rfind(':');
               if (firstColon != std::string::npos && firstColon == lastColon)
               {
                    parsed.Host = hostPort.substr(0, firstColon);
                    parsed.Port = std::stoi(hostPort.substr(firstColon + 1));
               }
               else
               {
                    parsed.Host = hostPort;
               }
          }

          if (parsed.Host.empty() || parsed.Port <= 0 || parsed.Port > 65535)
          {
               throw std::runtime_error("Invalid output_hlquery endpoint");
          }

          return parsed;
     }

     HttpResponse PostJson(const std::string& path, const std::string& body) const
     {
          InitializeSockets();
          const ParsedEndpoint endpoint = ParseEndpoint();

          addrinfo hints{};
          hints.ai_family = AF_UNSPEC;
          hints.ai_socktype = SOCK_STREAM;

          addrinfo* result = nullptr;
          const std::string portString = std::to_string(endpoint.Port);
          if (::getaddrinfo(endpoint.Host.c_str(), portString.c_str(), &hints, &result) != 0)
          {
               throw std::runtime_error("Failed to resolve " + endpoint.Host);
          }

          SocketHandle sock = InvalidSocket;
          for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next)
          {
               sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
               if (sock == InvalidSocket)
               {
                    continue;
               }

               if (::connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
               {
                    break;
               }

               CloseSocket(sock);
               sock = InvalidSocket;
          }

          ::freeaddrinfo(result);

          if (sock == InvalidSocket)
          {
               throw std::runtime_error("Failed to connect to " + endpoint.Host + ":" + std::to_string(endpoint.Port));
          }

#ifdef _WIN32
          const DWORD timeoutMs = static_cast<DWORD>(Config.TimeoutSeconds * 1000);
          ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
          ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
          timeval timeout{};
          timeout.tv_sec = Config.TimeoutSeconds;
          ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
          ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

          std::ostringstream request;
          request << "POST " << endpoint.BasePath << path << " HTTP/1.1\r\n";
          request << "Host: " << endpoint.Host << ":" << endpoint.Port << "\r\n";
          request << "User-Agent: hlog/1.0\r\n";
          request << "Accept: application/json\r\n";
          request << "Content-Type: application/json\r\n";
          request << "Content-Length: " << body.size() << "\r\n";
          request << "Connection: close\r\n";
          if (!Config.AuthToken.empty())
          {
               if (Config.AuthMethod == "api-key")
               {
                    request << "X-API-Key: " << Config.AuthToken << "\r\n";
               }
               else
               {
                    request << "Authorization: Bearer " << Config.AuthToken << "\r\n";
               }
          }
          request << "\r\n" << body;

          const std::string wire = request.str();
          size_t sentTotal = 0;
          while (sentTotal < wire.size())
          {
               const size_t remaining = wire.size() - sentTotal;
               const int chunkSize = static_cast<int>(std::min<size_t>(
                    remaining, static_cast<size_t>(std::numeric_limits<int>::max())));
               const int sent = ::send(sock, wire.data() + sentTotal, chunkSize, 0);
               if (sent <= 0)
               {
                    CloseSocket(sock);
                    throw std::runtime_error("Failed to send request to output_hlquery");
               }

               sentTotal += static_cast<size_t>(sent);
          }

          std::string response;
          char buffer[4096];
          for (;;)
          {
               const int received = ::recv(sock, buffer, sizeof(buffer), 0);
               if (received <= 0)
               {
                    break;
               }
               response.append(buffer, static_cast<size_t>(received));
          }
          CloseSocket(sock);

          const size_t lineEnd = response.find("\r\n");
          if (lineEnd == std::string::npos)
          {
               throw std::runtime_error("Invalid HTTP response from output_hlquery");
          }

          HttpResponse parsed;
          std::istringstream statusLine(response.substr(0, lineEnd));
          std::string httpVersion;
          statusLine >> httpVersion >> parsed.StatusCode;

          const size_t bodyPos = response.find("\r\n\r\n");
          if (bodyPos != std::string::npos)
          {
               parsed.Body = response.substr(bodyPos + 4);
          }

          return parsed;
     }

     void CreateCollectionIfMissing(const std::string& collection) const
     {
          std::vector<json> fields = {{{"name", Event.MessageField}, {"type", "string"}}};

          if (Event.IncludePath)
          {
               fields.push_back(json{{"name", Event.PathField}, {"type", "string"}});
          }
          if (Event.IncludeFile)
          {
               fields.push_back(json{{"name", Event.FileField}, {"type", "string"}});
          }
          if (Event.IncludeHost)
          {
               fields.push_back(json{{"name", Event.HostField}, {"type", "string"}});
          }
          if (Event.IncludeDate)
          {
               fields.push_back(json{{"name", Event.DateField}, {"type", "string"}});
          }
          if (Event.IncludeTags)
          {
               fields.push_back(json{{"name", Event.TagsField}, {"type", "string"}});
          }

          std::set<std::string> reserved = {
               Event.IdField,
               Event.MessageField,
               Event.PathField,
               Event.FileField,
               Event.HostField,
               Event.DateField,
               Event.TagsField,
          };

          for (const auto& filter : AddFieldFilters)
          {
               if (filter.Field.empty() || reserved.find(filter.Field) != reserved.end())
               {
                    continue;
               }
               fields.push_back(json{{"name", filter.Field}, {"type", "string"}});
               reserved.insert(filter.Field);
          }

          json schema = {
               {"name", collection},
               {"fields", fields},
          };

          const HttpResponse response = PostJson("/collections?distributed=off", schema.dump());
          const bool alreadyExists = response.StatusCode == 409 ||
                                     response.Body.find("already exists") != std::string::npos ||
                                     response.Body.find("Already exists") != std::string::npos;

          if ((response.StatusCode < 200 || response.StatusCode >= 300) && !alreadyExists)
          {
               throw std::runtime_error("Failed to create collection '" + collection + "': status=" +
                                        std::to_string(response.StatusCode) + " body=" + response.Body);
          }
     }

     HlqueryOutputConfig Config;
     EventConfig Event;
     std::vector<AddFieldFilterConfig> AddFieldFilters;
};


class Pipeline::AsyncHlqueryOutput
{
   public:
     AsyncHlqueryOutput(HlqueryOutputConfig config, EventConfig eventConfig,
                        std::vector<AddFieldFilterConfig> addFieldFilters,
                        std::shared_ptr<FailureRecorder> failureRecorder)
         : Output(std::move(config), std::move(eventConfig), std::move(addFieldFilters)),
           Recorder(std::move(failureRecorder))
     {
          LogManager* logs = GetPipelineLogger();
          if (!Output.Enabled())
          {
               return;
          }

          try
          {
               Output.EnsureCollection();
               if (logs)
               {
                    logs->Normal("output_hlquery",
                                 "ready collection=" + Output.GetConfig().Collection +
                                      " endpoint=" + Output.GetConfig().Endpoint);
               }
          }
          catch (const std::exception& e)
          {
               if (logs)
               {
                    logs->Normal("output_hlquery",
                                 "startup_collection_check_failed collection=" + Output.GetConfig().Collection +
                                      " endpoint=" + Output.GetConfig().Endpoint +
                                      " error=" + std::string(e.what()));
               }
          }

          Worker = std::thread(&AsyncHlqueryOutput::Run, this);
     }

     ~AsyncHlqueryOutput()
     {
          Shutdown();
     }

     bool Enabled() const
     {
          return Output.Enabled();
     }

     const HlqueryOutputConfig& GetConfig() const
     {
          return Output.GetConfig();
     }

     void Enqueue(PipelineEvent event, std::string filePath) const
     {
          {
               std::lock_guard<std::mutex> lock(Mutex);
               Queue.push_back(QueuedEvent{std::move(event), std::move(filePath)});
          }
          Condition.notify_one();
     }

     void Shutdown()
     {
          {
               std::lock_guard<std::mutex> lock(Mutex);
               if (Stopping)
               {
                    return;
               }
               Stopping = true;
          }

          Condition.notify_all();
          if (Worker.joinable())
          {
               Worker.join();
          }
     }

   private:
     struct QueuedEvent
     {
          PipelineEvent Event;
          std::string FilePath;
     };

     std::string GetEventTimestamp(const PipelineEvent& event) const
     {
          const std::string& field = Output.GetEventConfig().DateField;
          if (!field.empty())
          {
               auto it = event.Document.find(field);
               if (it != event.Document.end() && it->is_string())
               {
                    return it->get<std::string>();
               }
          }

          return Tools::GetTimestamp("%Y-%m-%dT%H:%M:%S");
     }

     void Run()
     {
          for (;;)
          {
               std::vector<QueuedEvent> batch;
               {
                    std::unique_lock<std::mutex> lock(Mutex);
                    Condition.wait(lock, [this]() { return Stopping || !Queue.empty(); });
                    if (Stopping && Queue.empty())
                    {
                         break;
                    }

                    if (Output.GetConfig().BatchLines <= 1 && Output.GetConfig().BatchIntervalMs <= 0)
                    {
                         batch.push_back(std::move(Queue.front()));
                         Queue.pop_front();
                    }
                    else
                    {
                         const size_t maxLines = Output.GetConfig().BatchLines > 0
                              ? static_cast<size_t>(Output.GetConfig().BatchLines)
                              : std::numeric_limits<size_t>::max();
                         const int intervalMs = Output.GetConfig().BatchIntervalMs;

                         if (intervalMs > 0 && Queue.size() < maxLines)
                         {
                              const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
                              while (!Stopping && Queue.size() < maxLines)
                              {
                                   if (Condition.wait_until(lock, deadline, [this, maxLines]() {
                                        return Stopping || Queue.size() >= maxLines;
                                   }))
                                   {
                                        break;
                                   }

                                   if (std::chrono::steady_clock::now() >= deadline)
                                   {
                                        break;
                                   }
                              }
                         }

                         while (!Queue.empty() && batch.size() < maxLines)
                         {
                              batch.push_back(std::move(Queue.front()));
                              Queue.pop_front();
                         }
                    }
               }

               if (batch.empty())
               {
                    continue;
               }

               PipelineEvent eventToEmit = batch.front().Event;
               std::string filePath = batch.front().FilePath;
               const std::string batchFrom = GetEventTimestamp(batch.front().Event);
               const std::string batchTo = GetEventTimestamp(batch.back().Event);
               const std::string batchId = SanitizeIdPart(
                    "file:" + fs::path(filePath).filename().string() + ":" + batchFrom + ":" + batchTo + ":" + std::to_string(batch.size()));

               std::string joined;
               joined.reserve(batch.size() * 32);
               json events = json::array();

               for (size_t i = 0; i < batch.size(); ++i)
               {
                    if (i != 0)
                    {
                         joined += "\n";
                    }

                    joined += batch[i].Event.RawLine;

                    json eventDocument = batch[i].Event.Document;
                    eventDocument["raw_line"] = batch[i].Event.RawLine;
                    eventDocument["source_path"] = batch[i].FilePath;
                    events.push_back(std::move(eventDocument));
               }

               eventToEmit.RawLine = joined;
               eventToEmit.Document[Output.GetEventConfig().MessageField] = joined;
               eventToEmit.Document[Output.GetEventConfig().IdField] = batchId;
               eventToEmit.Document["batch_from"] = batchFrom;
               eventToEmit.Document["batch_to"] = batchTo;
               eventToEmit.Document["event_count"] = batch.size();
               eventToEmit.Document["events"] = std::move(events);
               eventToEmit.Document["messages_text"] = joined;

               std::string error;
               if (!Output.Emit(eventToEmit, &error))
               {
                    LogManager* logs = GetPipelineLogger();
                    if (logs)
                    {
                         logs->Critical("output_hlquery",
                                        "collection=" + Output.GetConfig().Collection +
                                             " endpoint=" + Output.GetConfig().Endpoint +
                                             " error=" + error);
                    }
                    if (Recorder)
                    {
                         for (const auto& queued : batch)
                         {
                              Recorder->Record(queued.Event.RawLine, queued.FilePath);
                              logs = GetPipelineLogger();
                              if (logs)
                              {
                                   logs->Sparse("failure_buffer",
                                                "appended line path=" + queued.FilePath + " buffer=" + Recorder->GetPath().string());
                              }
                         }
                    }
                    continue;
               }

          }
     }

     HlqueryHttpOutput Output;
     std::thread Worker;
     mutable std::deque<QueuedEvent> Queue;
     mutable std::mutex Mutex;
     mutable std::condition_variable Condition;
     bool Stopping = false;
     std::shared_ptr<FailureRecorder> Recorder;
};

Pipeline::Pipeline(PipelineConfig config)
    : Config(std::move(config)),
      FailureRecorderPtr(Config.FailureBufferEnabled && !Config.FailureBufferPath.empty()
                             ? std::make_shared<FailureRecorder>(fs::path(Config.FailureBufferPath))
                             : nullptr),
      ModuleManager(std::make_unique<HLogModuleManager>()),
      HlqueryOutput(std::make_unique<AsyncHlqueryOutput>(
           Config.HlqueryOutput, Config.Event, Config.AddFieldFilters, FailureRecorderPtr))
{
     std::string moduleError;
     if (ModuleManager && !ModuleManager->LoadModules(Config, moduleError))
     {
          throw std::runtime_error(moduleError);
     }
}

Pipeline::~Pipeline() = default;

const PipelineConfig& Pipeline::GetConfig() const
{
     return Config;
}

bool Pipeline::HasSourceModule() const
{
     return ModuleManager && ModuleManager->HasSourceModule();
}

bool Pipeline::RunSourceModule(WatchMode mode, int intervalMs, std::string& errorMessage) const
{
     if (!ModuleManager)
     {
          return true;
     }

     return ModuleManager->RunSourceModule(*this, mode, intervalMs, errorMessage);
}

void Pipeline::ProcessLine(const FileState& state, const std::string& line) const
{
     LogManager* logs = GetPipelineLogger();
     PipelineEvent event;
     event.RawLine = line;
     event.Document[Config.Event.IdField] = Tools::RandomHex(24);
     event.Document[Config.Event.MessageField] = line;

     if (Config.LogAllEvents && logs)
     {
          logs->Normal("pipeline_event",
                       "received path=" + state.PathValue.string() +
                            " file=" + state.Label +
                            " bytes=" + std::to_string(line.size()) + ".");
     }

     if (Config.Event.IncludePath)
     {
          event.Document[Config.Event.PathField] = state.PathValue.string();
     }
     if (Config.Event.IncludeFile)
     {
          event.Document[Config.Event.FileField] = state.Label;
     }
     if (Config.Event.IncludeHost)
     {
          event.Document[Config.Event.HostField] = Config.Event.HostValue;
     }
     if (Config.Event.IncludeDate)
     {
          event.Document[Config.Event.DateField] = Tools::GetTimestamp(Config.Event.DateFormat);
     }
     if (Config.Event.IncludeTags)
     {
          event.Document[Config.Event.TagsField] = Config.Event.TagsValue;
     }

     for (const auto& filter : Config.AddFieldFilters)
     {
          event.Document[filter.Field] = filter.Value;

          if (Config.LogAllEvents && logs)
          {
               logs->Normal("pipeline_event",
                            "add_field field=" + filter.Field +
                                 " value=" + filter.Value + ".");
          }
     }

     for (const auto& filter : Config.JsonParseFilters)
     {
          std::string sourceValue;
          if (!TryGetFieldText(event.Document, filter.SourceField, sourceValue))
          {
               continue;
          }

          try
          {
               const json parsed = json::parse(sourceValue);
               if (!filter.TargetField.empty())
               {
                    event.Document[filter.TargetField] = parsed;
                }
               else if (parsed.is_object())
               {
                    MergeObjectIntoDocument(event.Document, parsed);
                }
               else
               {
                    event.Document[filter.SourceField] = parsed;
               }
          }
          catch (const std::exception&)
          {
               if (filter.DropInvalid)
               {
                    event.Dropped = true;

                    if (Config.LogAllEvents && logs)
                    {
                         logs->Normal("pipeline_event",
                                      "json_parse_drop field=" + filter.SourceField + ".");
                    }

                    break;
               }
          }
     }

     if (event.Dropped)
     {
          if (Config.LogAllEvents && logs)
          {
               logs->Normal("pipeline_event",
                            "dropped path=" + state.PathValue.string() + " stage=json_parse.");
          }

          return;
     }

     for (const auto& filter : Config.RegexExtractFilters)
     {
          std::string sourceValue;
          if (!TryGetFieldText(event.Document, filter.SourceField, sourceValue))
          {
               if (filter.DropOnNoMatch)
               {
                    event.Dropped = true;
                    break;
               }
               continue;
          }

          std::smatch match;
          const std::regex pattern(filter.Pattern);
          if (!std::regex_search(sourceValue, match, pattern))
          {
               if (filter.DropOnNoMatch)
               {
                    event.Dropped = true;
                    break;
               }
               continue;
          }

          for (size_t i = 0; i < filter.Fields.size(); ++i)
          {
               const size_t matchIndex = i + 1;
               if (matchIndex >= match.size() || filter.Fields[i].empty())
               {
                    continue;
               }

               event.Document[filter.Fields[i]] = match[matchIndex].str();
          }

          if (Config.LogAllEvents && logs)
          {
               logs->Normal("pipeline_event",
                            "regex_extract field=" + filter.SourceField +
                                 " captures=" + std::to_string(filter.Fields.size()) + ".");
          }
     }

     if (event.Dropped)
     {
          if (Config.LogAllEvents && logs)
          {
               logs->Normal("pipeline_event",
                            "dropped path=" + state.PathValue.string() + " stage=regex_extract.");
          }

          return;
     }

     for (const auto& filter : Config.DropContainsFilters)
     {
          if (!event.Document.contains(filter.Field))
          {
               continue;
          }

          const std::string value = JsonToString(event.Document[filter.Field]);
          if (value.find(filter.Value) != std::string::npos)
          {
               event.Dropped = true;

               if (Config.LogAllEvents && logs)
               {
                    logs->Normal("pipeline_event",
                                 "drop_contains field=" + filter.Field +
                                      " value=" + filter.Value + ".");
               }

               break;
          }
     }

     if (event.Dropped)
     {
          if (Config.LogAllEvents && logs)
          {
               logs->Normal("pipeline_event",
                            "dropped path=" + state.PathValue.string() + " stage=drop_contains.");
          }

          return;
     }

     for (const auto& filter : Config.RemoveFieldFilters)
     {
          for (const auto& field : filter.Fields)
          {
               event.Document.erase(field);
          }
     }

     if (ModuleManager && !ModuleManager->Empty())
     {
          ModuleManager->ProcessEvent(event, state);
     }

     if (event.Dropped)
     {
          if (Config.LogAllEvents && logs)
          {
               logs->Normal("pipeline_event",
                            "dropped path=" + state.PathValue.string() + " stage=module.");
          }

          return;
     }

     if (Config.StdoutOutput.Enabled && IsForegroundOutputEnabled())
     {
          std::cout << "[" << state.Label << "] " << line << std::endl;
     }

     if (HlqueryOutput && HlqueryOutput->Enabled())
     {
          if (Config.LogAllEvents && logs)
          {
               logs->Normal("pipeline_event",
                            "enqueue_output path=" + state.PathValue.string() +
                                 " collection=" + HlqueryOutput->GetConfig().Collection + ".");
          }

          HlqueryOutput->Enqueue(std::move(event), state.PathValue.string());
     }
}
