#ifndef URLPARSER_HPP
#define URLPARSER_HPP

#include <string>

struct URLInfo {
    std::string protocol;
    std::string host;
    std::string port = "80";
    std::string path = "/";
};

class URLParser {
public:
    static int parse(const std::string& url, URLInfo& info);
};

#endif
