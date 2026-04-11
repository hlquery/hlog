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

#include "core/exitmanager.h"
#include "core/hlcore.h"
#include "core/pipeline.h"
#include "core/pipeline.h"
#include "core/runtime.h"

/* Global standalone hlog instance. */

hlcore *Instance = nullptr;

/* Construct the standalone hlog core. */

hlcore::hlcore(int argc, char** argv)
{
     Instance = this;

     /* Mirror hlquery ownership by constructing server config first. */

     Config = std::make_unique<ServerConfig>(argc, argv);

     /* Parse wrapper and direct CLI overrides before runtime startup. */

     if (Config)
     {
          Config->LoadCommandLineOptions();
     }

     /* Bring up logging and resolve the effective pipeline state. */

     InitializeLogs();
     ResolvePipelineConfig();
     ResolveEffectiveWatchMode();

     /* Register one cleanup hook for normal exit and signal-driven teardown. */

     ExitManager::RegisterCleanup([]()
     {
          if (Instance)
          {
               Instance->Cleanup();
          }
     });
}

/* Entry point for the standalone hlog application. */

int main(int argc, char** argv)
{
     new hlcore(argc, argv);
     Instance->Run();
     delete Instance;
     Instance = nullptr;

     return 0;
}
