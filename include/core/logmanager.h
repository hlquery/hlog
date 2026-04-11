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

#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/config.h"

class ServerConfig;

/* Supported runtime log verbosity levels. */

enum class LogLevel
{
     LOG_NONE = 0,
     LOG_CRITICAL = 1,
     LOG_SPARSE = 2,
     LOG_NORMAL = 3,
     LOG_VERBOSE = 4,
     LOG_DEBUG = 5
};

/* One configured output target for the standalone logger. */

struct LogConfig
{
     /* Output backend name such as console or file. */

     std::string method;

     /* Logical source tag written into each line. */

     std::string type;

     /* Minimum accepted log level for this stream. */

     LogLevel level = LogLevel::LOG_NORMAL;

     /* Destination path for file-backed logging. */

     std::string target;

     /* Rotate once the file reaches this size in bytes. */

     size_t max_size = 0;

     /* Rotation cadence in seconds or sentinel values. */

     int rotation_interval = 0;

     /* Maximum number of rotated files to retain. */

     size_t max_rotated_files = 10;

     /* Maximum age in days for rotated files. */

     size_t max_age_days = 0;
};

/* One live log stream that writes to console or a file. */

class CoreExport LogStream
{
   public:

     /* Construct one runtime stream from parsed log config. */

     explicit LogStream(const LogConfig& config);

     /* Flush any pending output before destruction. */

     ~LogStream();

     /* Return whether the stream is currently writable. */

     bool IsOpen() const;

     /* Write one formatted message to the stream. */

     void WriteLog(LogLevel level, const std::string& type, const std::string& message);

     /* Flush buffered output to the underlying target. */

     void Flush();

     /* Reopen the underlying file after fork or rotation. */

     void Reopen();

   private:

     /* Decide whether file rotation should happen now. */

     bool ShouldRotate() const;

     /* Rotate the file when size or time thresholds fire. */

     void RotateIfNeeded();

     /* Open the configured file destination. */

     void OpenFile();

     /* Remove old rotated files past retention limits. */

     void CleanupOldRotatedFiles();

     /* Build the rotated filename suffix for one archive. */

     std::string GenerateRotatedFilename() const;

     /* Format one line using the shared hlog log shape. */

     std::string FormatLogLine(LogLevel level, const std::string& type, const std::string& message) const;

     /* Map an internal level to the short visible tag. */

     std::string LevelToTag(LogLevel level) const;

     /* Static config copied into this stream instance. */

     LogConfig ConfigValue;

     /* File stream when method=file. */

     std::unique_ptr<std::ofstream> FileStream;

     /* Last rotation timestamp used for interval checks. */

     std::time_t LastRotationTime = 0;

     /* Serialize writes and reopen operations. */

     mutable std::mutex WriteMutex;
};

/* Standalone log manager used by the hlog runtime and modules. */

class CoreExport LogManager
{
   public:

     /* Construct an empty manager before Initialize(). */

     LogManager();

     /* Flush and release all configured streams. */

     ~LogManager();

     /* Build and initialize one manager from server config. */

     static std::unique_ptr<LogManager> CreateAndInitialize(ServerConfig* config);

     /* Initialize all configured log outputs and flags. */

     bool Initialize(const std::vector<LogConfig>& log_configs, bool debug = false, bool nofork = false, bool verbose = false);

     /* Dispatch one message to every eligible stream. */

     void Log(LogLevel level, const std::string& type, const std::string& message);

     /* Write one critical runtime message. */

     void Critical(const std::string& type, const std::string& message);

     /* Write one sparse runtime message. */

     void Sparse(const std::string& type, const std::string& message);

     /* Write one normal runtime message. */

     void Normal(const std::string& type, const std::string& message);

     /* Write one verbose runtime message. */

     void Verbose(const std::string& type, const std::string& message);

     /* Write one debug runtime message. */

     void Debug(const std::string& type, const std::string& message);

     /* Safe wrapper for logging through a nullable manager. */

     static void SafeLog(LogManager* self, LogLevel level, const std::string& type, const std::string& message);

     /* Ensure the runtime logs directory exists. */

     static bool CreateLogsDirectory(const std::string& path = HLQUERY_LOG_DIR);

     /* Parse one string level name into the enum. */

     static LogLevel StringToLogLevel(const std::string& level_str);

     /* Flush every configured stream. */

     void FlushAll();

     /* Return the number of active configured streams. */

     size_t GetLogCount() const;

     /* Return whether debug logging is enabled globally. */

     bool GetDebugMode() const;

     /* Reopen file streams after daemon fork boundaries. */

     void ResetAfterFork();

   private:

     /* Decide whether one stream should receive this message. */

     bool ShouldLog(const LogConfig& config, LogLevel level, const std::string& type) const;

     /* Active runtime stream instances. */

     std::vector<std::unique_ptr<LogStream>> LogStreams;

     /* Stored config matching the active streams. */

     std::vector<LogConfig> LogConfigs;

     /* Serialize manager-wide stream access. */

     mutable std::mutex ManagerMutex;

     /* Whether Initialize() completed successfully. */

     bool Initialized = false;

     /* Whether debug mode is enabled. */

     bool DebugMode = false;

     /* Whether the runtime is in nofork mode. */

     bool NoForkMode = false;

     /* Whether verbose mode is enabled. */

     bool VerboseMode = false;
};
