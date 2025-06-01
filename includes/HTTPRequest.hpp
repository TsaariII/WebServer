
#pragma once

#include "Enums.hpp"
#include "Parser.hpp"
#include <string>
#include <map>

class HTTPRequest
{
    private:
        void parser(std::string headers, ServerConfig server);

    public:
        std::string method;
        reqTypes eMethod;
        std::string path;
        std::string file;
        std::string version;
        std::string location;
        std::string query;
        std::string pathInfo;
        std::map<std::string, std::string> headers;
        std::string body;
        std::string tempBodyFile; // Temporary file for large bodies;
        bool isCGI;
        bool tempFileOpen; // Flag to check if temporary file is open
        int BodyFd; // File descriptor for the file
        HTTPRequest();
        HTTPRequest(std::string headers, ServerConfig server);
};