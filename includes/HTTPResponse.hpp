
#pragma once
#include <string>
#include <map>
#include <sstream>

class HTTPResponse
{
    private:
        int status;
        std::string stat_msg;
    public:
        HTTPResponse(int code = 200, const std::string& msg = "OK", std::map<int, std::string> error_pages = {{0, ""}});
        std::map<std::string, std::string> headers;
        std::string body;
        std::string toString() const;

        // Functions
        int getStatusCode();
        std::string getStatusMessage();
        void generateRedirectResponse(int code,const std::string& msg);
        void generateErrorResponse(int code, const std::string& msg, std::map<int, std::string> error_pages);
};