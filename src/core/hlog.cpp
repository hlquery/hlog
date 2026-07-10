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

#include <iostream>

/* Global standalone hlog instance. */

hlcore *Instance = nullptr;

/* Entry point for the standalone hlog application. */

int main(int argc, char** argv)
{
     hlcore* core = nullptr;

     try
     {
          core = new hlcore(argc, argv);
          core->Run();
          delete core;
          Instance = nullptr;
          return 0;
     }
     catch (const std::exception& ex)
     {
          if (core && core->Logs)
          {
               core->Logs->Critical("startup", ex.what());
          }
          else
          {
               std::cerr << "hlog: " << ex.what() << std::endl;
          }

          delete core;
          Instance = nullptr;
          return 1;
     }
     catch (...)
     {
          if (core && core->Logs)
          {
               core->Logs->Critical("startup", "unknown exception");
          }
          else
          {
               std::cerr << "hlog: unknown exception" << std::endl;
          }

          delete core;
          Instance = nullptr;
          return 1;
     }
}

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
