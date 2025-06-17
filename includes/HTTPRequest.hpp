
#pragma once

#include "Parser.hpp"
#include <string>
#include <vector>
#include <map>

enum reqTypes
{
    GET,
    POST,
    DELETE,
    INVALID
};

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
        std::string tempFileName;
        int fileFd;
        bool fileUsed;
        bool fileIsOpen;
        bool isCGI;
        bool multipart;
        bool validHostName;
        HTTPRequest();
        HTTPRequest(std::string headers, ServerConfig server);
};