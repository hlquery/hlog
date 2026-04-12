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
#include <ctime>
#include <time.h>

/*
 * Return the current wall-clock time as whole seconds since the Unix epoch.
 * This helper uses `std::chrono::system_clock` and keeps the hlog standalone
 * runtime independent from hlquery's public headers.
 */

inline time_t Time()
{
     try
     {
          const auto now = std::chrono::system_clock::now();
          const auto duration = now.time_since_epoch();
          const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);

          return static_cast<time_t>(seconds.count());
     }
     catch (...)
     {
          return 0;
     }
}

/*
 * Return the current wall-clock time as milliseconds since the Unix epoch.
 */

inline long long NowMs()
{
     try
     {
          const auto now = std::chrono::system_clock::now();
          const auto duration = now.time_since_epoch();
          const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

          return static_cast<long long>(milliseconds.count());
     }
     catch (...)
     {
          return 0;
     }
}

/*
 * Return a monotonic steady-clock time point for interval measurement.
 */

inline std::chrono::steady_clock::time_point Now()
{
     try
     {
          struct timespec ts;

#if defined(__linux__) && defined(CLOCK_BOOTTIME)

          if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0)
          {
               const auto duration =
                    std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);

               return std::chrono::steady_clock::time_point(
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration));
          }

#endif

#if defined(CLOCK_MONOTONIC)

          if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
          {
               const auto duration =
                    std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);

               return std::chrono::steady_clock::time_point(
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration));
          }

#endif

          return std::chrono::steady_clock::now();
     }
     catch (...)
     {
          return std::chrono::steady_clock::time_point{};
     }
}
