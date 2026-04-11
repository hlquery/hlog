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

#include <cstddef>
#include <string>

#include "core/config.h"

enum class SocketWaitResult
{
     Ready,
     Timeout,
     Error
};

enum class SocketIoResult
{
     Success,
     Closed,
     WouldBlock,
     Error
};

class CoreExport SocketEngine
{
   public:
#ifdef _WIN32
     using SocketHandle = void*;
     static constexpr SocketHandle InvalidSocket = nullptr;
#else
     using SocketHandle = int;
     static constexpr SocketHandle InvalidSocket = -1;
#endif

   private:
     SocketHandle Handle = InvalidSocket;
     bool NonBlocking = false;

     static bool InitializeSockets(std::string& error);
     static std::string DescribeSocketError(int error_code);
     static bool IsWouldBlockError(int error_code);
     static int GetLastSocketErrorCode();
     static void CloseNativeSocket(SocketHandle handle);
     bool SetNonBlocking(bool enabled, std::string& error);
     SocketWaitResult WaitForEvent(bool read_ready, int timeout_ms, std::string& error) const;

   public:

     /* Construct an empty socket wrapper. */

     SocketEngine();

     /* Close any open socket on destruction. */

     ~SocketEngine();

     /* Open a TCP connection to one resolved host address. */

     bool Connect(const std::string& host, int port, bool non_blocking, int timeout_ms, std::string& error);

     /* Apply send and receive socket timeouts in seconds. */

     bool SetTimeoutSeconds(int timeout_seconds, std::string& error);

     /* Wait until the socket can be read without blocking. */

     SocketWaitResult WaitForRead(int timeout_ms, std::string& error) const;

     /* Wait until the socket can be written without blocking. */

     SocketWaitResult WaitForWrite(int timeout_ms, std::string& error) const;

     /* Read one chunk from the socket. */

     SocketIoResult Receive(char* buffer, size_t length, long& bytes_read, std::string& error) const;

     /* Write one chunk to the socket. */

     SocketIoResult Send(const char* buffer, size_t length, long& bytes_sent, std::string& error) const;

     /* Write the full payload, waiting when the socket back-pressures. */

     bool SendAll(const std::string& payload, int wait_timeout_ms, std::string& error) const;

     /* Return whether the wrapper currently owns an open socket. */

     bool IsOpen() const;

     /* Close the current socket, if any. */

     void Close();
};
