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

#include "core/modules.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

#ifdef __linux__
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

#include "common/options.h"
#include "core/hlcore.h"
#include "core/pipeline.h"
#include "core/pipeline.h"


/* Shared module run flag flipped by process signals. */

volatile std::sig_atomic_t Running = 1;

/* Stop the source loop on process termination signals. */

void HandleSignal(int)
{
     Running = 0;
}

/* Read one string attribute from the module config with fallback. */

std::string GetModuleAttribute(const ModuleConfig& config, const std::string& key, const std::string& fallback = "")
{
     const auto it = config.Attributes.find(key);
     if (it == config.Attributes.end())
     {
          return fallback;
     }

     return it->second;
}

/* Split comma-separated file lists while trimming whitespace. */

std::vector<std::string> SplitCommaSeparated(const std::string& value)
{
     std::vector<std::string> result;
     std::string current;

     for (char ch : value)
     {
          if (ch == ',')
          {
               const size_t start = current.find_first_not_of(" \t\r\n");
               if (start != std::string::npos)
               {
                    const size_t end = current.find_last_not_of(" \t\r\n");
                    result.push_back(current.substr(start, end - start + 1));
               }
               current.clear();
               continue;
          }

          current.push_back(ch);
     }

     const size_t start = current.find_first_not_of(" \t\r\n");
     if (start != std::string::npos)
     {
          const size_t end = current.find_last_not_of(" \t\r\n");
          result.push_back(current.substr(start, end - start + 1));
     }

     return result;
}

/* Refresh inode, size, and timestamp metadata for one tracked file. */

bool RefreshFileMetadata(FileState& state)
{
     struct stat st;
     if (::stat(state.PathValue.c_str(), &st) != 0)
     {
          state.Device = 0;
          state.Inode = 0;
          state.Size = 0;
          state.ModifiedTick = 0;
          state.Available = false;
          return false;
     }

     state.Device = st.st_dev;
     state.Inode = st.st_ino;
     state.Size = static_cast<std::uintmax_t>(st.st_size);
#if defined(__linux__)
     state.ModifiedTick = (static_cast<std::uint64_t>(st.st_mtim.tv_sec) << 32) |
                          static_cast<std::uint32_t>(st.st_mtim.tv_nsec);
#else
     state.ModifiedTick = static_cast<std::uint64_t>(st.st_mtime);
#endif
     state.Available = true;
     return true;
}

/* Reopen a tracked file after rotation, truncation, or first startup. */

bool ReopenFile(FileState& state, bool initialOpen)
{
     state.Stream.close();
     state.Stream.clear();

     if (!RefreshFileMetadata(state))
     {
          return false;
     }

     state.Stream.open(state.PathValue, std::ios::in);
     if (!state.Stream.is_open())
     {
          state.Available = false;
          return false;
     }

     if (initialOpen && state.Start == StartPosition::End)
     {
          state.Stream.seekg(0, std::ios::end);
     }

     const auto pos = state.Stream.tellg();
     state.Offset = (pos == std::streampos(-1)) ? 0 : static_cast<std::uintmax_t>(pos);
     state.Pending.clear();
     return true;
}

/* Drain any newly appended lines into the pipeline. */

void DrainFile(FileState& state, const Pipeline& pipeline)
{
     if (!state.Stream.is_open())
     {
          return;
     }

     state.Stream.clear();
     state.Stream.seekg(static_cast<std::streamoff>(state.Offset), std::ios::beg);

     std::string chunk;
     std::uintmax_t consumed = state.Offset;
     while (std::getline(state.Stream, chunk))
     {
          if (state.Stream.eof())
          {
               state.Pending += chunk;
               consumed = state.Size;
               break;
          }

          pipeline.ProcessLine(state, state.Pending + chunk);
          state.Pending.clear();

          const auto pos = state.Stream.tellg();
          if (pos != std::streampos(-1))
          {
               consumed = static_cast<std::uintmax_t>(pos);
          }
     }

     state.Stream.clear();
     state.Offset = consumed;
}

/* Detect file changes and resynchronize the stream before reading. */

void CheckAndRead(FileState& state, const Pipeline& pipeline)
{
     struct stat st;
     if (::stat(state.PathValue.c_str(), &st) != 0)
     {
          state.Available = false;
          state.Stream.close();
          state.Stream.clear();
          state.Offset = 0;
          state.Size = 0;
          state.ModifiedTick = 0;
          state.Pending.clear();
          return;
     }

     const bool fileChanged = !state.Available || state.Device != st.st_dev || state.Inode != st.st_ino;
     const std::uintmax_t currentSize = static_cast<std::uintmax_t>(st.st_size);
#if defined(__linux__)
     const std::uint64_t currentModifiedTick = (static_cast<std::uint64_t>(st.st_mtim.tv_sec) << 32) |
                                               static_cast<std::uint32_t>(st.st_mtim.tv_nsec);
#else
     const std::uint64_t currentModifiedTick = static_cast<std::uint64_t>(st.st_mtime);
#endif
     const bool truncated = !fileChanged && currentSize < state.Offset;
     const bool rewrittenSameSize = !fileChanged &&
                                    currentSize == state.Offset &&
                                    currentModifiedTick != state.ModifiedTick;

     if (fileChanged || truncated || rewrittenSameSize || !state.Stream.is_open())
     {
          ReopenFile(state, false);
     }

     RefreshFileMetadata(state);
     DrainFile(state, pipeline);
}

/* Map auto mode onto the best available backend for the platform. */

WatchMode ResolveMode(WatchMode configured)
{
     if (configured != WatchMode::Auto)
     {
          return configured;
     }

#ifdef __linux__
     return WatchMode::Kernel;
#else
     return WatchMode::Poll;
#endif
}

#ifdef __linux__
class InotifyWatcher
{
   public:
     /* Watch parent directories and dispatch file changes back to states. */

     explicit InotifyWatcher(std::vector<FileState>& states, const Pipeline& pipeline)
         : States(states), PipelineRef(pipeline)
     {
          Fd = inotify_init1(IN_NONBLOCK);
          if (Fd < 0)
          {
               throw std::runtime_error("inotify_init1 failed: " + std::string(std::strerror(errno)));
          }

          for (auto& state : States)
          {
               const std::string dir = state.ParentDir.empty() ? "." : state.ParentDir.string();
               if (WatchByDir.find(dir) != WatchByDir.end())
               {
                    continue;
               }

               const int wd = inotify_add_watch(
                    Fd,
                    dir.c_str(),
                    IN_MODIFY | IN_ATTRIB | IN_MOVE_SELF | IN_DELETE_SELF | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_CLOSE_WRITE);
               if (wd < 0)
               {
                    throw std::runtime_error("Failed to watch " + dir + ": " + std::string(std::strerror(errno)));
               }

               WatchByDir.emplace(dir, wd);
               DirByWatch.emplace(wd, dir);
          }
     }

     ~InotifyWatcher()
     {
          if (Fd >= 0)
          {
               close(Fd);
          }
     }

     void Run(int intervalMs)
     {
          struct pollfd pfd{Fd, POLLIN, 0};
          std::vector<char> buffer(16 * 1024);

          while (Running)
          {
               const int rc = ::poll(&pfd, 1, intervalMs);
               if (rc < 0)
               {
                    if (errno == EINTR)
                    {
                         continue;
                    }
                    throw std::runtime_error("poll(inotify) failed: " + std::string(std::strerror(errno)));
               }

               if (rc == 0)
               {
                    for (auto& state : States)
                    {
                         CheckAndRead(state, PipelineRef);
                    }
                    continue;
               }

               const ssize_t bytes = ::read(Fd, buffer.data(), buffer.size());
               if (bytes <= 0)
               {
                    continue;
               }

               size_t offset = 0;
               while (offset < static_cast<size_t>(bytes))
               {
                    const auto* event = reinterpret_cast<const struct inotify_event*>(buffer.data() + offset);
                    const auto dirIt = DirByWatch.find(event->wd);
                    if (dirIt != DirByWatch.end())
                    {
                         const std::string changedName = event->len > 0 ? std::string(event->name) : std::string();
                         for (auto& state : States)
                         {
                              if (state.ParentDir == dirIt->second &&
                                  (changedName.empty() || changedName == state.PathValue.filename().string()))
                              {
                                   CheckAndRead(state, PipelineRef);
                              }
                         }
                    }
                    offset += sizeof(struct inotify_event) + event->len;
               }
          }
     }

   private:
     std::vector<FileState>& States;
     const Pipeline& PipelineRef;
     int Fd = -1;
     std::unordered_map<std::string, int> WatchByDir;
     std::unordered_map<int, std::string> DirByWatch;
};
#endif

/* Fallback polling loop used on non-Linux systems and refresh mode. */

void RunPollLoop(std::vector<FileState>& states, const Pipeline& pipeline, int intervalMs)
{
     while (Running)
     {
          for (auto& state : states)
          {
               CheckAndRead(state, pipeline);
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
     }
}


class FileInputModule final : public HLogModule
{
   public:
     FileInputModule()
         : HLogModule("filein")
     {
     }

     bool Start(const ModuleConfig& config, std::string& errorMessage) override
     {
          /* Cache the raw config so Run() can still inspect method overrides. */

          ModuleConfigValue = config;

          const std::string startValue = GetModuleAttribute(config, "start_position", "end");
          try
          {
               StartPositionValue = ParseStartPosition(startValue);
          }
          catch (const std::exception& ex)
          {
               errorMessage = ex.what();
               return false;
          }

          std::string pathValue = GetModuleAttribute(config, "path", "");
          if (pathValue.empty())
          {
               pathValue = GetModuleAttribute(config, "files", "");
          }

          if (!pathValue.empty())
          {
               /* Explicit file lists take precedence over hlquery.conf fallback. */

               for (const auto& rawPath : SplitCommaSeparated(pathValue))
               {
                    std::filesystem::path path(rawPath);
                    if (!path.empty())
                    {
                         Paths.push_back(std::filesystem::absolute(path));
                    }
               }
          }
          else if (Instance)
          {
               try
               {
                    Paths = ResolveFilesFromHlqueryConfig(Instance->Config->GetOptions().ConfigPath);
               }
               catch (const std::exception& ex)
               {
                    errorMessage = ex.what();
                    return false;
               }
          }

          for (const auto& path : Paths)
          {
               /* Create one tracked state per file and prime its read offset. */

               FileState state;
               state.PathValue = std::filesystem::absolute(path);
               state.ParentDir = state.PathValue.parent_path();
               state.Label = state.PathValue.filename().string();
               if (state.Label.empty())
               {
                    state.Label = state.PathValue.string();
               }
               state.Start = StartPositionValue;
               ReopenFile(state, true);
               States.push_back(std::move(state));
          }

          if (Instance && Instance->Logs)
          {
               for (const auto& state : States)
               {
                    Instance->Logs->Normal("input_file", "path=" + state.PathValue.string() +
                         " start_position=" + std::string(state.Start == StartPosition::Beginning ? "beginning" : "end") + ".");
               }

               if (States.empty())
               {
                    Instance->Logs->Normal("input_file", "No input_file entries resolved; running idle.");
               }
          }

          return true;
     }

     bool IsSourceModule() const override
     {
          return true;
     }

     bool Run(const Pipeline& pipeline, WatchMode mode, int intervalMs, std::string& errorMessage) override
     {
          Running = 1;
          std::signal(SIGINT, HandleSignal);
#ifdef SIGTERM
          std::signal(SIGTERM, HandleSignal);
#endif

          try
          {
               /* Module-level settings can still override wrapper-level watch mode. */

               const std::string methodValue = GetModuleAttribute(ModuleConfigValue, "method", "");
               if (!methodValue.empty())
               {
                    mode = ParseMode(methodValue);
               }

               const std::string refreshValue = GetModuleAttribute(ModuleConfigValue, "refresh_ms", "");
               if (!refreshValue.empty())
               {
                    intervalMs = std::max(100, std::stoi(refreshValue));
               }

               mode = ResolveMode(mode);

               if (States.empty())
               {
                    /* Keep the source module alive even when no files resolved yet. */

                    while (Running)
                    {
                         std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                    }

                    return true;
               }

#ifdef __linux__
               if (mode == WatchMode::Kernel)
               {
                    /* Use inotify when available so tailing reacts immediately. */

                    InotifyWatcher watcher(States, pipeline);
                    watcher.Run(intervalMs);
                    return true;
               }
#endif

               RunPollLoop(States, pipeline, intervalMs);
               return true;
          }
          catch (const std::exception& ex)
          {
               errorMessage = ex.what();
               return false;
          }
     }

   private:
     ModuleConfig ModuleConfigValue;
     std::vector<std::filesystem::path> Paths;
     std::vector<FileState> States;
     StartPosition StartPositionValue = StartPosition::End;
};

MODULE_LOAD(FileInputModule)
