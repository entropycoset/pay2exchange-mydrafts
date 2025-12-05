#pragma once
#ifndef INCLUDEGUARD_ECUL_ECUL_SOCKET_HPP
#define INCLUDEGUARD_ECUL_ECUL_SOCKET_HPP

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <errno.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <chrono>
  #include <thread>
#endif

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "ecul.hpp" // basic errors/logging/etc

namespace ecul {
namespace socket { 

// On given SOCKET (fd) set the given timeout, to be used on blocking operations
void set_socket_send_timeout(
#if defined(_WIN32)
  SOCKET fd,
#else
  int fd,
#endif
  std::chrono::milliseconds timeout)
;

/// Write data to a socket `fd` in chunks with a global timeout.
///
/// This function attempts to write the entire string `data` to the socket,
/// splitting into chunks of size `chunk_size`. It enforces a global timeout
/// across all iterations, retries on recoverable errors (EINTR, EAGAIN, WOULDBLOCK),
/// and applies an backoff (e.g. 50ms to avoid bussy-loop waiting) when the peer is slow. On unrecoverable errors
/// or timeout, it throws with ecul_error.
/// It may log details.
///
/// @warning before calling, the socket `fd` should be consigured to small timeout (e.g. 100 ms ?) with `set_socket_send_timeout` otherwise it might hang forever if we are fully blocked on IO
///
/// @param fd - the FD of socket to use (see warning about settings socket timeout)
/// @param data - data to be written (binary write ok)
/// @param timeout_since - from this point in time we count the timeout, caller can pass here now() to count from call, or e.g. timestamp when he started longer operation that must complete soon
/// @param timeout_max_ms - number of ms (1000 = 1 second) that we will wait (since timeout_since) before timing out (more or less, also make sure socet `fd`has set own, small, socket timeout)
/// @param chunk_size - we might wish to send the data in chunks e.g. 64 KiB (just to do smaller separate calls, they might work better?)
///
/// Usage: call with a connected socket, a start time, and a maximum timeout.
/// Defensive coding ensures no invalid fd, no zero chunk size, and no overflow.
void write_with_timeout_chunks(
#if defined(_WIN32)
  SOCKET fd,
#else
  int fd,
#endif
  const std::string &data,
  std::chrono::steady_clock::time_point timeout_since,
  std::chrono::milliseconds timeout_max_ms,
  size_t chunk_size);


} // namespace socket
} // namespace ecul


#endif