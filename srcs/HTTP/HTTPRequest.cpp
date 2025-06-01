
#include "HTTPRequest.hpp"
#include "Enums.hpp"
#include "Logger.hpp"
#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>

HTTPRequest::HTTPRequest() {}

HTTPRequest::HTTPRequest(std::string headers, ServerConfig server)
{
    tempBodyFile = "/tmp/webserv_temp_body_file";
    tempFileOpen = false;
    BodyFd = -1; // Initialize BodyFd to -1
    isCGI = false;
    parser(headers, server);
}

reqTypes getMethodEnum(const std::string& method)
{
    if (method == "GET") return GET;
    if (method == "POST") return POST;
    if (method == "DELETE") return DELETE;
    return INVALID;
}

void HTTPRequest::parser(std::string raw, ServerConfig server)
{
    isCGI = false;
    // wslog.writeToLogFile(DEBUG, "Raw: " + raw, true);
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
    // wslog.writeToLogFile(DEBUG, "File: " + file, true);
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
            headers[key] = value;
        }
        // this needs else?
    }
    // In the path there should be the key of the location and it should be the longest key
    // For example you could have key "/" and "/directory/"
    // the matched one should be the longest so "/directory/"
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos)
    {
        query = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
    }
    std::vector<std::string> matches;
    for (auto it = server.routes.begin(); it != server.routes.end(); ++it)
    {
        if (path.find(it->first) != std::string::npos)
            matches.push_back(it->first);
    }
    auto it = std::max_element(matches.begin(), matches.end(), [](const std::string& a, const std::string& b) {
        return a.length() < b.length();
    });
    if (it == matches.end())
        location = "";
    else
    {
        location = *it;
        /// file path should be the left over after location. For example "/directory/olalala/file.txt"
        /// then file is /olalala/file.txt
        file = path.substr(0 + location.size());
    }
    // wslog.writeToLogFile(DEBUG, "Parser location is: " + location, true);
    if (server.routes.find(location) != server.routes.end())
    {
        if (!server.routes.at(location).cgiexecutable.empty())
        {
            std::filesystem::path filePath = file;
            std::string ext = filePath.extension().string();
            // wslog.writeToLogFile(DEBUG, "filepath extension is: " + ext, true);
            // wslog.writeToLogFile(DEBUG, "filepath extension is in vector: " + server.routes.at(location).cgi_extension.at(0), true);
            if (std::find(server.routes.at(location).cgi_extension.begin(), server.routes.at(location).cgi_extension.end(), ext) != server.routes.at(location).cgi_extension.end())
            {
                // wslog.writeToLogFile(DEBUG, "Setting isCGI true: " + location, true);
                if (std::find(server.routes.at(location).cgi_methods.begin(), server.routes.at(location).cgi_methods.end(), method) != server.routes.at(location).cgi_methods.end())
                    isCGI = true;
                else
                    wslog.writeToLogFile(INFO, "Method not allowed for CGI: " + method + " in location: " + location, true);
            }
        }
    }
}
