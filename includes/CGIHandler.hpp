#pragma once
#include <vector>
#include <string>
#include "Logger.hpp"
#include "HTTPRequest.hpp"
#include "HTTPResponse.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include "utils.hpp"
#include <limits.h>

std::string joinPaths(std::filesystem::path path1, std::filesystem::path path2);

class Client;

class CGIHandler
{
    private:
        std::vector<std::string> envVariables;
        
    public:
        char* envArray[16];
        char* exceveArgs[3];
        int writeCGIPipe[2]; //inPipe
        int readCGIPipe[2]; //outPipe
        pid_t childPid;
        std::string fullPath;
        std::string output;
        std::string tempFileName;
        bool fileOpen;
        // pid_t childPid;
        
        CGIHandler();
        void            setEnvValues(HTTPRequest& request, ServerConfig server);
        //int             executeCGI(HTTPRequest& request, ServerConfig server);
        void            writeBodyToChild(HTTPRequest& request);
        HTTPResponse    generateCGIResponse();
        void            collectCGIOutput(int readFd);
        int             getWritePipe();
        int             getReadPipe();
        int             getChildPid();
        char* const*     getEnvArray();
        char* const*     getExceveArgs();
};
