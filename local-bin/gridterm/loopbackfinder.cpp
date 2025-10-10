// wibecoded (or was)
// see the corresponding header (h/hpp) for copyright/author/notes

#include "loopbackfinder.h"

#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <cctype>
#include <regex>
#include <array>

#include <cstring> // required per user instruction

// Implementation

LoopbackFinder::LoopbackFinder(std::ostream &logger)
    : log_(logger)
{
    log_ << "LoopbackFinder constructed; logger attached." << std::endl;
}

int LoopbackFinder::find_free(int avoid_min, int avoid_max, bool only_listening) {
    if (avoid_min < 0 || avoid_max < 0 || avoid_min > avoid_max) {
        throw std::invalid_argument("invalid port range");
    }

    log_ << "Searching for first free 127.0.0.X excluding local ports "
         << avoid_min << "-" << avoid_max << " (only_listening=" << (only_listening ? "true" : "false") << ")" << std::endl;

    std::unordered_set<std::string> occupied;

    if (only_listening) {
        collect_from_ss("ss -ltn", occupied, avoid_min, avoid_max); // TCP listening
        collect_from_ss("ss -lun", occupied, avoid_min, avoid_max); // UDP sockets (listening semantics)
    } else {
        collect_from_ss("ss -ant", occupied, avoid_min, avoid_max); // all TCP states
        collect_from_ss("ss -anu", occupied, avoid_min, avoid_max); // all UDP
    }

    collect_from_proc("/proc/net/tcp", occupied, avoid_min, avoid_max, only_listening, /*is_tcp=*/true);
    collect_from_proc("/proc/net/tcp6", occupied, avoid_min, avoid_max, only_listening, /*is_tcp=*/true);
    collect_from_proc("/proc/net/udp", occupied, avoid_min, avoid_max, only_listening, /*is_tcp=*/false);
    collect_from_proc("/proc/net/udp6", occupied, avoid_min, avoid_max, only_listening, /*is_tcp=*/false);

    log_ << "Total distinct local IPs with matching sockets in range: " << occupied.size() << std::endl;

    for (int x = 1; x <= 254; ++x) {
        std::string candidate = "127.0.0." + std::to_string(x);
        log_ << "Checking " << candidate << " ..." << std::endl;
        if (occupied.find(candidate) != occupied.end()) {
            log_ << candidate << " has socket(s) in port range; skipping" << std::endl;
            continue;
        }
        log_ << "Found free address: " << candidate << " -> X = " << x << std::endl;
        return x;
    }

    log_ << "No free 127.0.0.X found in range 1..254 for ports " << avoid_min << "-" << avoid_max << std::endl;
    return -1;
}

std::string LoopbackFinder::run_and_capture(const std::string &cmd) {
    std::string result;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed");
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) result += buf;
    int rc = pclose(pipe);
    if (rc == -1) throw std::runtime_error("pclose failed");
    return result;
}

void LoopbackFinder::collect_from_ss(const std::string &ss_cmd, std::unordered_set<std::string> &ips, int port_lo, int port_hi) {
    log_ << "Running: " << ss_cmd << std::endl;
    std::string out = run_and_capture(ss_cmd + " 2>/dev/null");
    std::istringstream in(out);
    std::string line;
    std::regex ipport_re(R"((\d{1,3}(?:\.\d{1,3}){3}):(\d+))");
    while (std::getline(in, line)) {
        auto it = std::sregex_iterator(line.begin(), line.end(), ipport_re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            std::smatch m = *it;
            std::string ip = m[1].str();
            int port = std::stoi(m[2].str());
            if (port >= port_lo && port <= port_hi) {
                ips.insert(ip);
                log_ << "Detected (ss) socket on " << ip << ":" << port << std::endl;
            }
        }
    }
}

void LoopbackFinder::collect_from_proc(const std::string &path, std::unordered_set<std::string> &ips,
                                       int port_lo, int port_hi, bool only_listening, bool is_tcp) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[1024];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; } // skip header
    while (fgets(line, sizeof(line), f)) {
        unsigned int sl;
        char local[128], rem[128];
        unsigned int state;
        if (sscanf(line, "%u: %127s %127s %x", &sl, local, rem, &state) < 4) continue;
        if (is_tcp && only_listening) {
            if (state != 0x0A) continue;
        }
        char *p = strchr(local, ':');
        if (!p) continue;
        *p = '\0';
        const char *iphex = local;
        const char *porthex = p + 1;
        unsigned int pnum = 0;
        if (sscanf(porthex, "%x", &pnum) != 1) continue;
        if ((int)pnum < port_lo || (int)pnum > port_hi) continue;
        if (strlen(iphex) < 8) continue;
        unsigned int b0,b1,b2,b3;
        if (sscanf(iphex, "%2x%2x%2x%2x", &b0,&b1,&b2,&b3) != 4) continue;
        char ipbuf[64];
        snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", b3, b2, b1, b0);
        ips.insert(std::string(ipbuf));
        log_ << "Detected (proc) socket on " << ipbuf << ":" << pnum << " from " << path
             << " (state=0x" << std::hex << state << std::dec << ")" << std::endl;
    }
    fclose(f);
}

// main
int testdemo_main(int argc, char **argv) {
    try {
        int avoid_min = 3000;
        int avoid_max = 4000;
        bool only_listening = false;
        if (argc >= 3) {
            avoid_min = std::atoi(argv[1]);
            avoid_max = std::atoi(argv[2]);
        }
        if (argc >= 4) {
            only_listening = std::atoi(argv[3]) != 0;
        }

        LoopbackFinder finder(std::cerr);
        int x = finder.find_free(avoid_min, avoid_max, only_listening);
        if (x >= 1) {
            std::cout << x << std::endl;
            return 0;
        } else {
            return 2;
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 3;
    }
}

