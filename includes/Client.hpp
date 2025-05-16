#pragma once
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <sstream>
#include <chrono>
#include "Parser.hpp"
#include "Enums.hpp"
#include "HTTPResponse.hpp"
#include "HTTPRequest.hpp"

#define READ_BUFFER_SIZE 1000 //nginx has 8192?

enum connectionStates {
    IDLE,
    READ_HEADER,
    READ_BODY,
    SEND
};

class Client {
    public:  //change all these to private? fix later
        int fd;
        std::chrono::steady_clock::time_point timestamp;
        enum connectionStates state;

        std::string headerString;
        std::string rawReadData;
        size_t previousDataAmount;
        std::string readBuffer;
        std::string writeBuffer;
        int bytesRead;
        int bytesWritten;
        bool erase;

        ServerConfig serverInfo;

        HTTPRequest request;
        HTTPResponse response;

        std::string chunkBuffer;     // Väliaikainen bufferi chunkin lukemista varten
        
        Client();
        Client(const Client& copy);
        Client& operator=(const Client& copy);
        ~Client();

        void reset();
        reqTypes getMethodEnum();
};