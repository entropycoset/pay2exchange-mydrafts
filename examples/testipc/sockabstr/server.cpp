#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <unistd.h>
#include <stdexcept>

using boost::asio::local::datagram_protocol;

int main() {
    try {
        boost::asio::io_context io;

        std::string name = std::string("\0myapp.") +
                           std::to_string(getuid()) + "." +
                           std::to_string(getpid());

        datagram_protocol::endpoint ep(name);
        datagram_protocol::socket sock(io);

        boost::system::error_code ec;
        sock.open();
        sock.bind(ep, ec);
        if (ec) {
            throw std::runtime_error("Failed to bind socket: " + ec.message());
        }

        std::cout << "myapp listening on abstract datagram socket: "
                  << name.substr(1) << "\n";

        char data[256];
        datagram_protocol::endpoint sender;

        while (true) {
            boost::system::error_code recv_ec;
            size_t n = sock.receive_from(boost::asio::buffer(data), sender, 0, recv_ec);
            if (recv_ec) {
                std::cerr << "Warning: receive error: " << recv_ec.message() << "\n";
                continue; // recoverable
            }

            std::string msg(data, n);
            std::string reply;

            if (msg.rfind("Hi;", 0) == 0) {
                auto pos = msg.find("USER=");
                if (pos != std::string::npos) {
                    auto end = msg.find(';', pos);
                    std::string uidstr = msg.substr(pos+5, end-(pos+5));
                    uid_t theiruid = std::stoi(uidstr);
                    uid_t myuid = getuid();

                    if (theiruid != myuid) {
                        reply = "Error;Not me I am USER=" +
                                std::to_string(myuid) +
                                " not " + std::to_string(theiruid);
                    } else {
                        reply = "Hello from myapp instance " +
                                std::to_string(getpid());
                    }
                }
            }

            if (!reply.empty()) {
                boost::system::error_code send_ec;
                sock.send_to(boost::asio::buffer(reply), sender, 0, send_ec);
                if (send_ec) {
                    std::cerr << "Warning: failed to send reply: "
                              << send_ec.message() << "\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}

