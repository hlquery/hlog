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

#include <string>
#include <vector>

#include "common/options.h"
#include "core/config.h"
#include "core/logmanager.h"

/* Stored command-line pointer pair for later argument parsing. */

struct CommandLine
{
     /* Original argc value passed into main(). */

     int argc = 0;

     /* Original argv array passed into main(). */

     char** argv = nullptr;
};

/* Minimal standalone config holder used by hlog. */

class ServerConfig
{
   public:

     /* Capture startup arguments for later option parsing. */

     ServerConfig(int argc = 0, char** argv = nullptr);

     /* Default destructor. */

     ~ServerConfig();

     /* Load the standalone log config file. */

     bool LoadConfig(const std::string& config_file = HLQUERY_CONFIG_DIR "/hlquery.conf");

     /* Return whether the last load completed successfully. */

     bool IsValid() const
     {
          return Valid;
     }

     /* Return the most recent config load error string. */

     const std::string& GetError() const
     {
          return ErrorMsg;
     }

     /* Return the parsed log outputs from the config file. */

     const std::vector<LogConfig>& GetLogConfigs() const
     {
          return LogConfigs;
     }

     /* Return the stored original command line. */

     const CommandLine& GetCommandLine() const
     {
          return CmdLine;
     }

     /* Store parsed standalone startup options. */

     void SetOptions(const Options& options)
     {
          ParsedOptions = options;
     }

     /* Parse stored startup arguments into standalone options. */

     void LoadCommandLineOptions();

     /* Return parsed standalone startup options. */

     const Options& GetOptions() const
     {
          return ParsedOptions;
     }

     /* Record whether the runtime is staying in foreground mode. */

     void SetNoForkMode(bool enabled)
     {
          NoForkMode = enabled;
     }

     /* Return whether nofork mode is enabled. */

     bool GetNoForkMode() const
     {
          return NoForkMode;
     }

     /* hlog currently has no config-backed debug mode. */

     bool GetDebugMode() const
     {
          return false;
     }

     /* hlog currently has no config-backed verbose mode. */

     bool GetVerboseMode() const
     {
          return false;
     }

     /* Override the config file path before loading. */

     void SetConfigFile(const std::string& config_file)
     {
          ConfigFile = config_file;
     }

   private:

     /* Stored startup command line. */

     CommandLine CmdLine;

     /* Whether the last load completed successfully. */

     bool Valid = false;

     /* Whether the runtime should stay in foreground mode. */

     bool NoForkMode = false;

     /* Most recent config loading error text. */

     std::string ErrorMsg;

     /* Resolved config file path. */

     std::string ConfigFile;

     /* Parsed output log definitions from the config file. */

     std::vector<LogConfig> LogConfigs;

     /* Parsed standalone startup options. */

     Options ParsedOptions;
};
