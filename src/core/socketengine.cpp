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

#include "core/socketengine.h"

#include <cerrno>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif


#ifdef _WIN32
using NativeSocketHandle = SOCKET;
constexpr NativeSocketHandle NativeInvalidSocket = INVALID_SOCKET;
#else
using NativeSocketHandle = int;
constexpr NativeSocketHandle NativeInvalidSocket = -1;
#endif

/* Convert the internal handle storage into the platform socket type. */

NativeSocketHandle ToNativeHandle(SocketEngine::SocketHandle handle)
{
#ifdef _WIN32
     return reinterpret_cast<SOCKET>(handle);
#else
     return handle;
#endif
}

/* Convert the platform socket type into the internal handle storage. */

SocketEngine::SocketHandle ToStoredHandle(NativeSocketHandle handle)
{
#ifdef _WIN32
     return reinterpret_cast<SocketEngine::SocketHandle>(handle);
#else
     return handle;
#endif
}


/* Construct the socket engine with an initially closed handle. */

SocketEngine::SocketEngine() = default;

/* Close any open socket when the engine is destroyed. */

SocketEngine::~SocketEngine()
{
     Close();
}

/* Initialize platform socket state before any socket operations. */

bool SocketEngine::InitializeSockets(std::string& error)
{
#ifdef _WIN32
     static bool initialized = false;
     static bool success = false;

     if (initialized)
     {
          if (!success)
          {
               error = "WSAStartup failed.";
          }

          return success;
     }

     initialized = true;

     WSADATA data{};
     const int rc = WSAStartup(MAKEWORD(2, 2), &data);
     if (rc != 0)
     {
          error = "WSAStartup failed: " + std::to_string(rc) + ".";
          success = false;
          return false;
     }

     success = true;
     return true;
#else
     (void)error;
     return true;
#endif
}

/* Describe a platform socket error code with a readable message. */

std::string SocketEngine::DescribeSocketError(int error_code)
{
#ifdef _WIN32
     LPSTR buffer = nullptr;
     const DWORD length = FormatMessageA(
          FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr,
          static_cast<DWORD>(error_code),
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          reinterpret_cast<LPSTR>(&buffer),
          0,
          nullptr);

     std::string message = "Win32 error " + std::to_string(error_code);

     if (length > 0 && buffer)
     {
          message += ": ";
          message.append(buffer, length);

          while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
          {
               message.pop_back();
          }
     }

     if (buffer)
     {
          LocalFree(buffer);
     }

     return message;
#else
     return std::strerror(error_code);
#endif
}

/* Return whether the given error means the socket would block. */

bool SocketEngine::IsWouldBlockError(int error_code)
{
#ifdef _WIN32
     return error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS || error_code == WSAEALREADY;
#else
     return error_code == EAGAIN || error_code == EWOULDBLOCK || error_code == EINPROGRESS || error_code == EALREADY;
#endif
}

/* Return the most recent platform socket error code. */

int SocketEngine::GetLastSocketErrorCode()
{
#ifdef _WIN32
     return WSAGetLastError();
#else
     return errno;
#endif
}

/* Close one native socket handle when it is valid. */

void SocketEngine::CloseNativeSocket(SocketHandle handle)
{
     if (handle == InvalidSocket)
     {
          return;
     }

#ifdef _WIN32
     closesocket(ToNativeHandle(handle));
#else
     ::close(ToNativeHandle(handle));
#endif
}

/* Enable or disable non-blocking mode on the active socket. */

bool SocketEngine::SetNonBlocking(bool enabled, std::string& error)
{
     if (!IsOpen())
     {
          error = "Socket is not open.";
          return false;
     }

#ifdef _WIN32
     u_long mode = enabled ? 1UL : 0UL;
     if (ioctlsocket(ToNativeHandle(Handle), FIONBIO, &mode) != 0)
     {
          error = "ioctlsocket(FIONBIO) failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return false;
     }
#else
     const int flags = fcntl(ToNativeHandle(Handle), F_GETFL, 0);
     if (flags < 0)
     {
          error = "fcntl(F_GETFL) failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return false;
     }

     const int updated_flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
     if (fcntl(ToNativeHandle(Handle), F_SETFL, updated_flags) != 0)
     {
          error = "fcntl(F_SETFL) failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return false;
     }
#endif

     NonBlocking = enabled;
     return true;
}

/* Wait until the socket is ready for reading or writing. */

SocketWaitResult SocketEngine::WaitForEvent(bool read_ready, int timeout_ms, std::string& error) const
{
     if (!IsOpen())
     {
          error = "Socket is not open.";
          return SocketWaitResult::Error;
     }

#ifdef _WIN32
     fd_set read_set;
     fd_set write_set;
     FD_ZERO(&read_set);
     FD_ZERO(&write_set);

     if (read_ready)
     {
          FD_SET(ToNativeHandle(Handle), &read_set);
     }
     else
     {
          FD_SET(ToNativeHandle(Handle), &write_set);
     }

     timeval timeout{};
     timeout.tv_sec = timeout_ms / 1000;
     timeout.tv_usec = (timeout_ms % 1000) * 1000;

     const int rc = select(0,
                           read_ready ? &read_set : nullptr,
                           read_ready ? nullptr : &write_set,
                           nullptr,
                           timeout_ms < 0 ? nullptr : &timeout);
     if (rc == 0)
     {
          return SocketWaitResult::Timeout;
     }

     if (rc < 0)
     {
          error = "select() failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return SocketWaitResult::Error;
     }
#else
     pollfd descriptor{};
     descriptor.fd = ToNativeHandle(Handle);
     descriptor.events = read_ready ? POLLIN : POLLOUT;

     const int rc = ::poll(&descriptor, 1, timeout_ms);
     if (rc == 0)
     {
          return SocketWaitResult::Timeout;
     }

     if (rc < 0)
     {
          error = "poll() failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return SocketWaitResult::Error;
     }

     if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
     {
          error = "poll() reported a socket error.";
          return SocketWaitResult::Error;
     }
#endif

     return SocketWaitResult::Ready;
}

/* Connect to the target endpoint with optional non-blocking mode. */

bool SocketEngine::Connect(const std::string& host, int port, bool non_blocking, int timeout_ms, std::string& error)
{
     Close();

     if (!InitializeSockets(error))
     {
          return false;
     }

     addrinfo hints{};
     hints.ai_family = AF_UNSPEC;
     hints.ai_socktype = SOCK_STREAM;

     addrinfo* result = nullptr;
     const std::string port_text = std::to_string(port);
     const int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
     if (rc != 0)
     {
#ifdef _WIN32
          error = "getaddrinfo failed for " + host + ": " + std::to_string(rc) + ".";
#else
          error = "getaddrinfo failed for " + host + ": " + std::string(gai_strerror(rc)) + ".";
#endif
          return false;
     }

     bool connected = false;

     for (addrinfo* current = result; current != nullptr; current = current->ai_next)
     {
          const NativeSocketHandle fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
          if (fd == NativeInvalidSocket)
          {
               continue;
          }

          Handle = ToStoredHandle(fd);

          if (!SetNonBlocking(non_blocking, error))
          {
               Close();
               continue;
          }

          const int connect_rc = ::connect(fd, current->ai_addr, current->ai_addrlen);
          if (connect_rc == 0)
          {
               connected = true;
               break;
          }

          const int socket_error = GetLastSocketErrorCode();

          if (non_blocking && IsWouldBlockError(socket_error))
          {
               std::string wait_error;
               const SocketWaitResult wait_result = WaitForWrite(timeout_ms, wait_error);
               if (wait_result == SocketWaitResult::Ready)
               {
                    int pending_error = 0;
#ifdef _WIN32
                    int length = sizeof(pending_error);
#else
                    socklen_t length = sizeof(pending_error);
#endif
                    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&pending_error), &length) == 0 &&
                        pending_error == 0)
                    {
                         connected = true;
                         break;
                    }
               }
               else if (wait_result == SocketWaitResult::Error)
               {
                    error = wait_error;
               }
          }
          else
          {
               error = "connect() failed: " + DescribeSocketError(socket_error) + ".";
          }

          Close();
     }

     ::freeaddrinfo(result);

     if (!connected)
     {
          if (error.empty())
          {
               error = "connect() failed for " + host + ":" + std::to_string(port) + ".";
          }

          Close();
          return false;
     }

     return true;
}

/* Apply the same timeout to send and receive operations. */

bool SocketEngine::SetTimeoutSeconds(int timeout_seconds, std::string& error)
{
     if (!IsOpen())
     {
          error = "Socket is not open.";
          return false;
     }

#ifdef _WIN32
     const int timeout_ms = timeout_seconds * 1000;
     if (::setsockopt(ToNativeHandle(Handle), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)) != 0)
     {
          error = "setsockopt(SO_RCVTIMEO) failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return false;
     }

     if (::setsockopt(ToNativeHandle(Handle), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)) != 0)
     {
          error = "setsockopt(SO_SNDTIMEO) failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return false;
     }
#else
     timeval timeout{};
     timeout.tv_sec = timeout_seconds;
     timeout.tv_usec = 0;

     if (::setsockopt(ToNativeHandle(Handle), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
     {
          error = "setsockopt(SO_RCVTIMEO) failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return false;
     }

     if (::setsockopt(ToNativeHandle(Handle), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
     {
          error = "setsockopt(SO_SNDTIMEO) failed: " + DescribeSocketError(GetLastSocketErrorCode()) + ".";
          return false;
     }
#endif

     return true;
}

/* Wait until the socket is readable. */

SocketWaitResult SocketEngine::WaitForRead(int timeout_ms, std::string& error) const
{
     return WaitForEvent(true, timeout_ms, error);
}

/* Wait until the socket is writable. */

SocketWaitResult SocketEngine::WaitForWrite(int timeout_ms, std::string& error) const
{
     return WaitForEvent(false, timeout_ms, error);
}

/* Receive bytes from the socket into the caller buffer. */

SocketIoResult SocketEngine::Receive(char* buffer, size_t length, long& bytes_read, std::string& error) const
{
     bytes_read = 0;

     if (!IsOpen())
     {
          error = "Socket is not open.";
          return SocketIoResult::Error;
     }

#ifdef _WIN32
     const int rc = ::recv(ToNativeHandle(Handle), buffer, static_cast<int>(length), 0);
#else
     const ssize_t rc = ::recv(ToNativeHandle(Handle), buffer, length, 0);
#endif
     if (rc > 0)
     {
          bytes_read = static_cast<long>(rc);
          return SocketIoResult::Success;
     }

     if (rc == 0)
     {
          return SocketIoResult::Closed;
     }

     const int socket_error = GetLastSocketErrorCode();
     if (IsWouldBlockError(socket_error))
     {
          return SocketIoResult::WouldBlock;
     }

     error = "recv() failed: " + DescribeSocketError(socket_error) + ".";
     return SocketIoResult::Error;
}

/* Send bytes from the caller buffer through the socket. */

SocketIoResult SocketEngine::Send(const char* buffer, size_t length, long& bytes_sent, std::string& error) const
{
     bytes_sent = 0;

     if (!IsOpen())
     {
          error = "Socket is not open.";
          return SocketIoResult::Error;
     }

#ifdef _WIN32
     const int rc = ::send(ToNativeHandle(Handle), buffer, static_cast<int>(length), 0);
#else
     const ssize_t rc = ::send(ToNativeHandle(Handle), buffer, length, 0);
#endif
     if (rc > 0)
     {
          bytes_sent = static_cast<long>(rc);
          return SocketIoResult::Success;
     }

     if (rc == 0)
     {
          return SocketIoResult::Closed;
     }

     const int socket_error = GetLastSocketErrorCode();
     if (IsWouldBlockError(socket_error))
     {
          return SocketIoResult::WouldBlock;
     }

     error = "send() failed: " + DescribeSocketError(socket_error) + ".";
     return SocketIoResult::Error;
}

/* Send the full payload, waiting when the socket would block. */

bool SocketEngine::SendAll(const std::string& payload, int wait_timeout_ms, std::string& error) const
{
     size_t sent = 0;

     while (sent < payload.size())
     {
          long bytes_sent = 0;
          const SocketIoResult result = Send(payload.data() + sent, payload.size() - sent, bytes_sent, error);
          if (result == SocketIoResult::Success)
          {
               sent += static_cast<size_t>(bytes_sent);
               continue;
          }

          if (result == SocketIoResult::WouldBlock)
          {
               const SocketWaitResult wait_result = WaitForWrite(wait_timeout_ms, error);
               if (wait_result == SocketWaitResult::Ready)
               {
                    continue;
               }

               if (wait_result == SocketWaitResult::Timeout)
               {
                    error = "socket send timed out.";
               }

               return false;
          }

          if (result == SocketIoResult::Closed)
          {
               error = "socket closed during send.";
          }

          return false;
     }

     return true;
}

/* Return whether the engine currently owns an open socket. */

bool SocketEngine::IsOpen() const
{
     return Handle != InvalidSocket;
}

/* Close the current socket and reset runtime flags. */

void SocketEngine::Close()
{
     if (!IsOpen())
     {
          return;
     }

     CloseNativeSocket(Handle);
     Handle = InvalidSocket;
     NonBlocking = false;
}
