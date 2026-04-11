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

#include "core/config.h"

/* Coordinates standalone shutdown and cleanup handlers. */

class CoreExport ExitManager
{
   public:

     /* Register a cleanup callback. */

     static void RegisterCleanup(void (*func)());

     /* Run registered cleanup callbacks in reverse order. */

     static void RunCleanups();

     /* Return true when shutdown is in progress. */

     static bool IsShuttingDown();

     /* Exit after running cleanup handlers. */

     [[noreturn]] static void Exit(int status = 0);

     /* Exit immediately without running cleanup handlers. */

     [[noreturn]] static void QuickExit(int status = 1);

     /* Exit immediately using the async-signal-safe path. */

     [[noreturn]] static void EmergencyExit(int status = 1);
};
