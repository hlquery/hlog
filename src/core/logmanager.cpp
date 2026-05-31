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

#include "core/logmanager.h"
#include "core/serverconfig.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;


std::string GetTimestamp(const char* format)
{
     const std::time_t now = std::time(nullptr);
     std::tm tm_value{};
     localtime_r(&now, &tm_value);

     char buffer[64];
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
     if (std::strftime(buffer, sizeof(buffer), format, &tm_value) == 0)
     {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
          return "";
     }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

     return buffer;
}


LogStream::LogStream(const LogConfig& config)
     : ConfigValue(config),
       LastRotationTime(std::time(nullptr))
{
     if (ConfigValue.method == "file")
     {
          OpenFile();
     }
}

LogStream::~LogStream()
{
     Flush();
}

bool LogStream::IsOpen() const
{
     if (ConfigValue.method == "console")
     {
          return true;
     }

     return FileStream && FileStream->is_open();
}

void LogStream::OpenFile()
{
     fs::path target(ConfigValue.target);
     if (!target.parent_path().empty())
     {
          std::error_code ec;
          fs::create_directories(target.parent_path(), ec);
     }

     FileStream = std::make_unique<std::ofstream>(ConfigValue.target, std::ios::app);
}

void LogStream::Reopen()
{
     std::lock_guard<std::mutex> lock(WriteMutex);

     if (FileStream && FileStream->is_open())
     {
          FileStream->close();
     }

     OpenFile();
}

std::string LogStream::LevelToTag(LogLevel level) const
{
     switch (level)
     {
          case LogLevel::LOG_CRITICAL:
               return "CRIT";
          case LogLevel::LOG_SPARSE:
          case LogLevel::LOG_NORMAL:
          case LogLevel::LOG_VERBOSE:
          case LogLevel::LOG_DEBUG:
               return "OK";
          default:
               return "LOG";
     }
}

std::string LogStream::FormatLogLine(LogLevel level, const std::string& type, const std::string& message) const
{
     std::ostringstream out;
     out << "[" << GetTimestamp("%Y-%m-%d %H:%M:%S") << "] "
         << "[ " << LevelToTag(level) << " ] "
         << "[" << type << "] "
         << message;
     return out.str();
}

bool LogStream::ShouldRotate() const
{
     if (ConfigValue.method != "file" || !FileStream)
     {
          return false;
     }

     std::error_code ec;
     const auto size = fs::exists(ConfigValue.target, ec) ? fs::file_size(ConfigValue.target, ec) : 0;
     if (ConfigValue.max_size > 0 && size >= ConfigValue.max_size)
     {
          return true;
     }

     if (ConfigValue.rotation_interval == 0)
     {
          return false;
     }

     const std::time_t now = std::time(nullptr);
     if (ConfigValue.rotation_interval == -1)
     {
          return now - LastRotationTime >= 24 * 60 * 60;
     }

     if (ConfigValue.rotation_interval == -2)
     {
          return now - LastRotationTime >= 7 * 24 * 60 * 60;
     }

     return now - LastRotationTime >= ConfigValue.rotation_interval;
}

std::string LogStream::GenerateRotatedFilename() const
{
     return ConfigValue.target + "." + GetTimestamp("%Y%m%d%H%M%S");
}

void LogStream::CleanupOldRotatedFiles()
{
     if (ConfigValue.method != "file" || ConfigValue.target.empty())
     {
          return;
     }

     fs::path target(ConfigValue.target);
     const std::string prefix = target.filename().string() + ".";

     std::vector<fs::directory_entry> rotated_files;
     std::error_code ec;

     if (!fs::exists(target.parent_path(), ec))
     {
          return;
     }

     for (const auto& entry : fs::directory_iterator(target.parent_path(), ec))
     {
          if (ec || !entry.is_regular_file())
          {
               continue;
          }

          const std::string name = entry.path().filename().string();
          if (name.rfind(prefix, 0) == 0)
          {
               rotated_files.push_back(entry);
          }
     }

     std::sort(rotated_files.begin(), rotated_files.end(), [](const fs::directory_entry& left, const fs::directory_entry& right) {
          std::error_code left_ec;
          std::error_code right_ec;

          return left.last_write_time(left_ec) > right.last_write_time(right_ec);
     });

     if (ConfigValue.max_rotated_files > 0 && rotated_files.size() > ConfigValue.max_rotated_files)
     {
          for (size_t i = ConfigValue.max_rotated_files; i < rotated_files.size(); ++i)
          {
               fs::remove(rotated_files[i].path(), ec);
          }
     }

     if (ConfigValue.max_age_days == 0)
     {
          return;
     }

     const auto now = fs::file_time_type::clock::now();
     const auto max_age = std::chrono::hours(24 * static_cast<int>(ConfigValue.max_age_days));

     for (const auto& entry : rotated_files)
     {
          std::error_code time_ec;
          const auto last_write_time = entry.last_write_time(time_ec);
          if (time_ec)
          {
               continue;
          }

          if (now - last_write_time > max_age)
          {
               fs::remove(entry.path(), ec);
          }
     }
}

void LogStream::RotateIfNeeded()
{
     if (!ShouldRotate())
     {
          return;
     }

     if (FileStream && FileStream->is_open())
     {
          FileStream->close();
     }

     const std::string rotated = GenerateRotatedFilename();
     std::error_code ec;
     fs::rename(ConfigValue.target, rotated, ec);
     LastRotationTime = std::time(nullptr);
     CleanupOldRotatedFiles();
     OpenFile();
}

void LogStream::WriteLog(LogLevel level, const std::string& type, const std::string& message)
{
     std::lock_guard<std::mutex> lock(WriteMutex);
     const std::string line = FormatLogLine(level, type, message);

     if (ConfigValue.method == "console")
     {
          if (level == LogLevel::LOG_CRITICAL)
          {
               std::cerr << line << std::endl;
          }
          else
          {
               std::cout << line << std::endl;
          }
          return;
     }

     RotateIfNeeded();
     if (!FileStream || !FileStream->is_open())
     {
          OpenFile();
     }

     if (FileStream && FileStream->is_open())
     {
          (*FileStream) << line << std::endl;
          FileStream->flush();
     }
}

void LogStream::Flush()
{
     std::lock_guard<std::mutex> lock(WriteMutex);
     if (FileStream && FileStream->is_open())
     {
          FileStream->flush();
     }
}

LogManager::LogManager() = default;
LogManager::~LogManager() = default;

std::unique_ptr<LogManager> LogManager::CreateAndInitialize(ServerConfig* config)
{
     auto logs = std::make_unique<LogManager>();
     if (!config)
     {
          return logs;
     }

     logs->Initialize(config->GetLogConfigs(), config->GetDebugMode(), config->GetNoForkMode(), config->GetVerboseMode());
     return logs;
}

bool LogManager::Initialize(const std::vector<LogConfig>& log_configs, bool debug, bool nofork, bool verbose)
{
     std::lock_guard<std::mutex> lock(ManagerMutex);
     LogStreams.clear();
     LogConfigs = log_configs;
     DebugMode = debug;
     NoForkMode = nofork;
     VerboseMode = verbose;

     for (const auto& config : LogConfigs)
     {
          LogStreams.push_back(std::make_unique<LogStream>(config));
     }

     Initialized = true;
     return true;
}

bool LogManager::ShouldLog(const LogConfig& config, LogLevel level, const std::string& type) const
{
     if (static_cast<int>(level) > static_cast<int>(config.level))
     {
          return false;
     }

     return config.type == "*" || config.type == type;
}

void LogManager::Log(LogLevel level, const std::string& type, const std::string& message)
{
     std::lock_guard<std::mutex> lock(ManagerMutex);
     if (!Initialized)
     {
          return;
     }

     for (size_t i = 0; i < LogStreams.size() && i < LogConfigs.size(); ++i)
     {
          if (!ShouldLog(LogConfigs[i], level, type))
          {
               continue;
          }

          LogStreams[i]->WriteLog(level, type, message);
     }
}

void LogManager::Critical(const std::string& type, const std::string& message)
{
     Log(LogLevel::LOG_CRITICAL, type, message);
}

void LogManager::Sparse(const std::string& type, const std::string& message)
{
     Log(LogLevel::LOG_SPARSE, type, message);
}

void LogManager::Normal(const std::string& type, const std::string& message)
{
     Log(LogLevel::LOG_NORMAL, type, message);
}

void LogManager::Verbose(const std::string& type, const std::string& message)
{
     Log(LogLevel::LOG_VERBOSE, type, message);
}

void LogManager::Debug(const std::string& type, const std::string& message)
{
     if (!DebugMode)
     {
          return;
     }

     Log(LogLevel::LOG_DEBUG, type, message);
}

void LogManager::SafeLog(LogManager* self, LogLevel level, const std::string& type, const std::string& message)
{
     if (!self)
     {
          return;
     }

     self->Log(level, type, message);
}

bool LogManager::CreateLogsDirectory(const std::string& path)
{
     std::error_code ec;
     return fs::create_directories(path, ec) || fs::exists(path, ec);
}

LogLevel LogManager::StringToLogLevel(const std::string& level_str)
{
     std::string value = level_str;
     std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
     });

     if (value == "critical")
     {
          return LogLevel::LOG_CRITICAL;
     }
     if (value == "sparse")
     {
          return LogLevel::LOG_SPARSE;
     }
     if (value == "verbose")
     {
          return LogLevel::LOG_VERBOSE;
     }
     if (value == "debug")
     {
          return LogLevel::LOG_DEBUG;
     }
     if (value == "none")
     {
          return LogLevel::LOG_NONE;
     }

     return LogLevel::LOG_NORMAL;
}

void LogManager::FlushAll()
{
     std::lock_guard<std::mutex> lock(ManagerMutex);
     for (auto& stream : LogStreams)
     {
          stream->Flush();
     }
}

size_t LogManager::GetLogCount() const
{
     std::lock_guard<std::mutex> lock(ManagerMutex);
     return LogStreams.size();
}

bool LogManager::GetDebugMode() const
{
     return DebugMode;
}

void LogManager::ResetAfterFork()
{
     std::lock_guard<std::mutex> lock(ManagerMutex);

     for (auto& stream : LogStreams)
     {
          stream->Reopen();
     }
}
