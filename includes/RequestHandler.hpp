
#pragma once

#include "Client.hpp"
#include "HTTPResponse.hpp"
#include <string>
#include <vector>

class RequestHandler
{
    public:
        static HTTPResponse handleRequest(Client& client);
        static HTTPResponse handleMultipart(Client& client);
        static bool isAllowedMethod(std::string method, Route route);
    private:
        static HTTPResponse handleGET(Client& client, std::string fullPath);
        static HTTPResponse handlePOST(Client& client, std::string fullPath);
        static HTTPResponse handleDELETE(std::string fullPath, std::map<int, std::string> error_pages);
        static HTTPResponse redirectResponse(std::string fullPath);
};
