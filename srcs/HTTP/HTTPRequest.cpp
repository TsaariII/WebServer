#include "HTTPRequest.hpp"
#include "Logger.hpp"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>

HTTPRequest::HTTPRequest() 
{
    method = "";
    path = "";
    version = "";
    file = "";
    eMethod = INVALID;
    pathInfo = "";
    isCGI = false;
    fileUsed = false;
    fileIsOpen = false;
    validHostName = true;
    multipart = false;
    fileFd = -1;
    query = "";
    multipart = false;
}

HTTPRequest::HTTPRequest(std::string headers, ServerConfig server)
{
    method = "";
    path = "";
    version = "";
    file = "";
    eMethod = INVALID;
    pathInfo = "";
    isCGI = false;
    fileUsed = false;
    fileIsOpen = false;
    validHostName = true;
    multipart = false;
    fileFd = -1;
    query = "";
    multipart = false;
    parser(headers, server);
}

reqTypes getMethodEnum(const std::string& method)
{
    if (method == "GET") return GET;
    if (method == "POST") return POST;
    if (method == "DELETE") return DELETE;
    return INVALID;
}

static std::string hexToAscii(std::string str)
{
    std::stringstream ss(str);
    int value;
    ss >> std::hex >> value;
    return std::string(1, static_cast<char>(value));
}

static void decode(std::string& raw)
{
    for (size_t i = 0; i < raw.size(); i++)
    {
        if (raw[i] == '%')
        {
            std::string temp = raw.substr(i + 1,  2);
            raw.erase(i, 3);
            temp = hexToAscii(temp);
            raw.insert(i, temp);
        }
    }
}

void HTTPRequest::parser(std::string raw, ServerConfig server)
{
    isCGI = false;
    decode(raw);
    std::istringstream stream(raw);
    std::string line;
    if (!std::getline(stream, line))
        return ;
    if (line.back() == '\r')
        line.pop_back();
    std::istringstream request_line(line);
    request_line >> method >> path >> version;
    eMethod = getMethodEnum(method);
    if (path.back() != '/')
    {
        std::string test_location = path + "/";
        if (server.routes.find(test_location) != server.routes.end())
            path += '/';
        else
            file = path.substr(path.find_last_of("/") + 1);
    }
    while (std::getline(stream, line))
    {
        if (line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            key.erase(std::remove_if(key.begin(), key.end(), [](char c){ return (c == ' ' || c == '\n' || c == '\r' ||
                c == '\t' || c == '\v' || c == '\f');}), key.end());

            value.erase(std::remove_if(value.begin(), value.end(), [](char c){ return (c == ' ' || c == '\n' || c == '\r' ||
                    c == '\t' || c == '\v' || c == '\f');}), value.end());
            auto result = headers.insert({key, value});
            if (result.second == false)
            {
                key = "Duplicate";
                value = "Key";
                headers.insert({key, value});
            }
        }
        else
        {
            std::string key = "Invalid";
            std::string value = "Format";
            headers.insert({key, value});
        }
    }
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos)
    {
        query = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
    }
    int pos1 = path.find_first_of("/");
    int pos2 = path.find_last_of("/");
    std::string locationfrompath;
    if (pos1 == pos2)
        locationfrompath = "/";
    else
        locationfrompath = path.substr(pos1, 1 + pos2 - pos1);
    for (auto it = server.routes.begin(); it != server.routes.end(); ++it)
    {
        if (it->first == locationfrompath)
        {
            location = it->first;
            file = path.substr(0 + location.size());
        }
    }
    if (headers.find("Content-Type") != headers.end())
    {
        if (headers.at("Content-Type").find("multipart/form-data") != std::string::npos)
        {
            fileUsed = true;
            multipart = true;   
        }
    }
    if (server.routes.find(location) != server.routes.end())
    {
        if (!server.routes.at(location).cgiexecutable.empty())
        {
            std::filesystem::path filePath = file;
            std::string ext = filePath.extension().string();
            if (std::find(server.routes.at(location).cgi_extension.begin(), server.routes.at(location).cgi_extension.end(), ext) != server.routes.at(location).cgi_extension.end())
            {
                if (std::find(server.routes.at(location).cgi_methods.begin(), server.routes.at(location).cgi_methods.end(), method) != server.routes.at(location).cgi_methods.end())
                    isCGI = true;
                else
                    wslog.writeToLogFile(ERROR, "Method not allowed for CGI: " + method + " in location: " + location, DEBUG_LOGS);
            }
        }
    }
}
