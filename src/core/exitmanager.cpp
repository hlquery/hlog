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

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <vector>

/* Return the registered cleanup callback list. */

std::vector<void (*)()>& GetCleanups()
{
     static std::vector<void (*)()> cleanups;

     return cleanups;
}

/* Return the cleanup list mutex. */

std::mutex& GetCleanupMutex()
{
     static std::mutex mutex;

     return mutex;
}

/* Return the shared shutdown state flag. */

std::atomic<bool>& GetShuttingDown()
{
     static std::atomic<bool> shutting_down{false};

     return shutting_down;
}

/* Register a cleanup callback. */

void ExitManager::RegisterCleanup(void (*func)())
{
     if (!func)
     {
          return;
     }

     std::lock_guard<std::mutex> lock(GetCleanupMutex());
     GetCleanups().push_back(func);
}

/* Run cleanup callbacks in reverse registration order. */

void ExitManager::RunCleanups()
{
     std::vector<void (*)()> cleanups;

     {
          std::lock_guard<std::mutex> lock(GetCleanupMutex());
          cleanups = GetCleanups();
     }

     for (auto it = cleanups.rbegin(); it != cleanups.rend(); ++it)
     {
          (*it)();
     }
}

/* Return true when shutdown has started. */

bool ExitManager::IsShuttingDown()
{
     return GetShuttingDown().load();
}

/* Exit after cleanup handlers complete. */

[[noreturn]] void ExitManager::Exit(int status)
{
     GetShuttingDown().store(true);
     RunCleanups();
     std::exit(status);
}

/* Exit immediately without cleanup handlers. */

[[noreturn]] void ExitManager::QuickExit(int status)
{
     GetShuttingDown().store(true);
     std::quick_exit(status);
}

/* Exit through the emergency path. */

[[noreturn]] void ExitManager::EmergencyExit(int status)
{
     std::_Exit(status);
}
