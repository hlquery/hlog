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

#include "utils/tools.h"

#include <ctime>
#include <random>

std::string Tools::RandomHex(size_t length)
{
     static thread_local std::random_device rd;
     static thread_local std::mt19937 gen(rd());
     static const char hex_chars[] = "0123456789ABCDEF";

     std::uniform_int_distribution<> dist(0, 15);
     std::string result;
     result.reserve(length);

     for (size_t i = 0; i < length; ++i)
     {
          result.push_back(hex_chars[dist(gen)]);
     }

     return result;
}

std::string Tools::GetTimestamp(const std::string& format)
{
     const std::time_t now = std::time(nullptr);
     std::tm tm_value{};
     localtime_r(&now, &tm_value);

     char buffer[128];
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
     if (std::strftime(buffer, sizeof(buffer), format.c_str(), &tm_value) == 0)
     {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
          return "";
     }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

     return buffer;
}
