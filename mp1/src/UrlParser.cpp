#include "UrlParser.hpp"

int URLParser::parse(const std::string& url, URLInfo& info) {
    // resolve the protocol
    size_t protoEnd = url.find("://");
    if (protoEnd == std::string::npos) return -1;

    info.protocol = url.substr(0, protoEnd);

    size_t hostStart = protoEnd + 3;  // length of "://"
    size_t pathStart = url.find('/', hostStart);
    if (pathStart != std::string::npos) {
        info.path = url.substr(pathStart);
    }

    size_t portStart = url.find(':', hostStart);
    if (portStart != std::string::npos && (pathStart == std::string::npos || portStart < pathStart)) {
        info.port = url.substr(portStart + 1, pathStart - portStart - 1);
        info.host = url.substr(hostStart, portStart - hostStart);
    } else {
        // no port specified in url, use default
        info.host = url.substr(hostStart, pathStart - hostStart);
    }

    return 0;
}
