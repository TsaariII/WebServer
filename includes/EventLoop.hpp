#pragma once

// #include "timeout.hpp"
#include "HTTPResponse.hpp"
#include "Logger.hpp"
#include "RequestHandler.hpp"
#include "CGIHandler.hpp"
#include "Logger.hpp"
#include "Parser.hpp"
#include "Client.hpp"
#include "utils.hpp"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <map>
#include <queue>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define MAX_CONNECTIONS 1024
#define TIMEOUT 60
#define CHILD_CHECK 1
#define DEFAULT_MAX_HEADER_SIZE 8192

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
        //std::map<int, ServerConfig> servers;
        std::map<int, Client> clients;
        int serverSocket;
        std::vector<epoll_event> eventLog;
        struct itimerspec timerValues;
        pid_t pid;
        std::string checkConnection;

        EventLoop(std::vector<ServerConfig> serverConfigs);
        void startLoop();
        void setTimerValues(int n);
        void checkTimeouts();//int timerFd, std::map<int, Client>& clients, int& children, int loop);
        void closeClient(int fd);//Client& client, std::map<int, Client>& clients, int& children, int loop);
        void createErrorResponse(Client &client, int code, std::string msg, std::string logMsg);
        void handleClientRecv(Client& client);
        void handleClientSend(Client &client);
        void checkChildrenStatus();
        void checkBody(Client &client);
        void handleCGI(Client& client);
        int  executeCGI(Client& client, ServerConfig server);
        bool checkMaxSize(Client& client);
        void closeFds();
        ~EventLoop();
};
