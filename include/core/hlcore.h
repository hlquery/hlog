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

#include <chrono>
#include <cstdint>
#include <ctime>
#include <memory>

#include "core/config.h"
#include "core/forwards.h"
#include "core/logmanager.h"
#include "core/types.h"
#include "core/serverconfig.h"

/* Global standalone application instance */

CoreExport extern hlcore* Instance;

/* Minimal standalone core runtime for hlog */

class CoreExport hlcore
{
   public:

     /* Constructor */

     hlcore(int argc, char** argv);

     /* Destructor */

     ~hlcore();

     /* Run the standalone pipeline lifecycle */

     void Run();

     /* Release runtime-owned resources */

     void Cleanup();

     /* Shared server configuration */

     std::unique_ptr<ServerConfig> Config;

     /* Active log manager */

     std::unique_ptr<LogManager> Logs;

     /* Runtime pipeline instance with owned resolved config */

     std::unique_ptr<Pipeline> HLogPipeline;

     /* Effective watch mode after auto-resolution */

     WatchMode HLogEffectiveMode = WatchMode::Auto;

     /* Returns the current time using system clock */

     time_t Time() const;

     /* Returns milliseconds since epoch */

     long long NowMs() const;

     /* Returns current time point from steady clock */

     std::chrono::steady_clock::time_point Now() const;

     /* Initialize console logging for standalone execution */

     void InitializeLogs();

     /* Load and normalize pipeline configuration */

     void ResolvePipelineConfig();

     /* Choose the effective watch backend */

     void ResolveEffectiveWatchMode();

     /* Emit startup diagnostics for resolved state */

     void EmitStartupLogs() const;
};
