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

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/forwards.h"
#include "core/types.h"

/* Resolve fallback input files from the referenced hlquery config. */

std::vector<std::filesystem::path> ResolveFilesFromHlqueryConfig(const std::string& configPath);

/* Validate that the referenced hlquery config can be read. */

void ValidateHlqueryConfig(const std::string& configPath);

/* Load the standalone hlog pipeline configuration file. */

PipelineConfig LoadPipelineConfig(const std::string& path);

/* Runtime pipeline that transforms lines and dispatches outputs. */

class Pipeline
{
   public:

     /* Construct the pipeline and load all configured modules. */

     explicit Pipeline(PipelineConfig config, LogManager* logs);

     /* Release outputs and loaded modules. */

     ~Pipeline();

     /* Return the resolved runtime pipeline config. */

     const PipelineConfig& GetConfig() const;

     /* Process one input line through filters, modules, and outputs. */

     void ProcessLine(const FileState& state, const std::string& line, LogManager* logs) const;

     /* Return whether a source module owns the input loop. */

     bool HasSourceModule() const;

     /* Run the configured source module when one is present. */

     bool RunSourceModule(WatchMode mode, int intervalMs, LogManager* logs, std::string& errorMessage) const;

   private:

     /* Internal async hlquery document output worker. */

     class AsyncHlqueryOutput;

     /* Fully resolved runtime pipeline config. */

     PipelineConfig Config;

     /* Optional local failure recorder for dropped posts. */

     std::shared_ptr<FailureRecorder> FailureRecorderPtr;

     /* Loaded runtime filter and source modules. */

     std::unique_ptr<HLogModuleManager> ModuleManager;

     /* Async hlquery posting backend. */

     std::unique_ptr<AsyncHlqueryOutput> HlqueryOutput;
};
