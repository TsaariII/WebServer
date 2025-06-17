#pragma once

#include "CGIHandler.hpp"
#include "HTTPResponse.hpp"
#include "HTTPRequest.hpp"
#include "Parser.hpp"
#include <string>
#include <vector>
#include <chrono>

#define READ_BUFFER_SIZE 8192

enum connectionStates {
    IDLE,
    READ,
    HANDLE_CGI,
    SEND
};

class Client {
    public:
        int fd;
        std::chrono::steady_clock::time_point timestamp;
        enum connectionStates state;

        std::string headerString;
        std::string rawReadData;
        size_t      previousDataAmount;;
        std::string readBuffer;
        std::string writeBuffer;
        std::string chunkBuffer;
        int         bytesRead;
        int         bytesWritten;
        size_t      bytesSent;
        size_t      chunkBodySize;
        bool erase;

        std::vector<ServerConfig>   serverInfoAll;
        ServerConfig                serverInfo;

        HTTPRequest                     request;
        std::vector<HTTPResponse>       response;
        CGIHandler                      CGI;

        Client(int loop, int serverSocket, std::map<int, Client>& clients, std::vector<ServerConfig> server);
        Client(const Client& copy);
        Client& operator=(const Client& copy);
        ~Client();

        void findCorrectHost(const std::string headerString, const std::vector<ServerConfig>& serverInfoAll);
        void reset();
};