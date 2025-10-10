#ifndef LOOPBACKFINDER_H
#define LOOPBACKFINDER_H

// wibecoded
// minimal-none checking

#include <string>
#include <unordered_set>
#include <ostream>
#include <cstring>
#include <iostream>

class LoopbackFinder {
public:
    // Constructs finder that logs to the provided ostream (default std::cerr).
    explicit LoopbackFinder(std::ostream &logger = std::cerr);

    // Find first free 127.0.0.X where no socket exists with local port in [avoid_min, avoid_max].
    // If only_listening == true, only consider sockets that are actually listening (TCP LISTEN or UDP).
    // If only_listening == false, consider sockets in any state (including TIME_WAIT, CLOSE_WAIT, ESTABLISHED, etc.).
    // Returns X (1..254) on success, -1 if none found, throws on fatal errors.
    int find_free(int avoid_min, int avoid_max, bool only_listening);

private:
    std::ostream &log_;

    std::string run_and_capture(const std::string &cmd);
    void collect_from_ss(const std::string &ss_cmd, std::unordered_set<std::string> &ips, int port_lo, int port_hi);
    void collect_from_proc(const std::string &path, std::unordered_set<std::string> &ips,
                                  int port_lo, int port_hi, bool only_listening, bool is_tcp);
};

#endif // LOOPBACKFINDER_H

