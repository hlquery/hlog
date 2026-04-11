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
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <cctype>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "core/config.h"
#include "core/configreader.h"
#include "core/serverconfig.h"
#include "utils/tools.h"

namespace
{

using TemplateAttributes = std::unordered_map<std::string, std::string>;

std::string ResolvePathLikeServerConfig(const std::string& configFilePath, const std::string& rawPath);
int ParseBatchIntervalMs(const std::string& rawValue, int fallback);

std::vector<std::string> SplitCommaSeparated(const std::string& value)
{
     std::vector<std::string> result;
     std::stringstream stream(value);
     std::string token;
     while (std::getline(stream, token, ','))
     {
          const size_t start = token.find_first_not_of(" \t\r\n");
          if (start == std::string::npos)
          {
               continue;
          }

          const size_t end = token.find_last_not_of(" \t\r\n");
          result.push_back(token.substr(start, end - start + 1));
     }

     return result;
}

std::string ResolveCommaSeparatedPaths(const std::string& configFilePath, const std::string& rawValue)
{
     if (rawValue.empty())
     {
          return rawValue;
     }

     const auto parts = SplitCommaSeparated(rawValue);
     std::ostringstream resolved;

     for (size_t i = 0; i < parts.size(); ++i)
     {
          if (i != 0)
          {
               resolved << ",";
          }

          resolved << ResolvePathLikeServerConfig(configFilePath, parts[i]);
     }

     return resolved.str();
}

std::string ResolvePathLikeServerConfig(const std::string& configFilePath, const std::string& rawPath)
{
     if (rawPath.empty())
     {
          return rawPath;
     }

     fs::path pathValue(rawPath);
     if (pathValue.is_absolute())
     {
          return pathValue.lexically_normal().string();
     }

     std::error_code ec;
     fs::path configPathValue(configFilePath);
     if (!configPathValue.is_absolute())
     {
          configPathValue = fs::absolute(configPathValue, ec);
          ec.clear();
     }

     const fs::path configDirValue = configPathValue.parent_path();
     std::vector<fs::path> baseDirs;

     if (!configDirValue.empty())
     {
          baseDirs.push_back(configDirValue);
          baseDirs.push_back(configDirValue.parent_path());
          baseDirs.push_back(configDirValue.parent_path().parent_path());
     }

     const fs::path configDirFallback(HLQUERY_CONFIG_DIR);
     if (!configDirFallback.empty())
     {
          baseDirs.push_back(configDirFallback);
          baseDirs.push_back(configDirFallback.parent_path());
          baseDirs.push_back(configDirFallback.parent_path().parent_path());
     }

     for (const auto& baseDir : baseDirs)
     {
          if (baseDir.empty())
          {
               continue;
          }

          fs::path candidate = (baseDir / pathValue).lexically_normal();
          if (fs::exists(candidate, ec))
          {
               return candidate.string();
          }
     }

     if (!configDirValue.empty())
     {
          return (configDirValue / pathValue).lexically_normal().string();
     }

     return pathValue.lexically_normal().string();
}

int ParseBatchIntervalMs(const std::string& rawValue, int fallback)
{
     if (rawValue.empty())
     {
          return fallback;
     }

     std::string value = rawValue;
     std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
     });

     long long multiplier = 1000;

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
          multiplier = 60LL * 1000LL;
          value.pop_back();
     }
     else if (!value.empty() && value.back() == 'h')
     {
          multiplier = 60LL * 60LL * 1000LL;
          value.pop_back();
     }
     else if (!value.empty() && value.back() == 'd')
     {
          multiplier = 24LL * 60LL * 60LL * 1000LL;
          value.pop_back();
     }

     try
     {
          const long long parsed = std::stoll(value);
          if (parsed < 0)
          {
               return fallback;
          }

          const long long millis = parsed * multiplier;
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

std::string RewriteTagAttributePath(const std::string& content,
                                    const std::string& configFilePath,
                                    const std::string& tagName,
                                    const std::string& attributeName)
{
     const std::regex pattern("(<\\s*" + tagName + "\\b[^>]*\\b" + attributeName + "\\s*=\\s*\")([^\"]*)(\")");

     std::string rewritten;
     rewritten.reserve(content.size() + 128);

     std::string::const_iterator searchStart = content.cbegin();
     std::smatch match;
     while (std::regex_search(searchStart, content.cend(), match, pattern))
     {
          rewritten.append(searchStart, match[0].first);
          rewritten += match[1].str();
          rewritten += ResolvePathLikeServerConfig(configFilePath, match[2].str());
          rewritten += match[3].str();
          searchStart = match[0].second;
     }

     rewritten.append(searchStart, content.cend());
     return rewritten;
}

std::string CreateReaderSafeHlogConfig(const std::string& path)
{
     std::ifstream input(path);
     if (!input.is_open())
     {
          throw std::runtime_error("Failed to open hlog config " + path + ".");
     }

     std::ostringstream buffer;
     buffer << input.rdbuf();

     std::string content = buffer.str();
     content = RewriteTagAttributePath(content, path, "include", "file");
     content = RewriteTagAttributePath(content, path, "input_file", "path");
     content = RewriteTagAttributePath(content, path, "watch", "file");
     content = RewriteTagAttributePath(content, path, "failure_buffer", "path");

     std::error_code ec;
     fs::path tempPath = fs::temp_directory_path(ec);
     if (ec || tempPath.empty())
     {
          tempPath = fs::absolute(path).parent_path();
          ec.clear();
     }

     tempPath /= "hlog_reader_" +
#ifdef _WIN32
                 std::to_string(_getpid()) +
#else
                 std::to_string(::getpid()) +
#endif
                 ".conf";

     std::ofstream output(tempPath);
     if (!output.is_open())
     {
          throw std::runtime_error("Failed to write temporary hlog config " + tempPath.string() + ".");
     }

     output << content;
     return tempPath.string();
}

std::unordered_map<std::string, TemplateAttributes> LoadTemplateAttributes(const ConfigReader& reader)
{
     std::unordered_map<std::string, TemplateAttributes> templates;

     for (const auto& templateTag : reader.GetTags("template"))
     {
          const std::string name = templateTag->GetString("name", "");
          if (name.empty())
          {
               continue;
          }

          TemplateAttributes attributes = templateTag->GetAttributes();
          attributes.erase("name");
          templates[name] = std::move(attributes);
     }

     return templates;
}

TemplateAttributes ResolveTemplateAttributes(const std::unordered_map<std::string, TemplateAttributes>& templates,
                                             TemplateAttributes attributes)
{
     const auto templateIt = attributes.find("template");
     if (templateIt == attributes.end() || templateIt->second.empty())
     {
          return attributes;
     }

     const auto namedTemplate = templates.find(templateIt->second);
     if (namedTemplate == templates.end())
     {
          throw std::runtime_error("Unknown hlog template '" + templateIt->second + "'.");
     }

     TemplateAttributes merged = namedTemplate->second;

     for (const auto& [key, value] : attributes)
     {
          if (key == "template")
          {
               continue;
          }

          merged[key] = value;
     }

     return merged;
}

}

FailureRecorder::FailureRecorder(fs::path path)
    : Path(std::move(path))
{
}

void FailureRecorder::Record(const std::string& line, const std::string& source)
{
     if (Path.empty() || line.empty())
     {
          return;
     }

     std::lock_guard<std::mutex> lock(Mutex);
     try
     {
          if (!Path.parent_path().empty())
          {
               fs::create_directories(Path.parent_path());
          }
     }
     catch (const std::exception&)
     {
     }

     std::ofstream out(Path, std::ios::app);
     if (!out)
     {
          return;
     }

     const std::string timestamp = Tools::GetTimestamp("%Y-%m-%dT%H:%M:%S");
     out << timestamp << " [" << source << "] " << line << "\n";
}

const fs::path& FailureRecorder::GetPath() const
{
     return Path;
}

std::string BaseName(const fs::path& path)
{
     const auto filename = path.filename().string();
     return filename.empty() ? path.string() : filename;
}

std::vector<fs::path> ResolveFilesFromHlqueryConfig(const std::string& configPath)
{
     ServerConfig config(0, nullptr);
     config.SetConfigFile(configPath);
     config.SetNoForkMode(true);

     if (!config.LoadConfig(configPath))
     {
          throw std::runtime_error(config.GetError());
     }

     std::vector<fs::path> result;
     std::set<std::string> seen;
     for (const auto& logConfig : config.GetLogConfigs())
     {
          if (logConfig.method != "file")
          {
               continue;
          }

          fs::path path(logConfig.target);
          const std::string normalized = path.lexically_normal().string();
          if (seen.insert(normalized).second)
          {
               result.push_back(path);
          }
     }

     return result;
}

void ValidateHlqueryConfig(const std::string& configPath)
{
     ServerConfig config(0, nullptr);
     config.SetConfigFile(configPath);
     config.SetNoForkMode(true);

     if (!config.LoadConfig(configPath))
     {
          throw std::runtime_error(config.GetError());
     }
}

PipelineConfig LoadPipelineConfig(const std::string& path)
{
     PipelineConfig config;
     if (!fs::exists(path))
     {
          return config;
     }

     ConfigReader reader;
     const std::string readerConfigPath = CreateReaderSafeHlogConfig(path);

     struct TempConfigCleanup
     {
          std::string PathValue;

          ~TempConfigCleanup()
          {
               if (!PathValue.empty())
               {
                    std::error_code ec;
                    fs::remove(PathValue, ec);
               }
          }
     } cleanup{readerConfigPath};

     if (!reader.LoadFile(readerConfigPath))
     {
          throw std::runtime_error("Failed to load hlog config " + path + ": " + reader.GetError());
     }

     const auto templates = LoadTemplateAttributes(reader);

     const fs::path configDir = fs::absolute(path).parent_path();

     for (const auto& inputTag : reader.GetTags("input_file"))
     {
          const std::string fileValue = inputTag->GetString("path", "");
          if (fileValue.empty())
          {
               continue;
          }

          HLogInputFile input;
          fs::path inputPath(fileValue);
          input.PathValue = inputPath.is_relative() ? (configDir / inputPath).lexically_normal() : inputPath;
          input.Start = ParseStartPosition(inputTag->GetString("start_position", "end"));

          const std::string methodValue = inputTag->GetString("method", "");
          if (!methodValue.empty())
          {
               input.ModeOverride = ParseMode(methodValue);
          }
          if (inputTag->HasAttribute("refresh_ms"))
          {
               input.IntervalMsOverride = std::max(100, inputTag->GetInt("refresh_ms", config.PollIntervalMs));
          }

          config.Inputs.push_back(std::move(input));
     }

     for (const auto& legacyWatchTag : reader.GetTags("watch"))
     {
          const std::string fileValue = legacyWatchTag->GetString("file", "");
          if (fileValue.empty())
          {
               continue;
          }

          HLogInputFile input;
          fs::path inputPath(fileValue);
          input.PathValue = inputPath.is_relative() ? (configDir / inputPath).lexically_normal() : inputPath;
          input.Start = StartPosition::End;
          config.Inputs.push_back(std::move(input));
     }

     for (const auto& moduleTag : reader.GetTags("module"))
     {
          ModuleConfig module;
          module.Name = moduleTag->GetString("name", "");
          module.Attributes = ResolveTemplateAttributes(templates, moduleTag->GetAttributes());

          const std::string pathValue = moduleTag->GetString("path", "");
          if (!pathValue.empty())
          {
               fs::path modulePath(pathValue);
               module.Path = modulePath.is_relative() ? (configDir / modulePath).lexically_normal().string() : modulePath.string();
          }

          if (!module.Name.empty())
          {
               if (auto moduleConfigTag = reader.GetTag(module.Name))
               {
                    for (const auto& [key, value] : ResolveTemplateAttributes(templates, moduleConfigTag->GetAttributes()))
                    {
                         module.Attributes[key] = value;
                    }
               }

               if (auto moduleConnectTag = reader.GetTag(module.Name + "_connect"))
               {
                    for (const auto& [key, value] : ResolveTemplateAttributes(templates, moduleConnectTag->GetAttributes()))
                    {
                         module.Attributes[key] = value;
                    }
               }

               if (auto moduleLogTag = reader.GetTag(module.Name + "_log"))
               {
                    for (const auto& [key, value] : ResolveTemplateAttributes(templates, moduleLogTag->GetAttributes()))
                    {
                         module.Attributes[key] = value;
                    }
               }

               if (module.Name == "irc")
               {
                    if (auto logTag = reader.GetTag("log"))
                    {
                         for (const auto& [key, value] : ResolveTemplateAttributes(templates, logTag->GetAttributes()))
                         {
                              module.Attributes[key] = value;
                         }
                    }
               }

               if (module.Name == "filein" || module.Name == "redis")
               {
                    if (module.Name == "filein")
                    {
                         const auto pathIt = module.Attributes.find("path");
                         if (pathIt != module.Attributes.end())
                         {
                              pathIt->second = ResolveCommaSeparatedPaths(path, pathIt->second);
                         }
                    }

                    const auto endpointIt = module.Attributes.find("endpoint");
                    if (endpointIt != module.Attributes.end() && !endpointIt->second.empty())
                    {
                         config.HlqueryOutput.Enabled = true;
                         config.HlqueryOutput.Endpoint = endpointIt->second;
                    }

                    const auto collectionIt = module.Attributes.find("collection");
                    if (collectionIt != module.Attributes.end() && !collectionIt->second.empty())
                    {
                         config.HlqueryOutput.Enabled = true;
                         config.HlqueryOutput.Collection = collectionIt->second;
                    }

                    const auto authMethodIt = module.Attributes.find("auth_method");
                    if (authMethodIt != module.Attributes.end() && !authMethodIt->second.empty())
                    {
                         config.HlqueryOutput.AuthMethod = authMethodIt->second;
                    }

                    const auto authTokenIt = module.Attributes.find("auth_token");
                    if (authTokenIt != module.Attributes.end())
                    {
                         config.HlqueryOutput.AuthToken = authTokenIt->second;
                    }

                    const auto timeoutIt = module.Attributes.find("timeout");
                    if (timeoutIt != module.Attributes.end() && !timeoutIt->second.empty())
                    {
                         config.HlqueryOutput.TimeoutSeconds = std::stoi(timeoutIt->second);
                    }

                    const auto batchLinesIt = module.Attributes.find("batch_lines");
                    if (batchLinesIt != module.Attributes.end() && !batchLinesIt->second.empty())
                    {
                         config.HlqueryOutput.BatchLines = std::max(0, std::stoi(batchLinesIt->second));
                    }

                    const auto batchIntervalIt = module.Attributes.find("batch_interval");
                    if (batchIntervalIt != module.Attributes.end() && !batchIntervalIt->second.empty())
                    {
                         config.HlqueryOutput.BatchIntervalMs = ParseBatchIntervalMs(batchIntervalIt->second, config.HlqueryOutput.BatchIntervalMs);
                    }
               }

               config.Modules.push_back(std::move(module));
          }
     }

     for (const auto& filterTag : reader.GetTags("filter_add_field"))
     {
          AddFieldFilterConfig filter;
          filter.Field = filterTag->GetString("field", "");
          filter.Value = filterTag->GetString("value", "");
          if (!filter.Field.empty())
          {
               config.AddFieldFilters.push_back(std::move(filter));
          }
     }

     for (const auto& filterTag : reader.GetTags("filter_drop_if_contains"))
     {
          DropContainsFilterConfig filter;
          filter.Field = filterTag->GetString("field", "message");
          filter.Value = filterTag->GetString("value", "");
          if (!filter.Value.empty())
          {
               config.DropContainsFilters.push_back(std::move(filter));
          }
     }

     for (const auto& filterTag : reader.GetTags("filter_json_parse"))
     {
          JsonParseFilterConfig filter;
          filter.SourceField = filterTag->GetString("field", filter.SourceField);
          filter.TargetField = filterTag->GetString("target_field", "");
          filter.DropInvalid = filterTag->GetBool("drop_invalid", false);
          config.JsonParseFilters.push_back(std::move(filter));
     }

     for (const auto& filterTag : reader.GetTags("filter_regex_extract"))
     {
          RegexExtractFilterConfig filter;
          filter.SourceField = filterTag->GetString("field", filter.SourceField);
          filter.Pattern = filterTag->GetString("pattern", "");
          filter.Fields = SplitCommaSeparated(filterTag->GetString("fields", ""));
          filter.DropOnNoMatch = filterTag->GetBool("drop_on_no_match", false);
          if (!filter.Pattern.empty() && !filter.Fields.empty())
          {
               config.RegexExtractFilters.push_back(std::move(filter));
          }
     }

     for (const auto& filterTag : reader.GetTags("filter_remove_field"))
     {
          RemoveFieldFilterConfig filter;
          filter.Fields = SplitCommaSeparated(filterTag->GetString("fields", ""));
          if (!filter.Fields.empty())
          {
               config.RemoveFieldFilters.push_back(std::move(filter));
          }
     }

     if (auto stdoutTag = reader.GetTag("output_stdout"))
     {
          config.StdoutOutput.Enabled = stdoutTag->GetBool("enabled", true);
          config.LogAllEvents = stdoutTag->GetBool("log_all_events", config.LogAllEvents);
     }

     if (auto eventTag = reader.GetTag("event"))
     {
          config.Event.IdField = eventTag->GetString("id_field", config.Event.IdField);
          config.Event.MessageField = eventTag->GetString("message_field", config.Event.MessageField);
          config.Event.PathField = eventTag->GetString("path_field", config.Event.PathField);
          config.Event.FileField = eventTag->GetString("file_field", config.Event.FileField);
          config.Event.HostField = eventTag->GetString("host_field", config.Event.HostField);
          config.Event.TagsField = eventTag->GetString("tags_field", config.Event.TagsField);
          config.Event.DateField = eventTag->GetString("date_field", config.Event.DateField);
          config.Event.DateFormat = eventTag->GetString("date_format", config.Event.DateFormat);
          config.Event.HostValue = eventTag->GetString("host_value", config.Event.HostValue);
          config.Event.TagsValue = eventTag->GetString("tags_value", config.Event.TagsValue);
          config.Event.IncludePath = eventTag->GetBool("include_path", config.Event.IncludePath);
          config.Event.IncludeFile = eventTag->GetBool("include_file", config.Event.IncludeFile);
          config.Event.IncludeHost = eventTag->GetBool("include_host", config.Event.IncludeHost);
          config.Event.IncludeTags = eventTag->GetBool("include_tags", config.Event.IncludeTags);
          config.Event.IncludeDate = eventTag->GetBool("include_date", config.Event.IncludeDate);
     }

     if (auto pushTag = reader.GetTag("output_hlquery"))
     {
          const auto attributes = ResolveTemplateAttributes(templates, pushTag->GetAttributes());
          ConfigTag resolvedTag("output_hlquery");
          for (const auto& [key, value] : attributes)
          {
               resolvedTag.SetAttribute(key, value);
          }

          config.HlqueryOutput.Enabled = resolvedTag.GetBool("enabled", false);
          config.HlqueryOutput.Endpoint = resolvedTag.GetString("endpoint", config.HlqueryOutput.Endpoint);
          config.HlqueryOutput.Collection = resolvedTag.GetString("collection", config.HlqueryOutput.Collection);
          config.HlqueryOutput.AuthMethod = resolvedTag.GetString("auth_method", config.HlqueryOutput.AuthMethod);
          config.HlqueryOutput.AuthToken = resolvedTag.GetString("auth_token", "");
          config.HlqueryOutput.TimeoutSeconds = resolvedTag.GetInt("timeout", config.HlqueryOutput.TimeoutSeconds);
          config.HlqueryOutput.BatchLines = resolvedTag.GetInt("batch_lines", config.HlqueryOutput.BatchLines);
          config.HlqueryOutput.BatchIntervalMs = ParseBatchIntervalMs(resolvedTag.GetString("batch_interval", ""), config.HlqueryOutput.BatchIntervalMs);
     }

     if (auto legacyPushTag = reader.GetTag("push"))
     {
          const auto attributes = ResolveTemplateAttributes(templates, legacyPushTag->GetAttributes());
          ConfigTag resolvedTag("push");
          for (const auto& [key, value] : attributes)
          {
               resolvedTag.SetAttribute(key, value);
          }

          config.HlqueryOutput.Enabled = resolvedTag.GetBool("enabled", config.HlqueryOutput.Enabled);
          config.HlqueryOutput.Endpoint = resolvedTag.GetString("endpoint", config.HlqueryOutput.Endpoint);
          config.HlqueryOutput.Collection = resolvedTag.GetString("collection", config.HlqueryOutput.Collection);
          config.HlqueryOutput.AuthMethod = resolvedTag.GetString("auth_method", config.HlqueryOutput.AuthMethod);
          config.HlqueryOutput.AuthToken = resolvedTag.GetString("auth_token", config.HlqueryOutput.AuthToken);
          config.HlqueryOutput.TimeoutSeconds = resolvedTag.GetInt("timeout", config.HlqueryOutput.TimeoutSeconds);
          config.HlqueryOutput.BatchLines = resolvedTag.GetInt("batch_lines", config.HlqueryOutput.BatchLines);
          config.HlqueryOutput.BatchIntervalMs = ParseBatchIntervalMs(resolvedTag.GetString("batch_interval", ""), config.HlqueryOutput.BatchIntervalMs);
     }

     if (auto bufferTag = reader.GetTag("failure_buffer"))
     {
          config.FailureBufferEnabled = bufferTag->GetBool("enabled", true);
          const std::string pathValue = bufferTag->GetString("path", "");
          if (!pathValue.empty())
          {
               fs::path bufferPath(pathValue);
               if (bufferPath.is_relative())
               {
                    bufferPath = (configDir / bufferPath).lexically_normal();
               }
               config.FailureBufferPath = bufferPath.string();
          }
     }

     return config;
}
