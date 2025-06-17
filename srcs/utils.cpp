#include "utils.hpp"
#include "Logger.hpp"
#include <filesystem>
#include <string>
#include <vector>

std::atomic<int> signum = 0;

std::string joinPaths(std::filesystem::path path1, std::filesystem::path path2)
{
    return path1 / path2;
}

void handleSignals(int signal) 
{
    wslog.writeToLogFile(ERROR, "Signal received: " + std::to_string(signal), DEBUG_LOGS);
    if (signal == SIGPIPE)
        signal = 0;
    else if (signal == SIGINT)
        signum = signal; 
    return ;
}

bool validateHeader(HTTPRequest req)
{
    //check if request line is valid
    if (req.method.empty() || req.path.empty() || req.version.empty())
        return false;

    //if HTTP/1.1 must have host header
    if (req.version == "HTTP/1.1")
    {
        auto it = req.headers.find("Host");
        if (it == req.headers.end())
            return false;
    }

    //check if there were headers without colons
    auto it = req.headers.find("Invalid");
    if (it != req.headers.end())
    {
        if (it->second == "Format")
            return false;
    }

    //check if a key appears many times in the headers
    it = req.headers.find("Duplicate");
    if (it != req.headers.end())
    {
        if (it->second == "Key")
            return false;
    }

    //if method is POST, check if transfer-encoding exist. if so, it must be chunked and content-length must not exist
    if (req.method == "POST")
    {
        bool transferEncoding = false;
        bool contentLength = false;
        auto it = req.headers.find("Transfer-Encoding");
        if (it != req.headers.end())
        {
            transferEncoding = true;
            if (it->second != "chunked")
                return false;
        }
        it = req.headers.find("Content-Length");
        if (it != req.headers.end())
            contentLength = true;

        if ((transferEncoding == false && contentLength == false)
            || (transferEncoding == true && contentLength == true))
            return false;
    }
    return true;
}

std::vector<std::string> split(const std::string& s, const std::string& s2)
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

std::string extractFilename(const std::string& path, int method)
{
    size_t start;
    if (method)
    {
        size_t start = path.find("filename=\"");
        if (start == std::string::npos)
            return "";
        start += 10;
        size_t end = path.find("\"", start);
        if (end == std::string::npos)
            return "";
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

std::string extractContent(const std::string& part)
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

std::string getFileExtension(const std::string& path)
{
    size_t dot = path.find_last_of('.');
    return (dot != std::string::npos) ? path.substr(dot) : "";
}