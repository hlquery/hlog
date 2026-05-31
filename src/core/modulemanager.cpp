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

#include "core/modulemanager.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "core/hlcore.h"
#include "core/logmanager.h"


/* Send loader messages to the runtime logger or stderr fallback. */

LogManager* GetModuleLogger()
{
     return (Instance && Instance->Logs) ? Instance->Logs.get() : nullptr;
}

void LogModuleMessage(const std::string& level, const std::string& message)
{
     LogManager* logs = GetModuleLogger();
     if (!logs)
     {
          std::cerr << message << std::endl;
          return;
     }

     if (level == "critical")
     {
          logs->Critical("modules", message);
          return;
     }

     logs->Normal("modules", message);
}

/* Resolve a module name into the first available runtime or build artifact. */

std::filesystem::path ResolveModulePath(const ModuleConfig& config)
{
     if (!config.Path.empty())
     {
          return std::filesystem::path(config.Path).lexically_normal();
     }

     const std::string primarySuffix = HLOG_MODULE_SUFFIX;
     std::vector<std::string> suffixes = {primarySuffix};
#if defined(__APPLE__)
     if (primarySuffix != ".so")
     {
          suffixes.push_back(".so");
     }
#endif

     std::vector<std::filesystem::path> candidates;
     candidates.reserve(suffixes.size() * 4);
     for (const auto& suffix : suffixes)
     {
          candidates.push_back(std::filesystem::path(HLOG_BASE_DIR) / "run/modules" / (config.Name + suffix));
          candidates.push_back(std::filesystem::path(HLOG_BASE_DIR) / "run/modules" / ("m_" + config.Name + suffix));
          candidates.push_back(std::filesystem::path(HLOG_BASE_DIR) / "build/modules" / (config.Name + suffix));
          candidates.push_back(std::filesystem::path(HLOG_BASE_DIR) / "build/modules" / ("m_" + config.Name + suffix));
     }

     std::error_code ec;
     for (const auto& candidate : candidates)
     {
          if (std::filesystem::exists(candidate, ec))
          {
               return candidate.lexically_normal();
          }
     }

     return candidates.front().lexically_normal();
}

#ifdef _WIN32

/* Translate a Win32 dynamic-loader error into readable text. */

std::string GetWindowsLoaderError()
{
     const DWORD errorCode = GetLastError();

     if (errorCode == 0)
     {
          return "unknown error";
     }

     LPSTR buffer = nullptr;
     const DWORD length = FormatMessageA(
          FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr,
          errorCode,
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          reinterpret_cast<LPSTR>(&buffer),
          0,
          nullptr);

     std::string message = "Win32 error " + std::to_string(errorCode);

     if (length > 0 && buffer)
     {
          message += ": ";
          message.append(buffer, length);

          while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
          {
               message.pop_back();
          }
     }

     if (buffer)
     {
          LocalFree(buffer);
     }

     return message;
}
#endif


/* Ensure all modules are unloaded when the module type is destroyed. */

HLogModule::~HLogModule() = default;

HLogModuleManager::HLogModuleManager() = default;

HLogModuleManager::~HLogModuleManager()
{
     UnloadAll();
}

/* Load, start, and stage every enabled module before committing them. */

bool HLogModuleManager::LoadModules(const PipelineConfig& config, std::string& errorMessage)
{
     UnloadAll();

     std::vector<LoadedModule> stagedModules;

     auto rollback = [&]()
     {
          for (auto it = stagedModules.rbegin(); it != stagedModules.rend(); ++it)
          {
               if (it->Instance)
               {
                    try
                    {
                         it->Instance->Stop();
                    }
                    catch (...)
                    {
                    }
                    it->Instance.reset();
               }

               if (it->Handle)
               {
#ifdef _WIN32
                    FreeLibrary(static_cast<HMODULE>(it->Handle));
#else
                    dlclose(it->Handle);
#endif
               }
          }

          stagedModules.clear();
     };

     for (const auto& moduleConfig : config.Modules)
     {
          if (!moduleConfig.Enabled || moduleConfig.Name.empty())
          {
               continue;
          }

          const std::filesystem::path modulePath = ResolveModulePath(moduleConfig);
          std::error_code ec;
          if (!std::filesystem::exists(modulePath, ec))
          {
               errorMessage = "Configured module '" + moduleConfig.Name + "' could not be found: " + modulePath.string();
               rollback();
               return false;
          }

          LogModuleMessage("normal", "Loading module '" + moduleConfig.Name + "' from " + modulePath.string() + ".");

          void* handle = nullptr;
          CreateHLogModuleFn createFn = nullptr;

#ifdef _WIN32
          handle = reinterpret_cast<void*>(LoadLibraryA(modulePath.string().c_str()));
          if (!handle)
          {
               errorMessage = "Failed to load module '" + moduleConfig.Name + "' from " + modulePath.string() + ": " + GetWindowsLoaderError();
               rollback();
               return false;
          }

          createFn = reinterpret_cast<CreateHLogModuleFn>(GetProcAddress(static_cast<HMODULE>(handle), "CreateHLogModule"));
          if (!createFn)
          {
               FreeLibrary(static_cast<HMODULE>(handle));
               errorMessage = "Module '" + moduleConfig.Name + "' is missing CreateHLogModule(): " + GetWindowsLoaderError();
               rollback();
               return false;
          }
#else
          handle = dlopen(modulePath.c_str(), RTLD_NOW | RTLD_LOCAL);
          if (!handle)
          {
               errorMessage = "Failed to load module '" + moduleConfig.Name + "' from " + modulePath.string() + ": " + std::string(dlerror());
               rollback();
               return false;
          }

          dlerror();
          createFn = reinterpret_cast<CreateHLogModuleFn>(dlsym(handle, "CreateHLogModule"));
          const char* symbolError = dlerror();
          if (symbolError)
          {
               dlclose(handle);
               errorMessage = "Module '" + moduleConfig.Name + "' is missing CreateHLogModule(): " + std::string(symbolError);
               rollback();
               return false;
          }
#endif

          std::shared_ptr<HLogModule> module(createFn());
          if (!module)
          {
#ifdef _WIN32
               FreeLibrary(static_cast<HMODULE>(handle));
#else
               dlclose(handle);
#endif
               errorMessage = "Module '" + moduleConfig.Name + "' returned a null module instance.";
               rollback();
               return false;
          }

          std::string startError;
          bool started = false;
          try
          {
               started = module->Start(moduleConfig, startError);
          }
          catch (const std::exception& ex)
          {
               startError = ex.what();
               started = false;
          }
          catch (...)
          {
               startError = "unknown exception";
               started = false;
          }

          if (!started)
          {
               try
               {
                    module->Stop();
               }
               catch (...)
               {
               }

#ifdef _WIN32
               FreeLibrary(static_cast<HMODULE>(handle));
#else
               dlclose(handle);
#endif
               errorMessage = "Module '" + moduleConfig.Name + "' Start() failed" +
                              std::string(startError.empty() ? "." : ": " + startError);
               rollback();
               return false;
          }

          stagedModules.push_back(LoadedModule{
               module->GetName().empty() ? moduleConfig.Name : module->GetName(),
               modulePath.string(),
               handle,
               std::move(module),
          });
     }

     Modules = std::move(stagedModules);
     return true;
}

/* Stop modules in reverse order and release their shared-library handles. */

void HLogModuleManager::UnloadAll()
{
     for (auto it = Modules.rbegin(); it != Modules.rend(); ++it)
     {
          if (it->Instance)
          {
               try
               {
                    it->Instance->Stop();
               }
               catch (const std::exception& ex)
               {
                    LogModuleMessage("critical", "Module '" + it->Name + "' threw during Stop(): " + ex.what());
               }
               catch (...)
               {
                    LogModuleMessage("critical", "Module '" + it->Name + "' threw during Stop(): unknown exception");
               }
               it->Instance.reset();
          }

          if (it->Handle)
          {
#ifdef _WIN32
               FreeLibrary(static_cast<HMODULE>(it->Handle));
#else
               dlclose(it->Handle);
#endif
               it->Handle = nullptr;
          }
     }

     Modules.clear();
}

/* Run the event pipeline through every loaded filter module. */

void HLogModuleManager::ProcessEvent(PipelineEvent& event, const FileState& state) const
{
     for (const auto& module : Modules)
     {
          if (!module.Instance)
          {
               continue;
          }

          try
          {
               module.Instance->ProcessEvent(event, state);
          }
          catch (const std::exception& ex)
          {
               LogModuleMessage("critical", "Module '" + module.Name + "' threw during ProcessEvent(): " + ex.what());
          }
          catch (...)
          {
               LogModuleMessage("critical", "Module '" + module.Name + "' threw during ProcessEvent(): unknown exception");
          }

          if (event.Dropped)
          {
               return;
          }
     }
}

bool HLogModuleManager::Empty() const
{
     return Modules.empty();
}

bool HLogModuleManager::HasSourceModule() const
{
     return std::any_of(Modules.begin(), Modules.end(), [](const LoadedModule& module) {
          return module.Instance && module.Instance->IsSourceModule();
     });
}

bool HLogModuleManager::RunSourceModule(const Pipeline& pipeline, WatchMode mode, int intervalMs, std::string& errorMessage) const
{
     const LoadedModule* sourceModule = nullptr;

     for (const auto& module : Modules)
     {
          if (!module.Instance || !module.Instance->IsSourceModule())
          {
               continue;
          }

          if (sourceModule)
          {
               errorMessage = "Multiple hlog source modules are configured. Load only one source module at a time.";
               return false;
          }

          sourceModule = &module;
     }

     if (!sourceModule)
     {
          return true;
     }

     try
     {
          return sourceModule->Instance->Run(pipeline, mode, intervalMs, errorMessage);
     }
     catch (const std::exception& ex)
     {
          errorMessage = "Source module '" + sourceModule->Name + "' threw during Run(): " + ex.what();
          return false;
     }
     catch (...)
     {
          errorMessage = "Source module '" + sourceModule->Name + "' threw during Run(): unknown exception";
          return false;
     }
}
