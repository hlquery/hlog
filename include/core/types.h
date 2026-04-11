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

#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

#include "common/options.h"
#include "json/json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

struct PipelineEvent
{
     json Document = json::object();
     bool Dropped = false;
     std::string RawLine;
};

struct AddFieldFilterConfig
{
     std::string Field;
     std::string Value;
};

struct DropContainsFilterConfig
{
     std::string Field = "message";
     std::string Value;
};

struct JsonParseFilterConfig
{
     std::string SourceField = "message";
     std::string TargetField;
     bool DropInvalid = false;
};

struct RegexExtractFilterConfig
{
     std::string SourceField = "message";
     std::string Pattern;
     std::vector<std::string> Fields;
     bool DropOnNoMatch = false;
};

struct RemoveFieldFilterConfig
{
     std::vector<std::string> Fields;
};

struct HlqueryOutputConfig
{
     bool Enabled = false;
     std::string Endpoint = "http://127.0.0.1:9200";
     std::string Collection = "logs";
     std::string AuthToken;
     std::string AuthMethod = "bearer";
     int TimeoutSeconds = 5;
     int BatchLines = 0;
     int BatchIntervalMs = 0;
};

struct StdoutOutputConfig
{
     bool Enabled = false;
};

struct EventConfig
{
     std::string IdField = "id";
     std::string MessageField = "message";
     std::string PathField = "path";
     std::string FileField = "file";
     std::string HostField = "host";
     std::string TagsField = "tags";
     std::string DateField = "ingested_at";
     std::string DateFormat = "%Y-%m-%dT%H:%M:%S";
     std::string HostValue;
     std::string TagsValue;
     bool IncludePath = false;
     bool IncludeFile = false;
     bool IncludeHost = false;
     bool IncludeTags = false;
     bool IncludeDate = false;
};

struct ModuleConfig
{
     std::string Name;
     std::string Path;
     bool Enabled = true;
     std::unordered_map<std::string, std::string> Attributes;
};

struct PipelineConfig
{
     WatchMode Mode = WatchMode::Auto;
     int PollIntervalMs = 1000;
     bool LogAllEvents = false;
     std::vector<HLogInputFile> Inputs;
     std::vector<ModuleConfig> Modules;
     std::vector<AddFieldFilterConfig> AddFieldFilters;
     std::vector<DropContainsFilterConfig> DropContainsFilters;
     std::vector<JsonParseFilterConfig> JsonParseFilters;
     std::vector<RegexExtractFilterConfig> RegexExtractFilters;
     std::vector<RemoveFieldFilterConfig> RemoveFieldFilters;
     StdoutOutputConfig StdoutOutput;
     HlqueryOutputConfig HlqueryOutput;
     EventConfig Event;
     std::string FailureBufferPath;
     bool FailureBufferEnabled = false;
};

class FailureRecorder
{
   public:
     explicit FailureRecorder(fs::path path);

     void Record(const std::string& line, const std::string& source);
     const fs::path& GetPath() const;

   private:
     fs::path Path;
     std::mutex Mutex;
};

struct FileState
{
     fs::path PathValue;
     fs::path ParentDir;
     std::string Label;
     std::ifstream Stream;
     std::string Pending;
     std::uintmax_t Offset = 0;
     std::uintmax_t Size = 0;
     dev_t Device = 0;
     ino_t Inode = 0;
     std::uint64_t ModifiedTick = 0;
     bool Available = false;
     StartPosition Start = StartPosition::End;
};

std::string BaseName(const fs::path& path);
