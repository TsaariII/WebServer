#pragma once


#include <string>
#include <map>
#include <vector>
#include <sys/epoll.h>

#include "Client.hpp"
#include "Parser.hpp"
#include "Logger.hpp"

#define MAX_CONNECTIONS 1024
#define TIMEOUT 10
#define CHILD_CHECK 1
#define DEFAULT_MAX_HEADER_SIZE 8192
#define DEBUG_LOGS false

class EventLoop
{
    public:
        int timerFD;
        int nChildren;
        int childTimerFD;
        int loop;
        int status;
        bool timerOn;
        
        std::map<int, std::vector<ServerConfig>> servers;
        std::map<int, Client> clients;
        int serverSocket;
        std::vector<epoll_event> eventLog;
        struct itimerspec timerValues;
        pid_t pid;
        std::string checkConnection;
        std::vector<int> usedFDs;

        EventLoop(std::vector<ServerConfig> serverConfigs);
        bool validateRequestMethod(Client &client);
        void startLoop();
        void setTimerValues(int n);
        void checkTimeouts();
        void closeClient(int fd);
        void createErrorResponse(Client &client, int code, std::string msg, std::string logMsg);
        void handleClientRecv(Client& client);
        void handleClientSend(Client &client);
        void checkChildrenStatus();
        void checkBody(Client &client);
        void handleCGI(Client& client);
        int  executeCGI(Client& client, ServerConfig server);
        int  checkMaxSize(Client& client);
        void closeFds();
        ~EventLoop();
};
