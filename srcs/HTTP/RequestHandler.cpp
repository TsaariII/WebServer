#include "RequestHandler.hpp"
#include "utils.hpp"
#include "Logger.hpp"
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
#include <dirent.h>

void printRequest(const HTTPRequest &req)
{
    std::cout << req.method << " " << req.path << std::endl;
    for (auto it = req.headers.begin(); it != req.headers.end(); ++it) {
        std::cout << it->first << ": " << it->second << "\r\n";
    };
    std::cout <<"\r\n";
    std::cout << req.body << std::endl;
}

static std::string getMimeType(const std::string& ext)
{
    static std::map<std::string, std::string> types = {
        {".aac", "audio/aac"}, {".abw", "application/x-abiword"},
        {".apng", "image/apng"}, {".arc", "application/x-freearc"},
        {".avif", "image/avif"}, {".avi", "video/x-msvideo"},
        {".azw", "application/vnd.amazon.ebook"}, {".bin", "application/octet-stream"},
        {".bmp", "image/bmp"}, {".bz", "application/x-bzip"},
        {".bz2", "application/x-bzip2"}, {".cda", "application/x-cdf"},
        {".csh", "application/x-csh"}, {".css",	"text/css"},
        {".csv", "text/csv"}, {".doc", "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".eot", "application/vnd.ms-fontobject"}, {".epub", "application/epub+zip"},
        {".gz", "application/gzip"}, {".gif", "image/gif"},
        {".htm", "text/html"}, {".html", "text/html"},
        {".ico", "image/vnd.microsoft.icon"}, {".ics", "text/calendar"},
        {".jar", "application/java-archive"}, {".jpeg", "image/jpeg"},
        {".jpg", "image/jpeg"}, {".js","text/javascript"},
        {".json", "application/json"}, {".jsonld", "application/ld+json"},
        {".md", "text/markdown"},
        {".mid", "audio/x-midi"}, {".midi",	"audio/x-midi"},
        {".mjs", "text/javascript"}, {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"}, {".mpeg", "video/mpeg"},
        {".mpkg", "application/vnd.apple.installer+xml"}, {".odp", "application/vnd.oasis.opendocument.presentation"},
        {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
        {".odt", "application/vnd.oasis.opendocument.text"},
        {".oga", "audio/ogg"}, {".ogv", "video/ogg"},
        {".ogx", "application/ogg"}, {".opus", "audio/ogg"},
        {".otf", "font/otf"}, {".png", "image/png"},
        {".pdf", "application/pdf"}, {".php", "application/x-httpd-php"},
        {".ppt", "application/vnd.ms-powerpoint"}, {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {".rar", "application/vnd.rar"}, {".rtf", "application/rtf"},
        {".sh", "application/x-sh"}, {".svg", "image/svg+xml"}, 
        {".tar", "application/x-tar"}, {".tif", "image/tiff"},
        {".tiff", "image/tiff"}, {".ts", "video/mp2t"},
        {".ttf", "font/ttf"}, {".txt", "text/plain"},
        {".vsd", "application/vnd.visio"}, {".wav", "audio/wav"},
        {".weba", "audio/webm"}, {".webm", "video/webm"},
        {".webp", "image/webp"}, {".woff", "font/woff"},
        {".woff2", "font/woff2"}, {".xhtml", "application/xhtml+xml"},
        {".xls", "application/vnd.ms-excel"}, {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".xml", "application/xml"}, {".xul", "application/vnd.mozilla.xul+xml"},
        {".zip", "application/zip"}, {".3gp", "video/3gpp"},
        {".3g2", "video/3gpp2"}, {".7z", "application/x-7z-compressed"}
    };
    return types.count(ext) ? types[ext] : "application/octet-stream";
}

HTTPResponse generateSuccessResponse(std::string body, std::string type)
{
    HTTPResponse response(200, "OK");
    response.body = body;
    response.headers["Content-Type"] = type;
    response.headers["Content-Length"] = std::to_string(response.body.size());
    return response;
}

static HTTPResponse generateIndexListing(std::string fullPath, std::string location, Client &client)
{
    DIR* dir = opendir(fullPath.c_str());
    if (!dir)
    {
        wslog.writeToLogFile(ERROR, "500 Failed to open directory", DEBUG_LOGS);
        return HTTPResponse(500, "Failed to open directory", client.serverInfo.error_pages);
    }
    std::stringstream html;
    html << "<html><head><title>" << location << "</title></head><body>\n";
    html << "<h1 style=\"font-family:sans-serif\">" << location << "</h1><ul>\n";
    html << "<table cellpadding=\"5\" cellspacing=\"0\" style=\"text-align: left; font-family: sans-serif\">\n";
    html << "<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\n";
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        std::string name = entry->d_name;
        if (name =="." || name == "..")
            continue ;
        std::string ref = location;
        if (!ref.empty() && ref.back() != '/')
            ref += '/';
        ref += name;
        struct stat st;
        std::string fullEntry = fullPath + "/" + name;
        if (stat(fullEntry.c_str(), &st) == -1)
            continue;
        std::string displayedName = name;
        if (displayedName.length() > 15)
            displayedName = displayedName.substr(0, 12) + "...";
        if (entry->d_type == DT_DIR)
        {
            ref += "/";
            displayedName += "/";
        }
        char time[64];
        std::strftime(time, sizeof(time), "%Y-%m-%d %H:%M", std::localtime(&st.st_mtime));
        std::string size = (S_ISDIR(st.st_mode)) ? "-" : std::to_string(st.st_size) + " B";
        html << "<tr><td><a href=\"" << ref << "\">" << displayedName << "</a></td>"
             << "<td>" << time << "</td>"
             << "<td>" << size << "</td></tr>\n";
    }
    html << "</table></body></html>\n";
    closedir(dir);
    wslog.writeToLogFile(INFO, "GET Index listing successful", DEBUG_LOGS);
    return generateSuccessResponse(html.str(), "text/html");
}

HTTPResponse RequestHandler::handleMultipart(Client& client)
{
    if (client.request.headers.count("Content-Type") == 0)
    {
        wslog.writeToLogFile(ERROR, "400 Missing Content-Type", DEBUG_LOGS);
        return HTTPResponse(400, "Missing Content-Type", client.serverInfo.error_pages);
    }
    auto its = client.request.headers.find("Content-Type");
    std::string ct = its->second;
    if (its == client.request.headers.end())
    {
        wslog.writeToLogFile(ERROR, "400 Invalid headers", DEBUG_LOGS);
        return HTTPResponse(400, "Invalid headers", client.serverInfo.error_pages);
    }
    std::string boundary;
    std::string::size_type pos = ct.find("boundary=");
    if (pos == std::string::npos)
    {
        wslog.writeToLogFile(ERROR, "400 No boundary", DEBUG_LOGS);
        return HTTPResponse (400, "No boundary");
    }
    boundary = ct.substr(pos + 9);
    if (!boundary.empty() && boundary[0] == '"')
        boundary = boundary.substr(1, boundary.find('"', 1) - 1);
    std::string bound_mark = "--" + boundary;
    std::vector<std::string> parts = split(client.request.body, bound_mark);
    std::string lastPath;
    for (auto it = parts.begin(); it != parts.end(); ++it)
    {
        std::string& part = *it;
        if (part.empty() || part == "--\r\n" || part == "--")
            continue;
        std::string file = extractFilename(part, 1);
        if (file.empty())
            continue;
        std::string content = extractContent(part);
        std::string folder = client.serverInfo.routes[client.request.location].abspath;
        std::string path = folder;
        if (path.back() != '/')
            path += "/";
        path += file;
        lastPath = "." + path;
        std::ofstream out(lastPath.c_str(), std::ios::binary);
        if (!out.is_open())
        {
            wslog.writeToLogFile(ERROR, "500 Failed to open file for writing", DEBUG_LOGS);
            return HTTPResponse(500, "Failed to open file for writing", client.serverInfo.error_pages);
        }
        out.write(content.c_str(), content.size());
        out.close();
    }
    if (lastPath.empty() || access(lastPath.c_str(), R_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "400 File not uploaded", DEBUG_LOGS);
        return HTTPResponse(400, "File not uploaded", client.serverInfo.error_pages);
    }
    std::string ext = getFileExtension(client.request.path);
    wslog.writeToLogFile(INFO, "POST (multi) File(s) uploaded successfully", DEBUG_LOGS);
    return generateSuccessResponse("File(s) uploaded successfully\n", getMimeType(ext));
}

HTTPResponse RequestHandler::handlePOST(Client& client, std::string fullPath)
{
    if (client.request.headers.count("Content-Type") == 0)
    {
        wslog.writeToLogFile(ERROR, "400 Missing Content-Type", DEBUG_LOGS);
        return HTTPResponse(400, "Missing Content-Type", client.serverInfo.error_pages);
    }
    if (client.request.headers["Content-Type"].find("multipart/form-data") != std::string::npos)
        return handleMultipart(client);
    std::ofstream out(fullPath.c_str(), std::ios::binary);
    if (!out.is_open())
    {
        wslog.writeToLogFile(ERROR, "500 Failed to open file for writing", DEBUG_LOGS);
        return HTTPResponse(500, "Failed to open file for writing", client.serverInfo.error_pages);
    }
    out.write(client.request.body.c_str(), client.request.body.size());
    out.close();
    if (access(fullPath.c_str(), R_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "400 File not uploaded", DEBUG_LOGS);
        return HTTPResponse(400, "File not uploaded", client.serverInfo.error_pages);
    }
    if (client.request.file.empty())
    {
        wslog.writeToLogFile(ERROR, "400 Bad request", DEBUG_LOGS);
        return HTTPResponse(400, "Bad request", client.serverInfo.error_pages);
    }
    std::string ext = getFileExtension(client.request.path);
    wslog.writeToLogFile(INFO, "POST File(s) uploaded successfully", DEBUG_LOGS);
    return generateSuccessResponse("File(s) uploaded successfully\n", getMimeType(ext));
}

HTTPResponse RequestHandler::handleGET(Client& client, std::string fullPath)
{
    struct stat s;
    if (stat(fullPath.c_str(), &s) != 0 || access(fullPath.c_str(), R_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "404 Not Found", DEBUG_LOGS);
        return HTTPResponse(404, "Not Found", client.serverInfo.error_pages);
    }
    bool isDir = S_ISDIR(s.st_mode);
    if (isDir == true)
    {
        if (!client.serverInfo.routes.at(client.request.location).index_file.empty())
        {
            fullPath = joinPaths(fullPath, client.serverInfo.routes.at(client.request.location).index_file);
            std::ifstream file(fullPath.c_str(), std::ios::binary);
            if (!file.is_open())
            {
                wslog.writeToLogFile(ERROR, "404, Not Found", false);
                return HTTPResponse(404, "Not Found");
            }
            std::ostringstream content;
            content << file.rdbuf();
            file.close();
            std::string ext = getFileExtension(fullPath);
            wslog.writeToLogFile(INFO, "GET File(s) downloaded successfully", false);
            return generateSuccessResponse(content.str(), getMimeType(ext));
        }
        else
        {
            if (client.serverInfo.routes.at(client.request.location).autoindex)
                return generateIndexListing(fullPath, client.request.location, client);
            else
            {
                wslog.writeToLogFile(ERROR, "404, Not Found", false);
                return HTTPResponse(404, "Not Found");
            }
        }
    }
    std::ifstream file(fullPath.c_str(), std::ios::binary);
    if (!file.is_open())
    {
        wslog.writeToLogFile(ERROR, "500 Internal Server Error", DEBUG_LOGS);
        return HTTPResponse(500, "Internal Server Error", client.serverInfo.error_pages);
    }
    std::ostringstream content;
    content << file.rdbuf();
    file.close();
    std::string ext = getFileExtension(fullPath);
    wslog.writeToLogFile(INFO, "GET File(s) downloaded successfully", DEBUG_LOGS);
    return generateSuccessResponse(content.str(), getMimeType(ext));
}

HTTPResponse RequestHandler::handleDELETE(std::string fullPath, std::map<int, std::string> error_pages)
{
    if (access(fullPath.c_str(), F_OK) != 0)
        return HTTPResponse(404, "Not Found", error_pages);
    if (remove(fullPath.c_str()) != 0)
        return HTTPResponse(500, "Delete Failed", error_pages);
    wslog.writeToLogFile(INFO, "DELETE File deleted successfully", DEBUG_LOGS);
    return generateSuccessResponse("File deleted successfully\n", "text/plain");
}


bool RequestHandler::isAllowedMethod(std::string method, Route route)
{
    for (size_t i = 0; i < route.accepted_methods.size(); i++)
    {
        if (method == route.accepted_methods[i])
            return true;
    }
    return false;
}

HTTPResponse RequestHandler::redirectResponse(std::string fullPath)
{
    fullPath += "/";
    HTTPResponse res(301, "Moved Permanently");
    res.headers["Location"] = fullPath;
    return res;
}

HTTPResponse RequestHandler::handleRequest(Client& client)
{
    for (size_t i = 0; i < client.request.file.size(); i++)
    {
        if (isspace(client.request.file[i]))
            return HTTPResponse(403, "Whitespace in filename", client.serverInfo.error_pages);
    }
    if (client.request.path.find("..") != std::string::npos)
    {
        wslog.writeToLogFile(ERROR, "403 Forbidden", DEBUG_LOGS);
        return HTTPResponse(403, "Forbidden", client.serverInfo.error_pages);
    }
    std::string fullPath = "." + joinPaths(client.serverInfo.routes[client.request.location].abspath, client.request.file);
    bool validFile = false;
    try
    {
        validFile = std::filesystem::exists(fullPath);
    }
    catch(const std::exception& e)
    {
        wslog.writeToLogFile(ERROR, "Invalid file name", DEBUG_LOGS);
        return HTTPResponse(404, "Invalid file name", client.serverInfo.error_pages);
    }
    if (!validFile)
    {
        wslog.writeToLogFile(ERROR, "Invalid file", DEBUG_LOGS);
        return HTTPResponse(404, "Invalid file", client.serverInfo.error_pages);
    }
    if (fullPath != "." && std::filesystem::is_regular_file(fullPath) == false && std::filesystem::is_directory(fullPath) && fullPath.back() != '/')
        return redirectResponse(client.request.file);
    if (!isAllowedMethod(client.request.method, client.serverInfo.routes[client.request.location]))
        return HTTPResponse(405, "Method not allowed", client.serverInfo.error_pages);
    switch (client.request.eMethod)
    {
        case GET:
            return handleGET(client, fullPath);
        case POST:
            return handlePOST(client, fullPath);
        case DELETE:
            return handleDELETE(fullPath, client.serverInfo.error_pages);
        default:
            return HTTPResponse(501, "Not Implemented", client.serverInfo.error_pages);
    }
}