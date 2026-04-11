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

#include <vector>

#include "core/pipeline.h"
#include "core/types.h"

bool ReopenFile(FileState& state, bool initialOpen);
void RunPipelineLoop(std::vector<FileState>& states, const Pipeline& pipeline, WatchMode mode, int intervalMs);
