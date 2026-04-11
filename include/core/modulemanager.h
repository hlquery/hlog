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

#include <memory>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/forwards.h"
#include "core/modules.h"
#include "core/types.h"

/* Runtime loader and dispatcher for standalone hlog modules. */

class HLogModuleManager
{
   public:

     /* Construct an empty module manager. */

     HLogModuleManager();

     /* Stop and unload any active modules on destruction. */

     ~HLogModuleManager();

     /* Load and start every enabled module from pipeline config. */

     bool LoadModules(const PipelineConfig& config, LogManager* logs, std::string& errorMessage);

     /* Stop all loaded modules and release their shared libraries. */

     void UnloadAll(LogManager* logs);

     /* Send one pipeline event through all loaded filter modules. */

     void ProcessEvent(PipelineEvent& event, const FileState& state, LogManager* logs) const;

     /* Return whether no modules are currently loaded. */

     bool Empty() const;

     /* Return whether a loaded module owns the source loop. */

     bool HasSourceModule() const;

     /* Run the configured source module when one is present. */

     bool RunSourceModule(const Pipeline& pipeline, WatchMode mode, int intervalMs, LogManager* logs, std::string& errorMessage) const;

   private:

     /* One loaded shared object plus its live module instance. */

     struct LoadedModule
     {
          /* Logical module name. */

          std::string Name;

          /* Resolved runtime path for the shared library. */

          std::string Path;

          /* Native shared-library handle. */

          void* Handle = nullptr;

          /* Live module object created from the shared library. */

          std::shared_ptr<HLogModule> Instance;
     };

     /* Ordered list of loaded modules. */

     std::vector<LoadedModule> Modules;
};
