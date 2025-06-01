#include "eventLoop.hpp"
#include "timeout.hpp"
#include "HTTPResponse.hpp"
#include "Logger.hpp"
#include "RequestHandler.hpp"
#include "CGIhandler.hpp"
#include "Logger.hpp"
#include <fcntl.h>


bool        validateHeader(HTTPRequest req);
void        eventLoop(std::vector<ServerConfig> servers);
static int  initServerSocket(ServerConfig server);
static int  acceptNewClient(int loop, int serverSocket, std::map<int, Client>& clients);
//static void handleClientRecv(Client &client, int loop);
static void handleClientSend(Client &client, int loop);
static void toggleEpollEvents(int fd, int loop, uint32_t events);
static int  findOldestClient(std::map<int, Client>& clients);

// CGIHandler cgi;
// int eventFD; //remove when making eventLoop into a class?
int timerFD; //remove when making eventLoop into a class?
int nChildren;
int childTimerFD; //remove when making eventLoop into a class?

void eventLoop(std::vector<ServerConfig> serverConfigs)
{
    std::map<int, ServerConfig> servers;
    std::map<int, Client> clients;
    int serverSocket;
    struct epoll_event setup;
    std::vector<epoll_event> eventLog(MAX_CONNECTIONS);
    nChildren = 0;

    int loop = epoll_create1(0);

    for (size_t i = 0; i < serverConfigs.size(); i++)
    {
        ServerConfig newServer;
        serverSocket = initServerSocket(serverConfigs[i]);
        newServer = serverConfigs[i];
        newServer.fd = serverSocket;
        setup.data.fd = newServer.fd;
        setup.events = EPOLLIN;
        if (epoll_ctl(loop, EPOLL_CTL_ADD, serverSocket, &setup) < 0)
            throw std::runtime_error("serverSocket epoll_ctl ADD failed");
        servers[serverSocket] = newServer;
        // std::cout << "New server #" << i << " connected, got FD " << newServer.fd << std::endl;
    }

    //create and setup timerFd to check timeouts
    timerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerFD < 0)
        std::runtime_error("failed to create timerfd");
    // wslog.writeToLogFile(INFO, "Timerfd created, it got FD" + std::to_string(timerFD), true);
    setup.data.fd = timerFD;
    if (epoll_ctl(loop, EPOLL_CTL_ADD, timerFD, &setup) < 0)
        throw std::runtime_error("Failed to add timerFd to epoll");
    struct itimerspec timerValues { };
    bool timerOn = false;

    //create and setup childTimerFD to check child processes
    childTimerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (childTimerFD < 0)
        std::runtime_error("failed to create childTimerFD");
    // wslog.writeToLogFile(INFO, "childTimerFD created, it got FD" + std::to_string(childTimerFD), true);
    setup.data.fd = childTimerFD;
    if (epoll_ctl(loop, EPOLL_CTL_ADD, childTimerFD, &setup) < 0)
        throw std::runtime_error("Failed to add childTimerFD to epoll");

    // //create and setup eventFd to check child processes
    // int eventFd = eventfd(0, EFD_NONBLOCK);
    // if (eventFd < 0)
    //     throw std::runtime_error("Failed to create eventFd");
    // wslog.writeToLogFile(INFO, "Eventfd created, it got FD" + std::to_string(eventFd), true);
    // setup.data.fd = eventFd;
    // if (epoll_ctl(loop, EPOLL_CTL_ADD, eventFd, &setup) < 0)
    //      throw std::runtime_error("Failed to add eventFd to epoll");
    // eventFD = eventFd;

    while (true)
    {
        int nReady = epoll_wait(loop, eventLog.data(), MAX_CONNECTIONS, -1);
        if (nReady == -1)
        {
            if (errno == EINTR)
            {
                // wslog.writeToLogFile(INFO, "epoll_wait interrupted by signal", true);
                continue;
            }
            else
                throw std::runtime_error("epoll_wait failed");
        }
        for (int i = 0; i < nReady; i++)
        {
            int fd = eventLog[i].data.fd;
            if (servers.find(fd) != servers.end())
            {
                Client newClient;
                int clientFd = acceptNewClient(loop, fd, clients);
                setup.data.fd = clientFd;
                setup.events = EPOLLIN;
                newClient.serverInfo = servers[fd];
                newClient.fd = clientFd;
                newClient.timestamp = std::chrono::steady_clock::now();
                if (epoll_ctl(loop, EPOLL_CTL_ADD, clientFd, &setup) < 0)
                    throw std::runtime_error("newClient epoll_ctl ADD failed");
                clients[clientFd] = newClient;
                // std::cout << "New client with FD "<< clients[clientFd].fd << " connected to server with FD " << serverSocket << std::endl;
                if (timerOn == false)
                {
                    timerValues.it_value.tv_sec = TIMEOUT;
                    timerValues.it_interval.tv_sec = TIMEOUT / 2;
                    timerfd_settime(timerFD, 0, &timerValues, 0); //start timeout timer
                    timerOn = true;
                }
            }

            else if (fd == timerFD)
            {
                if (clients.empty())
                {
                    // wslog.writeToLogFile(INFO, "No more clients connected, not checking timeouts anymore until new connections", true);
                    timerValues.it_value.tv_sec = 0;
                    timerValues.it_interval.tv_sec = 0;
                    timerfd_settime(timerFD, 0, &timerValues, 0); //stop timer
                    timerOn = false;
                }
                else
                {
                    // wslog.writeToLogFile(INFO, "Time to check timeouts!", true);
                    checkTimeouts(timerFD, clients, nChildren);
                }
            }

            else if (fd == childTimerFD)
            {
                // wslog.writeToLogFile(INFO, "Time to check children! The amount of children is " + std::to_string(nChildren), true);
                checkChildrenStatus(childTimerFD, clients, loop, nChildren);
                if (nChildren == 0)
                {
                    timerValues.it_value.tv_sec = 0;
                    timerValues.it_interval.tv_sec = 0;
                    // wslog.writeToLogFile(INFO, "no children left, not checking their status anymore", true);
                    timerfd_settime(childTimerFD, 0, &timerValues, 0);
                }
            }

            else if (clients.find(fd) != clients.end())
            {
                if (eventLog[i].events & EPOLLIN)
                {
                    // if (client.request.isCGI && client.cgiFD) //this maybe not needed?
                    // {

                    //     handleCGI(client);
                    // }
                    // std::cout << "EPOLLIN fd " << fd << std::endl;
                    clients.at(fd).timestamp = std::chrono::steady_clock::now();
                    handleClientRecv(clients.at(fd), loop);
                }
                if (eventLog[i].events & EPOLLOUT)
                {
                    // std::cout << "EPOLLOUT" << std::endl;
                    clients.at(fd).timestamp = std::chrono::steady_clock::now();
                    handleClientSend(clients.at(fd), loop);
                }
                if (clients.at(fd).erase == true)
                if (clients.at(fd).erase == true)
                {
                    // std::cout << "client erased" << std::endl;
                    clients.erase(fd);
                }
            }
        }
    }
}

static int initServerSocket(ServerConfig server)
{
    int serverSocket = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
    if (serverSocket == -1)
    {
        // wslog.writeToLogFile(ERROR, "Socket creation failed", true);
        return -1;
    }
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //REMOVE LATER

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          // IPv4, käytä AF_UNSPEC jos haluat myös IPv6
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(server.host.c_str(), server.port.c_str(), &hints, &res);
    if (status != 0)
    {
        // wslog.writeToLogFile(ERROR, std::string("getaddrinfo failed"), true);
        return -1;
    }

    int rvalue = bind(serverSocket, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rvalue == -1)
    {
        // wslog.writeToLogFile(ERROR, "Bind failed for socket", true);
        return -1;
    }
    rvalue = listen(serverSocket, SOMAXCONN);
    if (rvalue == -1)
    {
        // wslog.writeToLogFile(ERROR, "Listen failed for socket", true);
        return -1;
    }

    return (serverSocket);
}

static int acceptNewClient(int loop, int serverSocket, std::map<int, Client>& clients)
{
    struct sockaddr_in clientAddress;
    socklen_t clientLen = sizeof(clientAddress);

    int newFd = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLen);
    if (newFd < 0)
    {
        if (errno == EMFILE) //max fds reached
        {
            int oldFd = findOldestClient(clients);
            if (oldFd != 0)
            {
                if (epoll_ctl(loop, EPOLL_CTL_DEL, oldFd, nullptr) < 0)
                    throw std::runtime_error("oldFd epoll_ctl DEL failed");
                close(oldFd);
                if (clients.find(oldFd) != clients.end())
                    clients.at(oldFd).reset();
                newFd = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLen);
            }
        }
        else
            throw std::runtime_error("accepting new client failed");
    }
    return newFd;
}

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

static bool isHexUnsignedLongLong(std::string str)
{
    std::stringstream ss(str);
    long long unsigned value;
    ss >> std::hex >> value;
    return !ss.fail();
}

static long long unsigned HexStrToUnsignedLongLong(std::string str)
{
    std::stringstream ss(str);
    long long unsigned value;
    ss >> std::hex >> value;
    return value;
}


static bool validateChunkedBody(Client &client)
{
    while (client.chunkBuffer.empty() == false)
    {
        long long unsigned bytes;
        std::string str = client.chunkBuffer;
        // Log the start (first 20 chars) and end (last 20 chars) of the buffer
        size_t logLen = 20;
        std::string start = str.substr(0, std::min(logLen, str.size()));
        std::string end = str.size() > logLen ? str.substr(str.size() - logLen) : str;
        wslog.writeToLogFile(DEBUG, "chunkBuffer start: {" + start + "}", true);
        wslog.writeToLogFile(DEBUG, "chunkBuffer end: {" + end + "}", true);
        if (!isHexUnsignedLongLong(str))
        {
            wslog.writeToLogFile(DEBUG, "triggered here1 ", true);
            return false;
        }
        bytes = HexStrToUnsignedLongLong(str);
        long long unsigned i = 0;
        while (str[i] != '\r' && i < str.size())
        {
            if (!std::isxdigit(str[i]))
            {
                wslog.writeToLogFile(DEBUG, "triggered here2 ", true);
                return false;
            }
            i++;
        }
        if (bytes == 0)
        {
            if (str.substr(1, 4) == "\r\n\r\n")
                return true;
            else
            {
                wslog.writeToLogFile(DEBUG, "triggered here3 ", true);
                return false;
            }
        }
        if (str.size() > (i + 1) && (str[i + 1] != '\n'))
        {
            wslog.writeToLogFile(DEBUG, "triggered here4 ", true);
            return false;
        }
        str = str.substr(i + 2);
        if (client.request.tempFileOpen == true) 
        {
            // Write existing body to file

            write(client.request.BodyFd, str.substr(0, bytes).data(), bytes);
        }
        else
            client.request.body += str.substr(0, bytes); // add the validated bytes to the request body
        str = str.substr(bytes);
        if (str.substr(0, 2) != "\r\n")
        {
            wslog.writeToLogFile(DEBUG, "str.substr(0, 2) = {" + str.substr(0, 2) + "}", true);
            wslog.writeToLogFile(DEBUG, "counter = " + std::to_string(i), true);
            wslog.writeToLogFile(DEBUG, "bytes = " + std::to_string(bytes), true);
            //wslog.writeToLogFile(DEBUG, "str = {" + str + "}", true);
            wslog.writeToLogFile(DEBUG, "triggered here5 ", true);
            return false;
        }
        else
            str = str.substr(2);
        client.chunkBuffer.erase(0, i + 2 + bytes + 2);
    }
    return true;
}

static bool checkMethods(Client &client, int loop)
{
    if (!RequestHandler::isAllowedMethod(client.request.method, client.serverInfo.routes[client.request.location]))
    {
        client.state = SEND;
        client.response.push_back(HTTPResponse(405, "Method not allowed"));
        client.writeBuffer = client.response.back().toString();
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return false;
    }
    else
        return true;
}

static void handleCGIWriteToPipe(Client& client)
{
    wslog.writeToLogFile(DEBUG, "Handling CGI write for client FD: " + std::to_string(client.fd), true);
    if (client.CGI.writeCGIPipe[1] != -1 && client.CGI.isFdWritable(client.CGI.writeCGIPipe[1]))
    {
        ssize_t written = write(client.CGI.writeCGIPipe[1], client.request.body.c_str(), client.request.body.size());
        wslog.writeToLogFile(DEBUG, "Writing to CGI pipe bytes written: " + std::to_string(written), true);
        if (written <= 0) 
        {
            wslog.writeToLogFile(DEBUG, "write() failed: " + std::string(strerror(errno)), true);
        }
        else
            client.request.body = client.request.body.substr(written);
    }
    if (client.request.body.empty() && client.CGI.writeCGIPipe[1] != -1) 
    {
        close(client.CGI.writeCGIPipe[1]);
        client.CGI.writeCGIPipe[1] = -1;
    }
}

static void handleCGIReadFromPipe(Client& client)
{
    wslog.writeToLogFile(DEBUG, "Handling CGI read for client FD: " + std::to_string(client.fd), true);
    if (client.CGI.readCGIPipe[0] != -1 && client.CGI.isFdReadable(client.CGI.readCGIPipe[0]))
    {
        char buffer[10000];
        ssize_t n;
        n = read(client.CGI.readCGIPipe[0], buffer, client.CGI.output.size());
        if (n > 0)
        {
            client.CGI.output.append(buffer, n);
            wslog.writeToLogFile(DEBUG, "Read from CGI pipe bytes: " + std::to_string(n), true);
        }
        else if (n == 0)
        {
            close(client.CGI.readCGIPipe[0]);
            client.CGI.readCGIPipe[0] = -1;
        }
        else
        {
            wslog.writeToLogFile(DEBUG, "read() failed: " + std::string(strerror(errno)), true);
        }
    }
}

static void handleCGIReadFromFile(Client& client)
{
    wslog.writeToLogFile(DEBUG, "Handling CGI read from file for client FD: " + std::to_string(client.fd), true);
    
    if (client.CGI.writeFileOpen == false)
    {
        client.CGI.writeFileFd = open(client.CGI.cgiTempFile.c_str(), O_RDONLY, 0644);
        if (client.CGI.writeFileFd < 0)
        {
            wslog.writeToLogFile(ERROR, "Failed to open temporary CGI file for reading from server", true);
            return ;
        }
        client.CGI.writeFileOpen = true;
    }
    if (client.CGI.writeFileFd != -1 && client.CGI.isFdReadable(client.CGI.writeFileFd))
    {
        char buffer[10000];
        ssize_t n;
        n = read(client.CGI.writeFileFd, buffer, sizeof(buffer));
        if (n > 0)
        {
            client.CGI.output.append(buffer, n);
            wslog.writeToLogFile(DEBUG, "Read from CGI file bytes: " + std::to_string(n), true);
        }
        else if (n == 0)
        {
            close(client.CGI.writeFileFd);
            client.CGI.writeFileOpen = false;
            close(client.CGI.readFileFd);
            client.CGI.writeFileFd = -1;
            client.CGI.readFileFd = -1;
            client.CGI.cgiReady = true;
        }
        else
        {
            wslog.writeToLogFile(DEBUG, "read() failed: " + std::string(strerror(errno)), true);
        }
    }
}

static void handleCGI(Client& client, int loop)
{
    wslog.writeToLogFile(DEBUG, "Handling CGI for client FD: " + std::to_string(client.fd), true);
    pid_t pid = waitpid(client.CGI.childPid, NULL, WNOHANG);
    wslog.writeToLogFile(DEBUG, "cgi.childPid is: " + std::to_string(client.CGI.childPid), true);
    wslog.writeToLogFile(DEBUG, "waitpid returned: " + std::to_string(pid), true);
    if (client.request.tempFileOpen == false)
    {
        handleCGIWriteToPipe(client);
        handleCGIReadFromPipe(client);
    }
    if (pid == client.CGI.childPid)
    {
        wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI pipes closed", true);
        if (client.request.tempFileOpen == false)
            handleCGIReadFromPipe(client);
        else
        {
            handleCGIReadFromFile(client);
            wslog.writeToLogFile(DEBUG, "CGI process finished", true);
            client.CGI.generateCGIResponse();
            client.state = SEND;
            nChildren--;
            toggleEpollEvents(client.fd, loop, EPOLLOUT);
            client.request.isCGI = false;
            return ;
        }
        client.state = SEND;
        // IT SEEMS THAT CGI EXTENSION RULE TAKES PRECEDENCE OVER IS ALLOWED METHOD RULES IN LOCATION
        // SO IF THE EXTENSION IS CORRECT IT SHOULD BE EXECUTED EVEN IF THE METHOD IS NOT ALLOWED
        client.request.isCGI = false;
        client.writeBuffer = client.response.back().toString();
        nChildren--;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
}

static void readChunkedBody(Client &client, int loop)
{
    if ((client.chunkBuffer.size() + client.rawReadData.size()) > BODY_MEMORY_LIMIT)
    {
        client.chunkBuffer += client.rawReadData;
        if (client.request.tempFileOpen == false)
        {
            std::ostringstream oss;
            oss << client.request.tempBodyFile.data() << "_" << std::time(NULL) << "_" << rand();
            std::string tempfilename = oss.str();
            client.request.tempBodyFile = tempfilename;
            client.request.BodyFd = open(client.request.tempBodyFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
            if (client.request.BodyFd < 0)
            {
                wslog.writeToLogFile(ERROR, "Failed to open temporary body file", true);
                return ;
            }
            client.request.tempFileOpen = true;
        }
    }
    else
        client.chunkBuffer += client.rawReadData;
    if (client.rawReadData.empty() || client.rawReadData.size() < 2 || client.rawReadData.substr(client.rawReadData.size() - 2) != "\r\n")
    {
        //wslog.writeToLogFile(DEBUG, "Chunk buffer is empty or does not end with \\r\\n, waiting for more data", true);
        return ;
    }
    if (!validateChunkedBody(client))
    {
        if (client.request.isCGI == false)
        {
            if (checkMethods(client, loop) == false)
                return ;
        }
        else
            client.response.push_back(HTTPResponse(400, "Bad request"));
        if (client.response.back().getStatusCode() >= 400)
            client.response.back() = client.response.back().generateErrorResponse(client.response.back());
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
    /* if (client.chunkBuffer.size() > 1000000) // 1MB limit for chunked body
    {
        wslog.writeToLogFile(DEBUG, "Chunked body too large, rejecting request", true);
        client.response.push_back(HTTPResponse(413, "Payload Too Large"));
        if (client.response.back().getStatusCode() >= 400)
                client.response.back() = client.response.back().generateErrorResponse(client.response.back());
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    } */

    if (client.rawReadData.size() >= 5 && client.rawReadData.substr(client.rawReadData.size() - 5) == "0\r\n\r\n")
    {
        if (client.request.tempFileOpen == true)
        {
            close(client.request.BodyFd);
            // opening again to reset the fd position to start after writing to there
            client.request.BodyFd = open(client.request.tempBodyFile.c_str(), O_RDONLY, 0644);
            if (client.request.BodyFd < 0)
            {
                wslog.writeToLogFile(ERROR, "Failed to open temporary body file", true);
                return ;
            }
        }
        client.chunkBuffer = "";
        if (client.request.isCGI == true)
        {
            wslog.writeToLogFile(DEBUG, "Handling CGI after chunked body", true);
            client.state = HANDLE_CGI;
            client.pipeFd = client.CGI.executeCGI(client.request, client.serverInfo);
            struct itimerspec timerValues { };
            if (nChildren == 0) //before first child
            {
                timerValues.it_value.tv_sec = CHILD_CHECK;
                timerValues.it_interval.tv_sec = CHILD_CHECK;
                timerfd_settime(childTimerFD, 0, &timerValues, 0); //there are children, check timeouts more often
            }
            wslog.writeToLogFile(INFO, "ChildTimer turned on", true);
            nChildren++;
            handleCGI(client, loop);
            return ;
        }
        client.response.push_back(RequestHandler::handleRequest(client));
        if (client.response.back().getStatusCode() >= 400)
            client.response.back() = client.response.back().generateErrorResponse(client.response.back());
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return;
    }
    client.rawReadData.clear();
}

// void handleSIGCHLD(int)
// {
//     wslog.writeToLogFile(INFO, "SIGCHLD received", true);
//     uint64_t notify = 1;
//     write(eventFD, &notify, sizeof(notify));
// }

static void checkBody(Client &client, int loop)
{

    auto TE = client.request.headers.find("Transfer-Encoding");
    if (TE != client.request.headers.end() && TE->second == "chunked")
        return readChunkedBody(client, loop);
    auto CL = client.request.headers.find("Content-Length");
    if (CL != client.request.headers.end() && client.rawReadData.size() >= stoul(CL->second)) //or end of chunks?
    {
        client.request.body = client.rawReadData;
        if (client.request.isCGI == true)
        {
            std::cout << "BODY CGI" << std::endl;
            client.state = HANDLE_CGI;
            client.pipeFd = client.CGI.executeCGI(client.request, client.serverInfo);
            struct itimerspec timerValues { };
            if (nChildren == 0) //before first child
            {
                timerValues.it_value.tv_sec = CHILD_CHECK;
                timerValues.it_interval.tv_sec = CHILD_CHECK;
                timerfd_settime(childTimerFD, 0, &timerValues, 0); //there are children, check timeouts more often
            }
            // wslog.writeToLogFile(INFO, "ChildTimer turned on", true);
            nChildren++;
            handleCGI(client, loop);
            return ;
        }
        client.response.push_back(RequestHandler::handleRequest(client));
        if (client.response.back().getStatusCode() >= 400)
            client.response.back() = client.response.back().generateErrorResponse(client.response.back());
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
}

void handleClientRecv(Client& client, int loop)
{
    switch (client.state)
    {
        case IDLE:
        {
            client.state = READ_HEADER;
            return ;
        case READ_HEADER:
        {
            client.bytesRead = 0;
            char buffer[READ_BUFFER_SIZE];
            client.bytesRead = recv(client.fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);

            if (client.bytesRead <= 0)
            {
                if (client.bytesRead == 0)
                    // wslog.writeToLogFile(INFO, "Client disconnected FD" + std::to_string(client.fd), true);
                close(client.fd);
                client.erase = true;
                // if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                //     throw std::runtime_error("epoll_ctl DEL failed");
                return ;
            }

            buffer[client.bytesRead] = '\0';
            std::string temp(buffer, client.bytesRead);
            client.rawReadData += temp;
            size_t headerEnd = client.rawReadData.find("\r\n\r\n");
            if (headerEnd != std::string::npos)
            {
                client.headerString = client.rawReadData.substr(0, headerEnd + 4);
                // wslog.writeToLogFile(DEBUG, "Header: " + client.headerString, true);
                client.request = HTTPRequest(client.headerString, client.serverInfo);
                if (validateHeader(client.request) == false)
                {
                    client.response.push_back(HTTPResponse(400, "Bad request"));
                    if (client.response.back().getStatusCode() >= 400)
                        client.response.back() = client.response.back().generateErrorResponse(client.response.back());
                    client.state = SEND;
                    client.writeBuffer = client.response.back().toString();
                    toggleEpollEvents(client.fd, loop, EPOLLOUT);
                    return ;
                }
                client.bytesRead = 0;
                client.rawReadData = client.rawReadData.substr(headerEnd + 4);

                /*
                split into three paths here based on request:
                1. static response with POST -> go to READ_BODY
                2. static response without POST -> go to SEND
                3. dynamic response with CGI -> (registerCGI ->) go to handleCGI //maybe we want client enum state like HANDLE_CGI?
                */

                if (client.request.method == "POST")
                {
                    client.state = READ_BODY;
                    std::cout << "OMG I'M HERE" << std::endl;
                    checkBody(client, loop);
                    return;
                }

                else
                {
                    if (client.request.isCGI == true)
                    {
                        client.state = HANDLE_CGI;
                        client.CGI.setEnvValues(client.request, client.serverInfo);
                        if (!checkMethods(client, loop))
                            return ;
                        client.pipeFd = client.CGI.executeCGI(client.request, client.serverInfo);
                        struct itimerspec timerValues { };
                        if (nChildren == 0) //before first child
                        {
                            timerValues.it_value.tv_sec = CHILD_CHECK;
                            timerValues.it_interval.tv_sec = CHILD_CHECK;
                            timerfd_settime(childTimerFD, 0, &timerValues, 0); //there are children, check timeouts more often
                        }
                        // wslog.writeToLogFile(INFO, "ChildTimer turned on", true);
                        nChildren++;
                        handleCGI(client, loop);
                        return ;
                    }
                    else
                    {
                        client.state = SEND;
                        client.request.body = client.rawReadData;
                        client.response.push_back(RequestHandler::handleRequest(client));
                        if (client.response.back().getStatusCode() >= 400)
                            client.response.back() = client.response.back().generateErrorResponse(client.response.back());
                        client.writeBuffer = client.response.back().toString();
                        toggleEpollEvents(client.fd, loop, EPOLLOUT);
                        return ;
                    }
                }
            }
            else
                return ;
            return ;
        }
        case READ_BODY:
        {
            wslog.writeToLogFile(INFO, "IN READ BODY", false);
            client.bytesRead = 0;
            char buffer2[READ_BUFFER_SIZE];
            client.bytesRead = recv(client.fd, buffer2, sizeof(buffer2) - 1, MSG_DONTWAIT);
            if (client.bytesRead == 0)
            {
                // Client disconnected
                // std::cout << "ClientFD disconnected " << client.fd << std::endl;
                close(client.fd);
                client.erase = true;
                epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr);
                return ;
            }
            if (client.bytesRead < 0)
            {
                close(client.fd);
                client.erase = true;
                // if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                //     throw std::runtime_error("epoll_ctl DEL failed");
                return ;
            }
            buffer2[client.bytesRead] = '\0';
            std::string temp(buffer2, client.bytesRead);
            client.rawReadData += temp;
            checkBody(client, loop);
            return ;
        }
        case HANDLE_CGI:
            return handleCGI(client, loop);
		// case HANDLE_CGI:
        // {
        //     wslog.writeToLogFile(INFO, "IN HANDLE_CGI case", true);
        //     if (handleCGI(client) == false)
        //         return ;
        //     else
        //     {
        //         client.state = SEND;
        //         client.writeBuffer = client.response.back().toString();
        //         toggleEpollEvents(client.fd, loop, EPOLLOUT);
        //         return ;
        //     }
        // }
		case SEND:
            return;
        }
    }
}

static void handleClientSend(Client &client, int loop)
{
    if (client.state != SEND)
        return ;
    wslog.writeToLogFile(INFO, "IN SEND", true);
    wslog.writeToLogFile(INFO, "To be sent = " + client.writeBuffer + " to client FD" + std::to_string(client.fd), true);

     if (client.request.tempFileOpen == true)
    {
        client.bytesWritten = send(client.fd, client.CGI.output.data(), client.CGI.output.size(), MSG_DONTWAIT);
        client.CGI.output.clear();
    }
    else
        client.bytesWritten = send(client.fd, client.writeBuffer.data(), client.writeBuffer.size(), MSG_DONTWAIT);
    if (client.request.tempFileOpen == true)
    {
        if (client.request.BodyFd < 0)
        {
            wslog.writeToLogFile(ERROR, "Temporary body file not open", true);
            return ;
        }
        if (client.CGI.cgiReady == false)
        {
            handleCGIReadFromFile(client);
        }
        else
        {
            client.request.tempFileOpen = false;
            client.request.isCGI = false;
        }
    }
    // wslog.writeToLogFile(INFO, "Bytes sent = " + std::to_string(client.bytesWritten), true);
    if (client.bytesWritten <= 0)
    {
        close(client.fd);
        client.erase = true;
        // if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
        //     throw std::runtime_error("check connection epoll_ctl DEL failed");
        return ;
    }
    client.writeBuffer.erase(0, client.bytesWritten);
    // wslog.writeToLogFile(INFO, "Remaining to send = " + std::to_string(client.writeBuffer.size()), true);
    if (client.writeBuffer.empty())
    {
        std::string checkConnection;
        if (client.request.headers.find("Connection") != client.request.headers.end())
            checkConnection = client.request.headers.at("Connection");
        if (!checkConnection.empty())
        {
            if (checkConnection == "close" || checkConnection == "Close")
            {
                close(client.fd);
                // if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                //     throw std::runtime_error("check connection epoll_ctl DEL failed");
                client.erase = true;
            }
            else
            {
                // wslog.writeToLogFile(INFO, "Client reset", true);
                client.reset();
                toggleEpollEvents(client.fd, loop, EPOLLIN);
            }
        }
        else if (client.request.version == "HTTP/1.0")
        {
            close(client.fd);
            // if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
            //     throw std::runtime_error("check connection epoll_ctl DEL failed");
            client.erase = true;
        }
        else
        {
            // wslog.writeToLogFile(INFO, "Client reset", true);
            client.reset();
            toggleEpollEvents(client.fd, loop, EPOLLIN);
        }
    }
}

static void toggleEpollEvents(int fd, int loop, uint32_t events)
{
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    if (epoll_ctl(loop, EPOLL_CTL_MOD, fd, &ev) < 0)
        throw std::runtime_error("epoll_ctl MOD failed");
}
