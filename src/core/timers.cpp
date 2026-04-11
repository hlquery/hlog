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

#include "core/timers.h"

#include <algorithm>
#include <mutex>

#include "core/hlcore.h"

TimerManager::Clock::time_point GetNow()
{
     return Instance ? Instance->Now() : TimerManager::Clock::now();
}

TimerManager::TimerManager()
{

}

TimerManager::~TimerManager()
{

}

void TimerManager::Add(Task task, std::chrono::milliseconds delay, bool repeating)
{
     std::unique_lock<std::shared_mutex> lock(MutexValue);
     const auto now = GetNow();
     Entries.push_back(Entry{now + delay, delay, std::move(task), repeating});
}

void TimerManager::Tick()
{
     std::unique_lock<std::shared_mutex> lock(MutexValue);
     const auto now = GetNow();

     for (auto& entry : Entries)
     {
          if (now < entry.next_run)
          {
               continue;
          }

          if (entry.task)
          {
               Task taskCopy = entry.task;
               lock.unlock();
               taskCopy();
               lock.lock();
          }

          entry.next_run = entry.repeating ? now + entry.interval : Clock::time_point::max();
     }

     Entries.erase(
          std::remove_if(Entries.begin(), Entries.end(), [](const Entry& entry) {
               return !entry.repeating && entry.next_run == Clock::time_point::max();
          }),
          Entries.end());
}

int TimerManager::GetTimeUntilNextMs()
{
     std::shared_lock<std::shared_mutex> lock(MutexValue);

     if (Entries.empty())
     {
          return -1;
     }

     const auto now = GetNow();
     auto earliest = Clock::time_point::max();

     for (const auto& entry : Entries)
     {
          if (entry.next_run < earliest)
          {
               earliest = entry.next_run;
          }
     }

     if (earliest == Clock::time_point::max())
     {
          return -1;
     }

     const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(earliest - now).count();
     return diff <= 0 ? 0 : static_cast<int>(diff);
}

size_t TimerManager::GetTimerCount() const
{
     std::shared_lock<std::shared_mutex> lock(MutexValue);
     return Entries.size();
}

void TimerManager::Clear()
{
     std::unique_lock<std::shared_mutex> lock(MutexValue);
     Entries.clear();
}
