
#include "RequestHandler.hpp"
#include "Logger.hpp"
#include "Enums.hpp"
#include "Client.hpp"
#include "Parser.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <map>
#include <vector>
#include <cstdio>
#include <iostream>
#include <filesystem>


void printRequest(const HTTPRequest &req)
{
    std::cout << req.method << " " << req.path << std::endl;
    for (std::map<std::string, std::string>::const_iterator it = req.headers.begin(); it != req.headers.end(); ++it) {
        std::cout << it->first << ": " << it->second << "\r\n";
    };
    std::cout <<"\r\n";
    std::cout << req.body << std::endl;

}

static std::string getFileExtension(const std::string& path)
{
    size_t dot = path.find_last_of('.');
    return (dot != std::string::npos) ? path.substr(dot) : "";
}

static std::string getMimeType(const std::string& ext)
{
    static std::map<std::string, std::string> types = {
        {".plain", "text/plain"},
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"}
    };
    return types.count(ext) ? types[ext] : "application/octet-stream";
}

static std::string extractFilename(const std::string& path, int method)
{
    // std::cout << "Extract path: " << path << std::endl;
    size_t start;
    if (method)
    {
        start = path.find("filename=\"");
        start += 10;
        if (start == std::string::npos)
            return "";
        size_t end = path.find("\"", start);
        return path.substr(start, end - start);
    }
    else
    {
        start = path.find_last_of('/');
        if (start == std::string::npos)
            return path;
        return path.substr(start + 1);
    }
}

static std::string extractContent(const std::string& part)
{
    size_t start = part.find("\r\n\r\n");
    size_t offset = 4;
    if (start == std::string::npos)
    {
        start = part.find("\n\n");
        offset = 2;
    }
    if (start == std::string::npos)
        return "";
    size_t conStart = start + offset;
    size_t conEnd = part.find_last_not_of("\r\n") + 1;
    if (conEnd <= conStart)
        return "";
    return part.substr(conStart, conEnd - conStart);
}

static std::vector<std::string> split(const std::string& s, const std::string& s2)
{
    std::vector<std::string> result;
    size_t pos = 0;
    while (true)
    {
        size_t start = s.find(s2, pos);
        if (start == std::string::npos)
            break;
        start += s2.length();
        while (start < s.size() && (s[start] == '-' || s[start] == '\r' || s[start] == '\n'))
            start++;
        size_t end = s.find(s2, start);
        std::string part = (end == std::string::npos) ? s.substr(start) : s.substr(start, end - start);
        while (!part.empty() && (part[0] == '\r' || part[0] == '\n'))
            part.erase(0, 1);
        while (!part.empty() && (part.back() == '\r' || part.back() == '\n'))
            part.pop_back();
        if (!part.empty())
            result.push_back(part);
        if (end == std::string::npos)
            break;
        pos = end;
    }
    return result;
}

HTTPResponse RequestHandler::executeCGI(Client& client)
{
    std::string key = client.request.path.substr(0, client.request.path.find_last_of("/") + 1);
    std::string path = "." + client.serverInfo.routes[key].abspath + client.request.file;
    std::cout << "CGI exe path: " << path << std::endl;
    if (access(path.c_str(), X_OK) != 0)
        return  HTTPResponse(403, "Forbidden");
    int inPipe[2], outPipe[2];
    pipe(inPipe);
    pipe(outPipe);
    pid_t pid = fork();
    if (pid == 0)
    {
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        close(inPipe[1]);
        close(outPipe[0]);
        setenv("REQUEST_METHOD", client.request.method.c_str(), 1);
        setenv("SCRIPT_NAME", client.request.file.c_str(), 1);
        setenv("CONTENT_LENGTH", std::to_string(client.request.body.size()).c_str(), 1);
        if (client.request.headers.count("Content-Type"))
            setenv("CONTENT_TYPE", client.request.headers.at("Content-Type").c_str(), 1);
        std::string pyth = "python3";
        char *argv[] = { const_cast<char*>(pyth.c_str()), const_cast<char*>(path.c_str()), NULL };
        execve(client.serverInfo.routes[key].cgipathpython.c_str(), argv, environ);
        _exit(1);
    }
    close(inPipe[0]);
    close(outPipe[1]);
    write(inPipe[1], client.request.body.c_str(), client.request.body.size());
    close(inPipe[1]);
    char buffer[4096];
    std::string output;
    ssize_t n;
    while ((n = read(outPipe[0], buffer, sizeof(buffer))) > 0)
    {
        output.append(buffer, n);
        std::cout << output << std::endl;
    }
    close(outPipe[0]);
    waitpid(pid, NULL, 0);
    std::string::size_type end = output.find("\r\n\r\n");
    if (end == std::string::npos)
        return HTTPResponse(500, "Invalid CGI output");
    std::string headers = output.substr(0, end);
    std::string body = output.substr(end + 4);
    HTTPResponse res(200, "OK");
    res.body = body;
    std::istringstream header(headers);
    std::string line;
    while (std::getline(header, line))
    {
        if (line.back() == '\r')
            line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos)
            res.headers[line.substr(0, colon)] = line.substr(colon + 2);
    }
    res.headers["Content-Length"] = std::to_string(res.body.size());
    return res;
}

HTTPResponse RequestHandler::handleMultipart(Client& client)
{
    std::string key = client.request.path.substr(0, client.request.path.find_last_of("/") + 1);
    std::cout << "Key: {" << key << "}" << std::endl;
    if (client.request.headers.count("Content-Type") == 0)
        return HTTPResponse(400, "Missing Content-Type");
    std::map<std::string, std::string>::const_iterator its = client.request.headers.find("Content-Type");
    std::string ct = its->second;
    if (its == client.request.headers.end())
        HTTPResponse response(400, "Invalid headers");
    std::string boundary;
    std::string::size_type pos = ct.find("boundary=");
    if (pos == std::string::npos)
        HTTPResponse response(400, "No boundary");
    boundary = ct.substr(pos + 9);
    if (!boundary.empty() && boundary[0] == '"')
        boundary = boundary.substr(1, boundary.find('"', 1) - 1);
    std::string bound_mark = "--" + boundary;
    std::vector<std::string> parts = split(client.request.body, bound_mark);
    std::string lastPath;
    for (std::vector<std::string>::iterator it = parts.begin(); it != parts.end(); ++it)
    {
        std::string& part = *it;
        // std::cout <<
        if (part.empty() || part == "--\r\n" || part == "--")
            continue;
        std::string file = extractFilename(part, 1);
        std::cout << "File: " << file << std::endl;
        if (file.empty())
            continue;
        std::string content = extractContent(part);
        std::string folder = client.serverInfo.routes[key].abspath;
        std::cout << "Folder: " << std::endl;
        if (folder[0] == '/')
            folder.erase(0, 1);
        std::string path = folder + "/" + file;
        lastPath = path;
        std::cout << "Last path: " << std::endl;
        std::ofstream out(path.c_str(), std::ios::binary);
        if (!out.is_open())
        {
            std::cout << "HERE" << std::endl;
            wslog.writeToLogFile(ERROR, "500 Failed to open file for writing", false);
            return HTTPResponse(500, "Failed to open file for writing");
        }
        out.write(content.c_str(), content.size());
        out.close();
    }
    if (lastPath.empty() || access(lastPath.c_str(), R_OK) != 0)
        return HTTPResponse(400, "File not uploaded");
    HTTPResponse res(200, "OK");
    res.body = "File(s) uploaded successfully\n";
    std::string ext = getFileExtension(client.request.path);
    res.headers["Content-Type"] = getMimeType(ext);
    res.headers["Content-Length"] = std::to_string(res.body.size());
    wslog.writeToLogFile(INFO, "POST (multi) File(s) uploaded successfully", false);
    return res;
}

HTTPResponse RequestHandler::handlePOST(Client& client)
{
    if (client.request.headers.count("Content-Type") == 0)
        return HTTPResponse(400, "Missing Content-Type");
    // std::cout << "Content type: " << req.headers["Content-Type"] << std::endl;
    if (client.request.headers["Content-Type"].find("multipart/form-data") != std::string::npos)
        return handleMultipart(client);
    std::string key = client.request.path.substr(0, client.request.path.find_last_of("/") + 1);
    std::cout << "Key: " << key << std::endl;
    std::string path = "." + client.serverInfo.routes[key].abspath + client.request.file;
    std::cout << "Path: " << path << std::endl;
    std::cout << "Client request file" << client.request.file << std::endl;
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out.is_open())
    {
        wslog.writeToLogFile(ERROR, "500 Failed to open file for writing", false);
        return HTTPResponse(500, "Failed to open file for writing");
    }
    out.write(client.request.body.c_str(), client.request.body.size());
    out.close();
    if (access(path.c_str(), R_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "400 File not uploaded", false);
        return HTTPResponse(400, "File not uploaded");
    }
    if (client.request.file.empty())
    {
        wslog.writeToLogFile(ERROR, "400 Bad request", false);
        return HTTPResponse(400, "Bad request");
    }
    HTTPResponse res(200, "OK");
    res.body = "File(s) uploaded successfully\n";
    std::string ext = getFileExtension(client.request.path);
    res.headers["Content-Type"] = getMimeType(ext);
    res.headers["Content-Length"] = std::to_string(res.body.size());
    wslog.writeToLogFile(INFO, "POST File(s) uploaded successfully", false);
    return res;
}

HTTPResponse RequestHandler::handleGET(Client& client)
{
    std::cout << client.request.path << std::endl;
    std::string key = client.request.path.substr(0, client.request.path.find_last_of("/") + 1);
    std::cout << "Key: " << key << std::endl;
    std::string path = "." + client.serverInfo.routes[key].abspath + client.request.file;
    std::cout << "Path: " << path << std::endl;
    if (path.find("..") != std::string::npos)
    {
        wslog.writeToLogFile(ERROR, "403 Forbidden", false);
        return HTTPResponse(403, "Forbidden");
    }
    if (path.find("/www/cgi/") != std::string::npos)
        return executeCGI(client);
    struct stat s;
    if (stat(path.c_str(), &s) != 0 || access(path.c_str(), R_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "404 Not Found", false);
        return HTTPResponse(404, "Not Found");
    }
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
    {
        wslog.writeToLogFile(ERROR, "500 Internal Server Error", false);
        return HTTPResponse(500, "Internal Server Error");
    }
    std::ostringstream content;
    content << file.rdbuf();
    file.close();
    HTTPResponse response(200, "OK");
    response.body = content.str();
    std::string ext = getFileExtension(path);
    response.headers["Content-Type"] = getMimeType(ext);
    response.headers["Content-Length"] = std::to_string(response.body.size());
    wslog.writeToLogFile(INFO, "GET File(s) downloaded successfully", false);
    return response;
}

HTTPResponse RequestHandler::handleDELETE(Client& client)
{
    // std::cout << "Req path: " << req.path << std::endl;
    std::string key = client.request.path.substr(0, client.request.path.find_last_of("/") + 1);
    // std::cout << "Key: " << key << std::endl;
    std::string fullPath = "." + client.serverInfo.routes[key].abspath + client.request.file;
    // std::cout << "Full path: " << fullPath << std::endl;
    if (fullPath.find("..") != std::string::npos || fullPath.find("/uploads/") == std::string::npos)
        return HTTPResponse(403, "Forbidden");
    if (access(fullPath.c_str(), F_OK) != 0)
        return HTTPResponse(404, "Not Found");
    if (remove(fullPath.c_str()) != 0)
        return HTTPResponse(500, "Delete Failed");
    HTTPResponse res(200, "OK");
    res.body = "File deleted successfully\n";
    res.headers["Content-Type"] = "text/plain";
    res.headers["Content-Length"] = std::to_string(res.body.size());
    wslog.writeToLogFile(INFO, "DELETE File deleted successfully", false);
    return res;
}


bool RequestHandler::isAllowedMethod(std::string method, Route route)
{
    // std::cout << route.accepted_methods.size() << std::endl;
    for (size_t i = 0; i < route.accepted_methods.size(); i++)
    {
        // std::cout << route.accepted_methods[i] << std::endl;
        if (method == route.accepted_methods[i])
            return true;
    }
    return false;
}

HTTPResponse RequestHandler::handleRequest(Client& client)
{
    // printRequest(req);
    // std::cout << "Req path: " << client.request.path << std::endl;
    std::string key = client.request.path.substr(0, client.request.path.find_last_of("/") + 1);
    // std::cout << "Key: " << key << std::endl;
    std::string fullPath = "." + client.serverInfo.routes[key].abspath + client.request.file;
    // std::cout << "Full path: " << fullPath << std::endl;
    bool validFile = false;
    try
    {
        validFile = std::filesystem::exists(fullPath);
    }
    catch(const std::exception& e)
    {
        wslog.writeToLogFile(ERROR, "Invalid file name", true);
        return HTTPResponse(400, "Invalid file name");
    }
    if (!isAllowedMethod(client.request.method, client.serverInfo.routes[key]))
        return HTTPResponse(400, "Method not allowed");
    switch (client.request.eMethod)
    {
        case GET:
        {
            if (validFile)
                return handleGET(client);
            else
                return HTTPResponse(400, "Invalid file");
        }
        case POST:
        {
            return handlePOST(client);
        }
        case DELETE:
        {
            return handleDELETE(client);
        }
        default:
            return HTTPResponse(501, "Not Implemented");
    }
}