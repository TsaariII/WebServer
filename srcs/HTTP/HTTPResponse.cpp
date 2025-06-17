
#include "HTTPResponse.hpp"
#include <unordered_map>
#include <fstream>
#include <iostream>

HTTPResponse::HTTPResponse(int code, const std::string& msg, std::map<int, std::string> error_pages) : status(code), stat_msg(msg)
{
    if (code >= 300 && code <= 308) generateRedirectResponse(code, msg);
    if (code >= 400) generateErrorResponse(code, msg, error_pages);
}

std::string HTTPResponse::toString() const
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << stat_msg << "\r\n";
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
        response << it->first << ": " << it->second << "\r\n";
    response << "\r\n";
    if (!body.empty())
        response << body;
    return response.str();
}

int HTTPResponse::getStatusCode()
{
    return status;
}

std::string HTTPResponse::getStatusMessage()
{
    return stat_msg;
}

void HTTPResponse::generateRedirectResponse(int code,const std::string& newLocation)
{
    static std::unordered_map<int, std::string> statusMessages =
    {
        {301, "Moved Permanently"},
        {302, "Found"},
        {307, "Temporary Redirect"},
        {308, "Permanent Redirect"},
    };
    headers["Location"] = newLocation;
    body = "<html><head><title>" + std::to_string(code) + " " + stat_msg + "</title></head>"
           "<body><p>Redirecting to <a href=\"" + newLocation + "\">" + newLocation + "</a></p></body></html>";
    headers["Content-Type"] = "text/html";
    headers["Content-Length"] = std::to_string(body.size());
}

void HTTPResponse::generateErrorResponse(int code, const std::string& msg, std::map<int, std::string> error_pages)
{
        if (error_pages.find(code) != error_pages.end())
        {
            std::string filepath = "." + error_pages[code];
            std::ifstream file(filepath);
            char buffer[7000];
            file.read(buffer, 7000);
            body = std::string(buffer);
            headers["Content-Type"] = "text/html; charset=UTF-8";
            headers["Content-Length"] = std::to_string(body.size());
            return;
        }
        static std::unordered_map<int, std::string> statusMessages =
        {
            {400, "Bad Request"},
            {405, "Method Not Allowed"},
            {403, "Forbidden"},
            {404, "Not Found"},
            {408, "Request Timeout"},
            {413, "Payload Too Large"},
            {414, "URI Too Long"},
            {415, "Unsupported Media Type"},
            {422, "Unprocessable Entity"},
            {429, "Too Many Requests"},
            {431, "Request Header Fields Too Large"},
            {451, "Unavailable For Legal Reasons"},
            {500, "Internal Server Error"},
            {501, "Not Implemented"},
            {503, "Service Unavailable"}
        };

        std::string reason;
        if (statusMessages.find(code) != statusMessages.end())
            reason = statusMessages[code];
        else
            reason = "Unknown Error";
        body = "<html><head><title>" + std::to_string(code) + " " + reason +
            "</title></head><body><h1>" + std::to_string(code) + " " + reason +
            "</h1><p>The server encountered an error: " + msg + ".</p></body></html>";

        headers["Content-Type"] = "text/html; charset=UTF-8";
        headers["Content-Length"] = std::to_string(body.size());
}
