#pragma once
#include "Logger.hpp"
#include "HTTPRequest.hpp"
#include "HTTPResponse.hpp"
#include "utils.hpp"
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

std::string joinPaths(std::filesystem::path path1, std::filesystem::path path2);

class Client;

class CGIHandler
{
    private:
        std::vector<std::string> envVariables;
        
    public:
        char* envArray[16];
        char* exceveArgs[3];
        int writeCGIPipe[2];
        int readCGIPipe[2];
        pid_t childPid;
        std::string fullPath;
        std::string inputFilePath;
        std::string output;
        std::string tempFileName;
        bool fileOpen;
        
        CGIHandler();
        void            setEnvValues(HTTPRequest& request, ServerConfig server);
        void            writeBodyToChild(HTTPRequest& request);
        HTTPResponse    generateCGIResponse(std::map<int, std::string> error_pages);
        void            collectCGIOutput(int readFd);
        int             getWritePipe();
        int             getReadPipe();
        int             getChildPid();
};
