#include "Client.hpp"
#include "Logger.hpp"

#include <map>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

static int findOldestClient(std::map<int, Client>& clients)
{
    int oldestClient = 0;
    std::chrono::steady_clock::time_point oldestTimestamp = std::chrono::steady_clock::now();

    for (auto it : clients)
    {
        if (it.second.timestamp < oldestTimestamp)
        {
            oldestClient = it.second.fd;
            oldestTimestamp = it.second.timestamp;
        }
    }
    return oldestClient;
}

Client::Client(int loop, int serverSocket, std::map<int, Client>& clients, std::vector<ServerConfig> server)
{
    this->state = IDLE;
    this->bytesRead = 0;
    this->bytesWritten = 0;
    this->bytesSent = 0;
    this->previousDataAmount = 0;
    this->chunkBodySize = 0;
    this->erase = false;
    struct sockaddr_in clientAddress;
    socklen_t clientLen = sizeof(clientAddress);
    fd = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLen);
    if (fd < 0)
    {
        if (errno == EMFILE)
        {
            int oldFd = findOldestClient(clients);
            wslog.writeToLogFile(INFO, "---CLOSING CLIENT FD" + std::to_string(oldFd) + " PREMATURELY!---", DEBUG_LOGS);
            if (oldFd != 0)
            {
                if (epoll_ctl(loop, EPOLL_CTL_DEL, oldFd, nullptr) < 0)
                    throw std::runtime_error("oldFd epoll_ctl DEL failed");
                close(oldFd);
                if (clients.at(oldFd).request.isCGI == true)
                {
                    int readPipe = clients.at(oldFd).CGI.getReadPipe();
                    int writePipe = clients.at(oldFd).CGI.getWritePipe();
                    if (readPipe != -1)
                        close(readPipe);
                    if (writePipe != -1)
                        close(writePipe);
                }
                if (clients.find(oldFd) != clients.end())
                    clients.erase(oldFd);
                fd = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLen);
            }
        }
        else
            throw std::runtime_error("Accepting new client failed");
    }
    serverInfoAll = server;
    timestamp = std::chrono::steady_clock::now();
}

Client::~Client()
{
    if (CGI.writeCGIPipe[0] != -1)
        close(CGI.writeCGIPipe[0]);
    if (CGI.writeCGIPipe[1] != -1)
        close(CGI.writeCGIPipe[1]);
    if (CGI.readCGIPipe[0] != -1)
        close(CGI.readCGIPipe[0]);
    if (CGI.readCGIPipe[1] != -1)
        close(CGI.readCGIPipe[1]);
}

Client::Client(const Client& copy)
{
    *this = copy;
}

Client& Client::operator=(const Client& copy)
{
    if (this != & copy)
    {
        this->fd = copy.fd;
        this->state = copy.state;
        this->timestamp = copy.timestamp;
        this->readBuffer = copy.readBuffer;
        this->rawReadData = copy.rawReadData;
        this->previousDataAmount = copy.previousDataAmount;
        this->writeBuffer = copy.writeBuffer;
        this->bytesRead = copy.bytesRead;
        this->bytesWritten = copy.bytesWritten;
        this->serverInfoAll = copy.serverInfoAll;
        this->serverInfo = copy.serverInfo;
        this->request = copy.request;
    }
    return *this;
}

void Client::reset()
{
    this->state = IDLE;
    this->readBuffer.clear();
    this->chunkBuffer.clear();
    this->rawReadData.clear();
    this->previousDataAmount = 0;
    this->writeBuffer.clear();
    this->headerString.clear();
    this->response.clear();
    this->bytesRead = 0;
    this->bytesWritten = 0;
    this->response.clear();
    this->erase = false;
    this->request = HTTPRequest();
    this->CGI = CGIHandler();
    this->bytesSent = 0;
    this->chunkBodySize = 0;
}

void Client::findCorrectHost(const std::string header, const std::vector<ServerConfig>& server)
{
    size_t hostPos = header.find("Host:");
    if (hostPos != std::string::npos)
    {
        hostPos += 5;
        while (hostPos < header.size() && std::isspace(header[hostPos]))
            hostPos++;
        size_t hostStart = hostPos;
        while (hostPos < header.size() && !std::isspace(header[hostPos]))
            hostPos++;
        std::string hostName = header.substr(hostStart, hostPos - hostStart);
        if (hostName.empty() != false && std::isspace(hostName.back() == true))
            hostName.pop_back();

        for (const ServerConfig& serverConfig : server)
        {
            for (const std::string& serverString : serverConfig.server_names)
            {
                if (serverString == hostName)
                {
                    this->serverInfo = serverConfig;
                    return ;
                }
            }
        }
        this->serverInfo = server[0];
        return ;
    }
    else
        this->serverInfo = server[0];
}
