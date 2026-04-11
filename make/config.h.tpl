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

#define SOCKETENGINE_NAME "${SOCKETENGINE_NAME}"
#define HLQUERY_VERSION "${HLQUERY_VERSION}"
#define HLQUERY_SYSTEM "${HLQUERY_SYSTEM}"
#define HLOG_BASE_DIR "${HLOG_BASE_DIR}"
#define HLQUERY_CONFIG_DIR "${HLQUERY_CONFIG_DIR}"
#define HLQUERY_DATA_DIR "${HLQUERY_DATA_DIR}"
#define HLQUERY_LOG_DIR "${HLQUERY_LOG_DIR}"
#define HLQUERY_ADMIN_DIR "${HLQUERY_ADMIN_DIR}"
#define HLQUERY_PID_DIR "${HLQUERY_PID_DIR}"
#define HLQUERY_SSL_DIR "${HLQUERY_SSL_DIR}"
#define HLQUERY_DEFAULT_HTTP_PORT ${HLQUERY_DEFAULT_HTTP_PORT}
#define HLQUERY_MAX_THREADS ${HLQUERY_MAX_THREADS}
#define HLOG_MODULE_SUFFIX "${HLOG_MODULE_SUFFIX}"
#define DNS_CACHE_MAX_SIZE 1024
#define CONFIG_READER_MAX_CONFIG_SIZE (10 * 1024 * 1024)
#define CONFIG_READER_MAX_TAGS 10000
#define CONFIG_READER_MAX_ATTRIBUTES 100
#define CONFIG_READER_MAX_ATTR_LENGTH 65536
#define CONFIG_READER_MAX_INCLUDE_DEPTH 10
#define CONFIG_READER_MAX_INCLUDE_SIZE (5 * 1024 * 1024)

#if defined(_WIN32)
# define CoreExport __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
# define CoreExport __attribute__((visibility("default")))
#else
# define CoreExport
#endif
