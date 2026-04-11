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

#include "core/config.h"
#include "core/forwards.h"
#include "core/types.h"

class CoreExport HLogModule
{
   public:
     explicit HLogModule(std::string name)
          : Name(std::move(name))
     {
     }

     virtual ~HLogModule();

     const std::string& GetName() const
     {
          return Name;
     }

     virtual bool Start(const ModuleConfig&, std::string&)
     {
          return true;
     }

     virtual void Stop()
     {
     }

     virtual void ProcessEvent(PipelineEvent&, const FileState&)
     {
     }

     virtual bool IsSourceModule() const
     {
          return false;
     }

     virtual bool Run(const Pipeline&, WatchMode, int, std::string&)
     {
          return true;
     }

   private:
     std::string Name;
};

using CreateHLogModuleFn = HLogModule* (*)();

#define MODULE_LOAD(ModuleType)                                     \
     extern "C" CoreExport HLogModule* CreateHLogModule()          \
     {                                                             \
          return new ModuleType();                                 \
     }
