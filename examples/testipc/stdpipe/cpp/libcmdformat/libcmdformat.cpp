#include "libcmdformat.hpp"

namespace libcmdformat {

std::string encode_command(const std::string& command, CmdFormat format) {
    switch (format) {
        case CmdFormat::cmdformat_raw:
            return encode_raw(command);
        case CmdFormat::cmdformat_v1lenend:
            return encode_v1lenend(command);
        default:
            throw std::runtime_error("Unknown command format for encoding");
    }
}

std::string encode_raw(const std::string& command) {
    return command + "\n";
}

std::string encode_v1lenend(const std::string& command) {
    return std::to_string(command.length()) + ";" + command + ";END";
}

} // namespace libcmdformat