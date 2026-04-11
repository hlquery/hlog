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

#include "core/serverconfig.h"

#include <filesystem>

#include "core/configreader.h"

ServerConfig::ServerConfig(int argc, char** argv)
{
     CmdLine.argc = argc;
     CmdLine.argv = argv;
}

ServerConfig::~ServerConfig() = default;

void ServerConfig::LoadCommandLineOptions()
{
     if (CmdLine.argc <= 0 || CmdLine.argv == nullptr)
     {
          return;
     }

     ParsedOptions = ::ParseArgs(CmdLine.argc, CmdLine.argv);
}

bool ServerConfig::LoadConfig(const std::string& config_file)
{
     ConfigFile = config_file;
     LogConfigs.clear();
     ErrorMsg.clear();
     Valid = false;

     ConfigReader reader;
     if (!reader.LoadFile(ConfigFile))
     {
          ErrorMsg = reader.GetError();
          return false;
     }

     const std::filesystem::path config_dir = std::filesystem::absolute(ConfigFile).parent_path();

     for (const auto& tag : reader.GetTags("log"))
     {
          LogConfig config;
          config.method = tag->GetString("method", "file");
          config.type = tag->GetString("type", "*");
          config.level = LogManager::StringToLogLevel(tag->GetString("level", "normal"));
          config.target = tag->GetString("target", "");
          config.max_size = tag->GetSize("rotate_size", 0);
          config.rotation_interval = 0;
          config.max_rotated_files = tag->GetUnsignedInt("max_rotated_files", 10);
          config.max_age_days = tag->GetUnsignedInt("max_age_days", 0);

          if (config.method == "file" && !config.target.empty())
          {
               std::filesystem::path target_path(config.target);
               if (target_path.is_relative())
               {
                    config.target = (config_dir / target_path).lexically_normal().string();
               }
          }

          LogConfigs.push_back(std::move(config));
     }

     Valid = true;
     return true;
}
