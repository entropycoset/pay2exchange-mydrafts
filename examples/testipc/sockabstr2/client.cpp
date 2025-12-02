#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <ctime>
#include <stdexcept>

const char* g_server_name = "myapp";

using boost::asio::local::datagram_protocol;

bool is_myapp_process(const std::string& pid, uid_t myuid) {
		std::cout<<"Scanning process pid="<<pid<<"\n";
    std::ifstream comm("/proc/" + pid + "/comm");
    std::string name;
    if (!(comm >> name)) return false;
    if (name !=  g_server_name) return false;


		std::cout<<"Scanning process pid="<<pid<<"  MATCHING NAME?\n";

    std::ifstream status("/proc/" + pid + "/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("Uid:", 0) == 0) {
            std::istringstream iss(line.substr(4));
            uid_t real;
            iss >> real;
						std::cout<<"Scan... real="<<real<<"  myuid="<<myuid<<"\n";
            return (real == myuid);
        }
    }
    return false;
}

bool try_talk(boost::asio::io_context& io, uid_t uid, const std::string& pid) {
    datagram_protocol::socket sock(io);
    boost::system::error_code ec;

    // FIXED: open with protocol + error_code
    sock.open(datagram_protocol(), ec);
    if (ec) {
        throw std::runtime_error("Failed to open socket: " + ec.message());
    }

    // Bind client socket to a unique abstract address so server can send replies
    std::string client_name = std::string("\0client.") + std::to_string(uid) + "." + std::to_string(getpid()) + "." + std::to_string(std::time(nullptr));
    datagram_protocol::endpoint client_ep(client_name);
    sock.bind(client_ep, ec);
    if (ec) {
        std::cerr << "Warning: failed to bind client socket: " << ec.message() << "\n";
        return false;
    }

    std::string server_name = std::string("\0myapp.") + std::to_string(uid) + "." + pid;
    datagram_protocol::endpoint server_ep(server_name);

    std::string msg = "Hi;USER=" + std::to_string(uid) + ";";
    sock.send_to(boost::asio::buffer(msg), server_ep, 0, ec);
    if (ec) {
        std::cerr << "Warning: failed to send: " << ec.message() << "\n";
        return false;
    }

    char buf[256];
    datagram_protocol::endpoint sender;
    sock.non_blocking(true);

    auto start = std::time(nullptr);
    while (std::difftime(std::time(nullptr), start) < 10) {
        boost::system::error_code recv_ec;
        size_t n = sock.receive_from(boost::asio::buffer(buf), sender, 0, recv_ec);
        if (!recv_ec && n > 0) {
            std::string reply(buf, n);
            std::cout << "Reply from PID " << pid << ": " << reply << "\n";
            return true;
        }
        usleep(100000); // 0.1s
    }
    std::cerr << "Warning: timeout waiting for reply from PID " << pid << "\n";
    return false;
}


int main() {
    try {
        uid_t myuid = getuid();
        auto start = std::time(nullptr);

        boost::asio::io_context io;

        while (std::difftime(std::time(nullptr), start) < 30) {
            DIR* d = opendir("/proc");
            if (!d) throw std::runtime_error("Failed to open /proc");

            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                if (!isdigit(ent->d_name[0])) continue;
                std::string pid = ent->d_name;
                if (is_myapp_process(pid, myuid)) {
                    if (try_talk(io, myuid, pid)) {
                        closedir(d);
                        return 0; // success
                    }
                }
            }
            closedir(d);
            sleep(1);
        }

        std::cerr << "Failed to connect within 30 seconds.\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}

