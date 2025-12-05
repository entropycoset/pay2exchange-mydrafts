
#include "ecul_socket.hpp"

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
{
  if (
#if defined(_WIN32)
    fd == INVALID_SOCKET
#else
    fd < 0
#endif
  ) {
    throw ecul_erro(mkstr() << "Invalid socket fd=" << fd);
  }

  auto ms = timeout.count();
  if (ms < 0) {
    throw ecul_erro(mkstr() << "Negative timeout not allowed: " << ms);
  }

#if defined(_WIN32)
  if (ms > static_cast<long long>(std::numeric_limits<DWORD>::max())) {
    throw ecul_erro(mkstr() << "Timeout too large for DWORD: " << ms);
  }
  DWORD to_ms = static_cast<DWORD>(ms);
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&to_ms),
                 sizeof(to_ms)) == SOCKET_ERROR) {
    int err = WSAGetLastError();
    throw ecul_erro(mkstr() << "setsockopt(SO_SNDTIMEO) failed, WSAError=" << err);
  }
#else
  if (ms / 1000 > std::numeric_limits<long>::max()) {
    throw ecul_erro(mkstr() << "Timeout seconds overflow for timeval: " << ms);
  }
  struct timeval tv;
  tv.tv_sec = static_cast<long>(ms / 1000);
  tv.tv_usec = static_cast<long>((ms % 1000) * 1000);
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
    const auto errno_save = errno;
    throw ecul_erro(mkstr() << "setsockopt(SO_SNDTIMEO) failed, errno=" << errno_save);
  }
#endif
}

/// returns how many seconds (fraction) elapsed since time `since`, but since must NOT be in future, otherwise display warning, sets (warning)=1 to avoid repeating warning;
/// the warning should be local variable at caller to avoid repeated warn
double now_elapsed( std::chrono::steady_clock::time_point timeout_since , bool & warned) {
    // if (!warned) ecul_abort("nullptr of 'warned' bool, it must be reference to local variable of caller")
    const auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>( now - timeout_since).count();
    if ((elapsed_sec < 0) &&  (!warned)) {
        warned=1;
        ecul_log_warn(mkstr()<<"The duration time point 'since' is in future (elapsed " << elapsed_sec << ")");
    }
    return elapsed_sec;
}

// Main function: write with timeout in chunks
void write_with_timeout_chunks(
#if defined(_WIN32)
  SOCKET fd,
#else
  int fd,
#endif
  const std::string &data,
  std::chrono::steady_clock::time_point timeout_since,
  std::chrono::milliseconds timeout_max_ms,
  size_t chunk_size)
{
  if (
#if defined(_WIN32)
    fd == INVALID_SOCKET
#else
    fd < 0
#endif
  ) {
    throw ecul_erro(mkstr() << "Invalid socket fd=" << fd);
  }
  if (chunk_size == 0) throw ecul_erro(mkstr() << "Chunk size must be > 0");
  if (chunk_size >= std::numeric_limits<ssize_t>::max()/2) throw ecul_erro(mkstr() << "Chunk size is too large (with ssize_t)"); // just to stay extra away from overflow related problems
  if (chunk_size >= std::numeric_limits<int>::max()/2) throw ecul_erro(mkstr() << "Chunk size is too large (with int)"); // just to stay extra away from overflow related problems
  bool warn_since=0; // did we yet warned about timeout_since being in the past

  auto fmt_secs = [](double secs) { return ( std::ostringstream() << std::fixed << std::setprecision(4) << secs ).str(); };

  size_t total_written = 0;
  size_t iterations = 0;

  while (total_written < data.size()) {
    size_t to_write = std::min(chunk_size, data.size() - total_written);

#if defined(_WIN32)
    int n = ::send(fd, data.data() + total_written,
                   static_cast<int>(to_write), 0);
#else
    ssize_t n = ::write(fd, data.data() + total_written, to_write);
#endif

    if (n > 0) {
      total_written += static_cast<size_t>(n);
      iterations++;
    } else if (n == 0) {
      double elapsed_sec = now_elapsed(timeout_since, warn_since);
      throw ecul_erro(mkstr() << "Socket closed unexpectedly (written zero, n="<<n<<") after writing "
        << total_written << " bytes, elapsed=" << fmt_secs(elapsed_sec) << "s");
    } else {
#if defined(_WIN32)
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
        // Timeout on this call, no bytes written
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      } else if (err == WSAEINTR) {
        // Interrupted, safe to retry
      } else {
        double elapsed_sec = std::chrono::duration<double>(
          std::chrono::steady_clock::now() - timeout_since).count();
        throw ecul_erro(mkstr() << "Send error: WSAError=" << err
          << " after writing " << total_written << " bytes, elapsed=" << fmt_secs(elapsed_sec) << "s");
      }
#else
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Timeout on this call, no bytes written
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      } else if (errno == EINTR) {
        // Interrupted, safe to retry
      } else {
        double elapsed_sec = std::chrono::duration<double>(
          std::chrono::steady_clock::now() - timeout_since).count();
        throw ecul_erro(mkstr() << "Write error: errno=" << errno
          << " after writing " << total_written << " bytes, elapsed=" << fmt_secs(elapsed_sec) << "s");
      }
#endif
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - timeout_since);
    if (elapsed > timeout_max_ms) {
      double elapsed_sec = std::chrono::duration<double>(now - timeout_since).count();
      throw ecul_erro(mkstr() << "Global timeout exceeded: elapsed=" << fmt_secs(elapsed_sec)
        << "s, max=" << timeout_max_ms.count() << "ms, written=" << total_written << " bytes");
    }
  }

  auto end = std::chrono::steady_clock::now();
  double elapsed_sec = std::chrono::duration<double>(end - timeout_since).count();
  ecul_log_info(mkstr()<<"Completed write of " << data.size() << " bytes in "
    << iterations << " iterations, elapsed=" << fmt_secs(elapsed_sec) << "s");
}

} // namespace socket
} // namespace ecul
