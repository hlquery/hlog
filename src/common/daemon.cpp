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

#include "core/hlcore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "core/pipeline.h"
#include "core/pipeline.h"

/* Join explicit file inputs into one comma-separated module path string. */

static std::string JoinPaths(const std::vector<HLogInputFile>& inputs)
{
     std::ostringstream joined;

     for (size_t i = 0; i < inputs.size(); ++i)
     {
          if (i != 0)
          {
               joined << ",";
          }

          joined << std::filesystem::absolute(inputs[i].PathValue).string();
     }

     return joined.str();
}

/* Detect whether the wrapper asked the process to stay attached. */

static bool IsForegroundMode()
{
     const char* foreground = std::getenv("HLOG_FOREGROUND");

     return foreground && std::string(foreground) == "1";
}

/* Detect whether the wrapper started the process in daemon mode. */

static bool IsDaemonMode()
{
     const char* daemon = std::getenv("HLOG_DAEMON");

     return daemon && std::string(daemon) == "1";
}

/* Remove the daemon pidfile after a clean runtime shutdown. */

static void RemovePidFile()
{
     const char* pidfile = std::getenv("HLOG_PIDFILE");

     if (!pidfile || !*pidfile)
     {
          return;
     }

     std::error_code error;

     std::filesystem::remove(pidfile, error);
}

/* Count the effective input files for config-test diagnostics. */

static size_t CountResolvedInputs(const PipelineConfig& config, const Options& options)
{
     if (!config.Inputs.empty())
     {
          return config.Inputs.size();
     }

     for (const auto& module : config.Modules)
     {
          if (module.Name != "filein")
          {
               continue;
          }

          const auto pathIt = module.Attributes.find("path");
          if (pathIt != module.Attributes.end() && !pathIt->second.empty())
          {
               size_t count = 1;
               for (char ch : pathIt->second)
               {
                    if (ch == ',')
                    {
                         ++count;
                    }
               }
               return count;
          }

          try
          {
               return ResolveFilesFromHlqueryConfig(options.ConfigPath).size();
          }
          catch (...)
          {
               return 0;
          }
     }

     return 0;
}

/* Convert explicit command-line files into the filein source module config. */

static void MaterializeFileInputModule(PipelineConfig& config)
{
     auto existing = std::find_if(config.Modules.begin(), config.Modules.end(), [](const ModuleConfig& module) {
          return module.Name == "filein";
     });

     if (!config.Inputs.empty())
     {
          ModuleConfig materialized;
          materialized.Name = "filein";
          materialized.Attributes["path"] = JoinPaths(config.Inputs);
          materialized.Attributes["start_position"] =
               config.Inputs.front().Start == StartPosition::Beginning ? "beginning" : "end";

          if (existing == config.Modules.end())
          {
               config.Modules.push_back(std::move(materialized));
          }
          else
          {
               existing->Attributes["path"] = materialized.Attributes["path"];
               existing->Attributes["start_position"] = materialized.Attributes["start_position"];
          }

          return;
     }

     (void)existing;
}

/* Destroy the standalone runtime and release owned resources. */

hlcore::~hlcore()
{
     Cleanup();
}

/* Release pipeline state and daemon bookkeeping in shutdown order. */

void hlcore::Cleanup()
{
     HLogPipeline.reset();
     Logs.reset();
     Config.reset();

     if (IsDaemonMode() && !IsForegroundMode())
     {
          RemovePidFile();
     }
}

/* Run config-test mode or the live pipeline source loop. */

void hlcore::Run()
{
     const Options& options = Config->GetOptions();

     if (options.TestConfig)
     {
          if (!options.Quiet)
          {
               Logs->Normal("config_test", "hlog_config=" + std::filesystem::absolute(options.HLogConfigPath).string() + ".");
               Logs->Normal("config_test", "hlquery_config=" + std::filesystem::absolute(options.ConfigPath).string() + ".");
               Logs->Normal("config_test", "resolved_inputs=" + std::to_string(CountResolvedInputs(HLogPipeline->GetConfig(), options)) + ".");
          }

          Logs->Normal("config_test", "Configuration parsed successfully.");
          return;
     }

     EmitStartupLogs();

     std::string sourceError;
     if (HLogPipeline->HasSourceModule() &&
         !HLogPipeline->RunSourceModule(HLogEffectiveMode,
                                        HLogPipeline->GetConfig().PollIntervalMs,
                                        sourceError))
     {
          throw std::runtime_error(sourceError.empty() ? "hlog source module failed." : sourceError);
     }

     Logs->Normal("pipeline", "Shutting down.");
}

/* Return the current wall-clock time in whole seconds. */

time_t hlcore::Time() const
{
     return ::Time();
}

/* Return the current wall-clock time in milliseconds. */

long long hlcore::NowMs() const
{
     return ::NowMs();
}

/* Return a steady clock point for interval measurements. */

std::chrono::steady_clock::time_point hlcore::Now() const
{
     return ::Now();
}

/* Bootstrap the default console logger used by standalone hlog. */

void hlcore::InitializeLogs()
{
     Logs = std::make_unique<LogManager>();

     LogConfig consoleConfig;
     consoleConfig.method = "console";
     consoleConfig.type = "*";
     consoleConfig.level = LogLevel::LOG_NORMAL;
     consoleConfig.target = "console";

     Logs->Initialize(std::vector<LogConfig>{consoleConfig}, false, IsForegroundMode(), false);
}

/* Load pipeline config, validate server config, and apply CLI overrides. */

void hlcore::ResolvePipelineConfig()
{
     const Options& options = Config->GetOptions();

     PipelineConfig pipelineConfig = LoadPipelineConfig(options.HLogConfigPath);

     ValidateHlqueryConfig(options.ConfigPath);

     if (options.OverrideMode.has_value())
     {
          pipelineConfig.Mode = *options.OverrideMode;
     }

     if (options.OverrideIntervalMs.has_value())
     {
          pipelineConfig.PollIntervalMs = *options.OverrideIntervalMs;
     }

     if (!options.ExplicitFiles.empty())
     {
          pipelineConfig.Inputs.clear();

          for (const auto& file : options.ExplicitFiles)
          {
               HLogInputFile input;
               input.PathValue = std::filesystem::absolute(file);
               input.Start = options.OverrideFromStart.value_or(false) ? StartPosition::Beginning : StartPosition::End;
               pipelineConfig.Inputs.push_back(std::move(input));
          }
     }

     if (options.OverrideFromStart.has_value())
     {
          for (auto& input : pipelineConfig.Inputs)
          {
               input.Start = *options.OverrideFromStart ? StartPosition::Beginning : StartPosition::End;
          }
     }

     for (const auto& input : pipelineConfig.Inputs)
     {
          if (input.ModeOverride.has_value())
          {
               pipelineConfig.Mode = *input.ModeOverride;
          }

          if (input.IntervalMsOverride.has_value())
          {
               pipelineConfig.PollIntervalMs = *input.IntervalMsOverride;
          }
     }

     MaterializeFileInputModule(pipelineConfig);
     HLogPipeline = std::make_unique<Pipeline>(std::move(pipelineConfig));
}

/* Resolve auto mode into the actual backend used by this platform. */

void hlcore::ResolveEffectiveWatchMode()
{
     HLogEffectiveMode = HLogPipeline->GetConfig().Mode;

     if (HLogEffectiveMode == WatchMode::Auto)
     {
#ifdef __linux__
          HLogEffectiveMode = WatchMode::Kernel;
#else
          HLogEffectiveMode = WatchMode::Poll;
#endif
     }
}

/* Emit the startup lines consumed by the wrapper summary logic. */

void hlcore::EmitStartupLogs() const
{
     if (!Logs || !Config || Config->GetOptions().Quiet)
     {
          return;
     }

     Logs->Normal("pipeline", "mode=" + WatchModeToString(HLogEffectiveMode) +
          " interval_ms=" + std::to_string(HLogPipeline->GetConfig().PollIntervalMs) + ".");
     Logs->Normal("pipeline", "hlog_config=" + std::filesystem::absolute(Config->GetOptions().HLogConfigPath).string() + ".");

     if (HLogPipeline && HLogPipeline->GetConfig().HlqueryOutput.Enabled)
     {
          Logs->Normal("output_hlquery", "endpoint=" + HLogPipeline->GetConfig().HlqueryOutput.Endpoint +
               " collection=" + HLogPipeline->GetConfig().HlqueryOutput.Collection + ".");
     }
}
