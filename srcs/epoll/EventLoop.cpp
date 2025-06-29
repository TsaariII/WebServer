#include "EventLoop.hpp"
#include "RequestHandler.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <csignal>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <cstdlib>

static int initServerSocket(ServerConfig server)
{
    int serverSocket = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
    if (serverSocket == -1)
        return -1;
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int status = getaddrinfo(server.host.c_str(), server.port.c_str(), &hints, &res);
    if (status != 0)
        return -1;
    int rvalue = bind(serverSocket, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rvalue == -1)
        return -1;
    rvalue = listen(serverSocket, SOMAXCONN);
    if (rvalue == -1)
        return -1;
    return (serverSocket);
}

static void toggleEpollEvents(int fd, int loop, uint32_t events)
{
    struct epoll_event ev {};
    ev.data.fd = fd;
    ev.events = events;
    if (epoll_ctl(loop, EPOLL_CTL_MOD, fd, &ev) < 0)
        throw std::runtime_error("epoll_ctl MOD failed " + std::to_string(errno));
}

EventLoop::EventLoop(std::vector<ServerConfig> serverConfigs) : eventLog(MAX_CONNECTIONS), timerValues { }
{
    signal(SIGPIPE, handleSignals);
    signal(SIGINT, handleSignals);
    struct epoll_event setup {};
    nChildren = 0;
    loop = epoll_create1(0);
    if (loop < 0)
        throw std::runtime_error("Creating epoll failed");
    for (size_t i = 0; i < serverConfigs.size(); i++)
    {
        try {
            bool hostFound = false;
            for (auto ite = this->servers.begin(); ite != this->servers.end(); ite++)
            {
                ServerConfig server = ite->second.at(0);
                if (serverConfigs.at(i).host == server.host && serverConfigs.at(i).port == server.port)
                {
                    ite->second.push_back(serverConfigs.at(i));
                    hostFound = true;
                    break ;
                }
            }
            if (hostFound == false)
            {
                serverSocket = initServerSocket(serverConfigs[i]);
                if (serverSocket == -1)
                    throw std::runtime_error("server setup failed");
                serverConfigs[i].fd = serverSocket;
                setup.data.fd = serverConfigs[i].fd;
                setup.events = EPOLLIN;
                if (epoll_ctl(loop, EPOLL_CTL_ADD, serverSocket, &setup) < 0)
                    throw std::runtime_error("serverSocket epoll_ctl ADD failed");
                servers[serverSocket].push_back(serverConfigs[i]);
                wslog.writeToLogFile(INFO, "Created a server with FD" + std::to_string(serverSocket), true);
            }
        }
        catch (const std::bad_alloc& e)
        {
            wslog.writeToLogFile(ERROR, "Failed to create server #" + std::to_string(i) + " due to bad alloc, continuing creating other servers", DEBUG_LOGS);
            continue ;
        }
    }
}

void EventLoop::closeFds()
{
    close(loop);
    for (auto& server : servers)
        close(server.first);
    for (auto& client : clients)
        close(client.first);
    wslog.~Logger();
}

EventLoop::~EventLoop() 
{
    closeFds();
}

static void handleErrorMessages(std::string errorMessage, std::map<int, Client>& clients, int newFd)
{
    if (errorMessage == "oldFd epoll_ctl DEL failed")
        throw std::runtime_error("oldFd epoll_ctl DEL failed, closing the server");
    else if (errorMessage == "Client insert failed or duplicate fd" || errorMessage == "Accepting new client failed")
        wslog.writeToLogFile(ERROR, "Accepting a new client failed, continuing without connecting the client", DEBUG_LOGS);
    else if (errorMessage == "newClient epoll_ctl ADD failed")
    {
        close(clients.at(newFd).fd);
        clients.erase(clients.at(newFd).fd);
        for (auto&  client : clients)
            wslog.writeToLogFile(INFO, "client FD" + std::to_string(client.second.fd)+  " is here", DEBUG_LOGS);
        wslog.writeToLogFile(INFO, "Client closed and removed, after failing to add FD into epoll, continuing", DEBUG_LOGS);
    }
}

void EventLoop::timestamp()
{
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (clients.empty() == false && now > lastTimeoutCheck + std::chrono::seconds(TIMEOUT + 1))
        checkTimeouts();
    if (clients.empty() == false && nChildren > 0 && now > lastChildrenCheck + std::chrono::seconds(CHILD_CHECK))
        checkChildrenStatus();
}

void EventLoop::startLoop()
{
    wslog.writeToLogFile(INFO, "Webserver ready", true);
    lastChildrenCheck = std::chrono::steady_clock::now();
    while (signum == 0)
    {
        timestamp();
        int nReady = epoll_wait(loop, eventLog.data(), MAX_CONNECTIONS, 1000);
        if (nReady == -1)
        {
            if (errno == EINTR)
            {
                wslog.writeToLogFile(INFO, "epoll_wait interrupted by signal", DEBUG_LOGS);
                if (signum != 0)
                    break ;
                else
                    continue;
            }
            else
                throw std::runtime_error("epoll_wait failed");
        }
        timestamp();
        for (int i = 0; i < nReady; i++)
        {
            int fd = eventLog[i].data.fd;
            int newFd;
            if (servers.find(fd) != servers.end())
            {
                try {
                    if (clients.empty() == true)
                        lastTimeoutCheck = std::chrono::steady_clock::now();
                    struct epoll_event setup { };
                    Client newClient(loop, fd, clients, servers[fd]);
                    auto result =  clients.emplace(newClient.fd, std::move(newClient));
                    if (!result.second)
                        throw std::runtime_error("Client insert failed or duplicate fd");
                    newFd = newClient.fd;
                    clients.at(newFd).erase = false;
                    setup.data.fd = newClient.fd;
                    setup.events = EPOLLIN;
                    if (epoll_ctl(loop, EPOLL_CTL_ADD, newClient.fd, &setup) < 0)
                        throw std::runtime_error("newClient epoll_ctl ADD failed");
                    wslog.writeToLogFile(INFO, "Connecting a new client FD" + std::to_string(newClient.fd), true);
                }
                catch (const std::bad_alloc& e)
                {
                    wslog.writeToLogFile(ERROR, "Failed to add a client element into std::map due to bad alloc, continuing without connecting the client", DEBUG_LOGS);
                    continue ;
                }
                catch (const std::runtime_error& e)
                {
                    std::string errorMessage = e.what();
                    handleErrorMessages(errorMessage, clients, newFd);
                    continue ;
                }
            }
            else if (clients.find(fd) != clients.end())
            {
                if (eventLog[i].events & EPOLLHUP || eventLog[i].events & EPOLLERR)
                {
                    closeClient(fd);
                    continue ;
                }
                if (eventLog[i].events & EPOLLIN)
                {
                    clients.at(fd).timestamp = std::chrono::steady_clock::now();
                    handleClientRecv(clients.at(fd), eventLog[i].events);
                }
                if (eventLog[i].events & EPOLLOUT)
                {
                    clients.at(fd).timestamp = std::chrono::steady_clock::now();
                    handleClientSend(clients.at(fd));
                }
            }
        }
    }
}

void EventLoop::checkTimeouts()
{
    wslog.writeToLogFile(INFO, "Checking timeouts", true);
    lastTimeoutCheck = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    for (auto it = clients.begin(); it != clients.end();)
    {
        auto& client = it->second;
        ++it;
        int elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(now - client.timestamp).count();
        std::chrono::steady_clock::time_point timeout = client.timestamp + std::chrono::seconds(TIMEOUT);
        if (now > timeout)
        {
            createErrorResponse(client, 408, "Request Timeout", " timed out due to inactivity!");
            continue ;
        }
        if (client.state == READ && elapsedTime > 0)
        {
            int dataReceived = client.rawReadData.size() - client.previousDataAmount;
            int dataRate = dataReceived / elapsedTime;
            if ((client.rawReadData.size() > 64 && dataRate < 1024)
                || (client.rawReadData.size() < 64 && dataReceived < 15))
            {
                createErrorResponse(client, 408, "Request Timeout", " disconnected, client sent data too slowly!");
                continue ;
            }
        }
        if (client.state == READ && checkMaxSize(client) != 0)
        {
            createErrorResponse(client, 413, "Payload Too Large", " disconnected, size too big!");
            continue ;
        }
        if (client.state == SEND && elapsedTime > 0)
        {
            int dataSent = client.previousDataAmount - client.writeBuffer.size();
            int dataRate = dataSent / elapsedTime;
            if (client.writeBuffer.size() > 1024 && dataRate < 1024)
            {
                closeClient(client.fd);
                continue ;
            }
        } 
        if (client.state == READ)
            client.previousDataAmount = client.rawReadData.size();
        else if (client.state == SEND)
            client.previousDataAmount = client.writeBuffer.size();
    }
}

void EventLoop::createErrorResponse(Client &client, int code, std::string msg, std::string logMsg)
{
    wslog.writeToLogFile(ERROR, "Client FD" + std::to_string(client.fd) + logMsg, true);
    client.response.push_back(HTTPResponse(code, msg, client.serverInfo.error_pages));
    client.writeBuffer = client.response.back().toString();
    client.bytesWritten = send(client.fd, client.writeBuffer.data(), client.writeBuffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    closeClient(client.fd);
}

void EventLoop::closeClient(int fd)
{
    if (epoll_ctl(loop, EPOLL_CTL_DEL, fd, nullptr) < 0)
        throw std::runtime_error("timeout epoll_ctl DEL failed in closeClient");
    if (clients.at(fd).request.isCGI == true)
        nChildren--;
    close(clients.at(fd).fd);
    clients.erase(clients.at(fd).fd);
}

void EventLoop::checkChildrenStatus()
{
    lastChildrenCheck = std::chrono::steady_clock::now();
    if (clients.empty() == true)
        nChildren = 0;
    for (auto it = clients.begin(); it != clients.end(); ++it)
    {
        auto& client = it->second;
        if (nChildren > 0 && client.request.isCGI == true)
        {
            wslog.writeToLogFile(INFO, "Checking children status for client FD" + std::to_string(it->first), DEBUG_LOGS);
            handleClientRecv(client, 0);
            continue ;
        }
    }
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
    std::chrono::steady_clock::time_point timeout = std::chrono::steady_clock::now() + std::chrono::seconds(TIMEOUT);
    client.chunkBodySize = 0;
    while (client.chunkBuffer.empty() == false)
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now > timeout)
            return false;
        long long unsigned bytes = 0;
        std::string str = client.chunkBuffer;
        if (!isHexUnsignedLongLong(str))
        {
            return false;
        }
        bytes = HexStrToUnsignedLongLong(str);
        long long unsigned i = 0;
        while (str[i] != '\r' && i < str.size())
        {
            if (!std::isxdigit(str[i]))
            {
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
                return false;
            }
        }
        if (str.size() > (i + 1) && (str[i + 1] != '\n'))
        {
            return false;
        }
        str = str.substr(i + 2);
        if (client.request.fileUsed == true && client.request.isCGI == true) 
        {
            int byteswritten = write(client.request.fileFd, str.substr(0, bytes).c_str(), bytes);
            if (byteswritten < 0)
                wslog.writeToLogFile(ERROR, "WRITE FAILED IN CHUNK", DEBUG_LOGS);
            client.chunkBodySize += byteswritten;
        }
        else
            client.request.body += str.substr(0, bytes);
        str = str.substr(bytes);
        if (str.substr(0, 2) != "\r\n")
        {
            return false;
        }
        else
            str = str.substr(2);
        client.chunkBuffer.erase(0, i + 2 + bytes + 2);
    }
    return true;
}

void CGIMultipart(Client& client)
{
    if (client.request.headers.count("Content-Type") == 0)
    {
        wslog.writeToLogFile(ERROR, "400 Missing Content-Type", DEBUG_LOGS);
        client.response.push_back(HTTPResponse(400, "Missing Content-Type", client.serverInfo.error_pages));
        return;
    }
    auto its = client.request.headers.find("Content-Type");
    std::string ct = its->second;
    if (its == client.request.headers.end())
    {
        wslog.writeToLogFile(ERROR, "400 Invalid headers", DEBUG_LOGS);
        client.response.push_back(HTTPResponse(400, "Invalid headers", client.serverInfo.error_pages));
        return;
    }
    std::string boundary;
    std::string::size_type pos = ct.find("boundary=");
    if (pos == std::string::npos)
    {
        wslog.writeToLogFile(ERROR, "400 No boundary", DEBUG_LOGS);
        client.response.push_back(HTTPResponse (400, "No boundary"));
        return;
    }
    boundary = ct.substr(pos + 9);
    if (!boundary.empty() && boundary[0] == '"')
        boundary = boundary.substr(1, boundary.find('"', 1) - 1);
    std::string bound_mark = "--" + boundary;
    std::vector<std::string> parts = split(client.request.body, bound_mark);
    std::string lastPath;
    for (std::vector<std::string>::iterator it = parts.begin(); it != parts.end(); ++it)
    {
        std::string& part = *it;
        if (part.empty() || part == "--\r\n" || part == "--")
            continue;
        std::string disposition = part.substr(0, part.find("\r\n"));
        if (disposition.find("filename=\"") == std::string::npos)
            continue; 
        std::string file = extractFilename(part, 1);
        if (file.empty())
            continue;
        std::string content = extractContent(part);
        std::string folder = client.serverInfo.routes.at(client.request.location).upload_path;
        std::string path = folder;
        if (path.back() != '/')
            path += "/";
        path += file;
        lastPath = "." + path;
        std::ofstream out(lastPath.c_str(), std::ios::binary);
        if (!out.is_open())
        {
            wslog.writeToLogFile(ERROR, "500 Failed to open file for writing", DEBUG_LOGS);
            client.response.push_back(HTTPResponse(500, "Failed to open file for writing", client.serverInfo.error_pages));
            return;
        }
        out.write(content.c_str(), content.size());
        out.close();
    }
    if (lastPath.empty() || access(lastPath.c_str(), R_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "400 File not uploaded", DEBUG_LOGS);
        client.response.push_back(HTTPResponse(400, "File not uploaded", client.serverInfo.error_pages));
        return;
    }    
    std::string ext = getFileExtension(client.request.path);
    client.CGI.inputFilePath = lastPath;
    wslog.writeToLogFile(INFO, "POST (multi) File(s) uploaded successfully", DEBUG_LOGS);
}

int EventLoop::executeCGI(Client& client)
{
	if (client.request.fileUsed)
	{  
        client.CGI.tempFileName = "/tmp/tempCGIoutput_" + std::to_string(std::time(NULL)); 
		client.CGI.readCGIPipe[1] =  open(client.CGI.tempFileName.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		client.CGI.fileOpen = true;
        if (client.request.multipart)
            client.request.fileFd = open(client.CGI.inputFilePath.c_str(), O_RDONLY | O_CLOEXEC, 0644);
        else
		    client.request.fileFd = open(client.request.tempFileName.c_str(), O_RDONLY | O_CLOEXEC, 0644);
		if (client.request.fileFd == -1)
			return -1;
		client.CGI.writeCGIPipe[0] = client.request.fileFd;
	}
    if (access(client.CGI.fullPath.c_str(), F_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "CGIHandler::executeCGI file not found: " + client.CGI.fullPath, DEBUG_LOGS);
        return -404;
    }
    if (access(client.CGI.fullPath.c_str(), X_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "CGIHandler::executeCGI access to cgi script forbidden: " + client.CGI.fullPath, DEBUG_LOGS);
        return -403;
    }
    if (!client.request.fileUsed && (pipe2(client.CGI.writeCGIPipe, O_CLOEXEC) == -1 || pipe2(client.CGI.readCGIPipe, O_CLOEXEC) == -1))
	{
        return -500;
	}
    client.CGI.childPid = fork();
    if (client.CGI.childPid == -1)
        return -500;
    if (client.CGI.childPid == 0)
    {
		closeFds();
        dup2(client.CGI.writeCGIPipe[0], STDIN_FILENO);
        dup2(client.CGI.readCGIPipe[1], STDOUT_FILENO);
		if (!client.request.fileUsed)
		{
            if (client.CGI.writeCGIPipe[1] != -1)
			    close(client.CGI.writeCGIPipe[1]);
			client.CGI.writeCGIPipe[1] = -1;
            if (client.CGI.readCGIPipe[0] != -1)
			    close(client.CGI.readCGIPipe[0]);
			client.CGI.readCGIPipe[0] = -1;
		}
        execve(client.CGI.execveArgs[0], client.CGI.execveArgs.data(), client.CGI.envArray.data());
        std::exit(1);
    }
	if (!client.request.fileUsed)
	{
        if (client.CGI.writeCGIPipe[0] != -1)
		    close(client.CGI.writeCGIPipe[0]);
		client.CGI.writeCGIPipe[0] = -1;
        if (client.CGI.readCGIPipe[1] != -1)
		    close(client.CGI.readCGIPipe[1]);
		client.CGI.readCGIPipe[1] = -1;

		int flags = fcntl(client.CGI.writeCGIPipe[1], F_GETFL);
		fcntl(client.CGI.writeCGIPipe[1], F_SETFL, flags | O_NONBLOCK);
		flags = fcntl(client.CGI.readCGIPipe[0], F_GETFL);
		fcntl(client.CGI.readCGIPipe[0], F_SETFL, flags | O_NONBLOCK);
	}
	return 0;
}

void EventLoop::handleCGI(Client& client, uint32_t eventType)
{
    if (eventType & EPOLLIN)
    {
        int peek;
        int connection = recv(client.fd, &peek, sizeof(peek), MSG_DONTWAIT | MSG_PEEK);
        if (connection == 0)
        {
            wslog.writeToLogFile(DEBUG, "Closed client FD" + std::to_string(client.fd) + " within CGI", true);
            closeClient(client.fd);
            return ;
        }
    }

    if (client.request.body.empty())
    {
        if (client.CGI.writeCGIPipe[1] != -1)
        {
            close(client.CGI.writeCGIPipe[1]);
            client.CGI.writeCGIPipe[1] = -1;
        }
    }
    if (!client.request.fileUsed && client.request.body.empty() == false)
        client.CGI.writeBodyToChild(client.request);
    else if (client.request.fileUsed == false)
        client.CGI.collectCGIOutput(client.CGI.getReadPipe());
    pid = waitpid(client.CGI.getChildPid(), &status, WNOHANG);
    if (pid == client.CGI.getChildPid())
    {
        nChildren--;
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            wslog.writeToLogFile(ERROR, "500 Internal Server Error", DEBUG_LOGS);
            client.response.push_back( HTTPResponse(500, "Internal Server Error", client.serverInfo.error_pages));
            client.writeBuffer = client.response.back().toString();
            return ;
        }
        if (!client.request.fileUsed)
        {
            client.CGI.collectCGIOutput(client.CGI.getReadPipe());
            client.response.push_back(client.CGI.generateCGIResponse(client.serverInfo.error_pages));
            client.writeBuffer = client.response.back().toString();
        }
        if (!client.CGI.tempFileName.empty())
        {
            if (client.CGI.readCGIPipe[1] != -1)
                close(client.CGI.readCGIPipe[1]);
            client.CGI.readCGIPipe[1] = -1;
            client.CGI.fileOpen = false;
        }
        else
        {
            if (client.CGI.readCGIPipe[0] != -1)
                close(client.CGI.readCGIPipe[0]);
            client.CGI.readCGIPipe[0] = -1;
        }
        if (!client.request.fileUsed)
            client.request.isCGI = false;
        return ;
    }
}


static bool checkMethods(Client &client, int loop)
{
    if (!RequestHandler::isAllowedMethod(client.request.method, client.serverInfo.routes[client.request.location]))
    {
        client.state = SEND;
        wslog.writeToLogFile(ERROR, "405 Method not allowed", DEBUG_LOGS);
        client.response.push_back(HTTPResponse(405, "Method not allowed", client.serverInfo.error_pages));
        client.writeBuffer = client.response.back().toString();
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return false;
    }
    else
        return true;
}

static bool readChunkedBody(Client &client, int loop)
{
    client.chunkBuffer += client.rawReadData;
    if (client.request.fileUsed == false && client.request.isCGI == true)
    {
        client.request.tempFileName = "/tmp/tempSaveFile " + std::to_string(std::time(NULL));
        client.request.fileFd = open(client.request.tempFileName.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (client.request.fileFd == -1)
            wslog.writeToLogFile(ERROR, "Opening temporary file for chunked request failed", DEBUG_LOGS);
        else
        {
            client.request.fileUsed = true;
            client.request.fileIsOpen = true;
        }
    }
    if (client.chunkBuffer.find("0\r\n\r\n") != std::string::npos)
    {
        std::size_t endPos = client.chunkBuffer.find("0\r\n\r\n");
        if (endPos != std::string::npos)
        {
            std::string leftover = client.chunkBuffer.substr(endPos + 5);
            client.rawReadData.clear();
            client.rawReadData += leftover;
            client.chunkBuffer = client.chunkBuffer.substr(0, endPos + 5);
        }
        if (!validateChunkedBody(client))
        {
            if (client.request.isCGI == false)
            {
                if (checkMethods(client, loop) == false)
                    return true;
            }
            else
            {
                wslog.writeToLogFile(ERROR, "400 Bad request", DEBUG_LOGS);
                client.response.push_back(HTTPResponse(400, "Bad request", client.serverInfo.error_pages));
                client.writeBuffer = client.response.back().toString();
            }
            client.state = SEND;
            toggleEpollEvents(client.fd, loop, EPOLLOUT);
            return true;
        }
        if (client.request.fileUsed == true)
        {
            client.request.headers["Content-Length"] = std::to_string(client.chunkBodySize);
            if (client.request.fileIsOpen == true && client.request.fileFd != -1)
                close(client.request.fileFd);
        }
        if (client.request.isCGI == true)
            return true;
        client.state = SEND;
        client.response.push_back(RequestHandler::handleRequest(client));
        client.writeBuffer = client.response.back().toString();
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return true;
    }
    client.rawReadData.clear();
    return false;
}

int EventLoop::checkMaxSize(Client& client)
{
    size_t maxBodySize;
    auto ite = client.serverInfo.routes.find(client.request.location);
    if (ite != client.serverInfo.routes.end())
        maxBodySize = ite->second.client_max_body_size;
    else
        return -400;

    if (client.headerString.size() > DEFAULT_MAX_HEADER_SIZE)
    {
        wslog.writeToLogFile(DEBUG, "Request header too big", DEBUG_LOGS);
        return -413;
    }

    if (client.request.body.size() > maxBodySize)
    {
        wslog.writeToLogFile(DEBUG, "Request body too big, max body size = " + std::to_string(maxBodySize) + ", while body size = " + std::to_string(client.request.body.size()), DEBUG_LOGS);
        return -413;
    }
    
    return 0;
}

void EventLoop::checkBody(Client& client, uint32_t eventType)
{
    if (client.request.method == "POST")
    {
        auto TE = client.request.headers.find("Transfer-Encoding");
        if (TE != client.request.headers.end() && TE->second == "chunked")
        {
            if (readChunkedBody(client, loop) == false)
                return;
        }
        else
        {
            auto CL = client.request.headers.find("Content-Length");
            if (CL != client.request.headers.end() && client.rawReadData.size() >= stoul(CL->second))
            {
                client.request.body = client.rawReadData.substr(0, stoul(CL->second));
                client.rawReadData = client.rawReadData.substr(client.request.body.size());
            }
            else
                return ;
        }
    }

    int maxSizeStatus = checkMaxSize(client);
    if (maxSizeStatus < 0)
    {
        if (maxSizeStatus == -400)
            client.response.push_back(HTTPResponse(400, "Bad Request"));
        else if (maxSizeStatus == -413)
            client.response.push_back(HTTPResponse(413, "Payload Too Large"));
        client.erase = true;
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
    if (client.rawReadData.empty() == false)
    {
        wslog.writeToLogFile(ERROR, "501 Not implemented", DEBUG_LOGS);
        client.response.push_back(HTTPResponse(501, "Not implemented", client.serverInfo.error_pages));
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
    if (client.request.isCGI == true)
    {
        if (client.request.multipart)
            CGIMultipart(client);
        client.state = HANDLE_CGI;
        client.CGI.setEnvValues(client.request, client.serverInfo);
        int error = executeCGI(client);
        if (error < 0)
        {
            if (error == -500)
            {
                wslog.writeToLogFile(ERROR, "500 Internal Server Error", DEBUG_LOGS);
                client.response.push_back(HTTPResponse(500, "Internal Server Error", client.serverInfo.error_pages));
            }
            else if (error == -403)
            {
                wslog.writeToLogFile(ERROR, "403 Forbidden", DEBUG_LOGS);
                client.response.push_back(HTTPResponse(403, "Forbidden", client.serverInfo.error_pages));
            }
            else if (error == -404)
            {
                wslog.writeToLogFile(ERROR, "404 Not Found", DEBUG_LOGS);
                client.response.push_back(HTTPResponse(404, "Not Found", client.serverInfo.error_pages));
            }
            client.writeBuffer = client.response.back().toString();
            client.state = SEND;
            toggleEpollEvents(client.fd, loop, EPOLLOUT);
            return ;
        }
        nChildren++;
        handleCGI(client, eventType);
        return ;
    }
    else
    {
        client.response.push_back(RequestHandler::handleRequest(client));
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
}

bool EventLoop::validateRequestMethod(Client& client)
{
    if (client.request.method == "POST" || client.request.method == "DELETE" || client.request.method == "GET")
        return true;
    else
        return false;
}

void EventLoop::handleClientRecv(Client& client, uint32_t eventType)
{
    try {
        switch (client.state)
        {
            case IDLE:
            {
                client.state = READ;
                return ;
            }
            case READ:
            {
                client.bytesRead = 0;
                client.bytesSent = 0;
                char buffer[READ_BUFFER_SIZE];
                client.bytesRead = recv(client.fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
                wslog.writeToLogFile(INFO, "Bytes read = " + std::to_string(client.bytesRead), DEBUG_LOGS);
                if (client.bytesRead <= 0)
                {
                    if (client.bytesRead == 0)
                        wslog.writeToLogFile(DEBUG, "Client FD" + std::to_string(client.fd) + " disconnected", true);
                    if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                        throw std::runtime_error("epoll_ctl DEL failed in READ");
                    close(client.fd);
                    clients.erase(client.fd);
                    return ;
                }
                buffer[client.bytesRead] = '\0';
                std::string temp(buffer, client.bytesRead);
                client.rawReadData += temp;
                wslog.writeToLogFile(INFO, "Request received from client FD" + std::to_string(client.fd) + ":\n" + client.rawReadData, DEBUG_LOGS);
                if (client.headerString.empty() == true)
                {
                    size_t headerEnd = client.rawReadData.find("\r\n\r\n");
                    if (headerEnd != std::string::npos)
                    {
                        client.headerString = client.rawReadData.substr(0, headerEnd + 4);
                        client.findCorrectHost(client.headerString, client.serverInfoAll);
                        client.request = HTTPRequest(client.headerString, client.serverInfo);
                        if (validateHeader(client.request) == false || validateRequestMethod(client) == false)
                        {
                            wslog.writeToLogFile(ERROR, "Validate request method is not valid", DEBUG_LOGS);
                            if (validateRequestMethod(client) == false)
                            {
                                wslog.writeToLogFile(ERROR, "501 Not implemented", DEBUG_LOGS);
                                client.response.push_back(HTTPResponse(501, "Not implemented", client.serverInfo.error_pages));
                            }
                            else
                            {
                                wslog.writeToLogFile(ERROR, "400 Bad request", DEBUG_LOGS);
                                client.response.push_back(HTTPResponse(400, "Bad request", client.serverInfo.error_pages));
                            }
                            client.rawReadData.clear();
                            client.state = SEND;
                            client.writeBuffer = client.response.back().toString();
                            toggleEpollEvents(client.fd, loop, EPOLLOUT);
                            return ;
                        }
                        wslog.writeToLogFile(DEBUG, client.headerString, DEBUG_LOGS);
                        if (client.serverInfo.routes.find(client.request.location) == client.serverInfo.routes.end())
                        {
                            wslog.writeToLogFile(ERROR, "404 Invalid location", DEBUG_LOGS);
                            client.response.push_back(HTTPResponse(404, "Invalid location", client.serverInfo.error_pages));
                            client.rawReadData.clear();
                            client.state = SEND;
                            client.writeBuffer = client.response.back().toString();
                            toggleEpollEvents(client.fd, loop, EPOLLOUT);
                            return ;
                        }
                        client.bytesRead = 0;
                        client.rawReadData = client.rawReadData.substr(headerEnd + 4);
                        if (client.serverInfo.routes.at(client.request.location).redirect.status_code)
                        {
                            client.response.push_back(HTTPResponse(client.serverInfo.routes.at(client.request.location).redirect.status_code, client.serverInfo.routes.at(client.request.location).redirect.target_url, client.serverInfo.error_pages));
                            client.rawReadData.clear();
                            client.state = SEND;
                            client.writeBuffer = client.response.back().toString();
                            toggleEpollEvents(client.fd, loop, EPOLLOUT);
                            return ;
                        }
                    }
                }
                if (client.headerString.empty() == false)
                    checkBody(client, eventType);
                return ;
            }
            case HANDLE_CGI:
                return handleCGI(client, eventType);
            case SEND:
                return;
        }
    }
    catch (const std::invalid_argument& e)
    {
        wslog.writeToLogFile(ERROR, "Client FD" + std::to_string(client.fd) + " suffered from invalid_argument in RECV, sending an error response!", DEBUG_LOGS);
        wslog.writeToLogFile(ERROR, "500 Internal Server Error", DEBUG_LOGS);
        client.response.push_back(HTTPResponse(500, "Internal Server Error", client.serverInfo.error_pages));
        client.rawReadData.clear();
        client.state = SEND;
        client.writeBuffer = client.response.back().toString();
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
    catch (const std::bad_alloc& e)
    {
        wslog.writeToLogFile(ERROR, "Client FD" + std::to_string(client.fd) + " suffered from bad_alloc in RECV, sending an error response!", DEBUG_LOGS);
        wslog.writeToLogFile(ERROR, "500 Internal Server Error", DEBUG_LOGS);
        client.response.push_back(HTTPResponse(500, "Internal Server Error", client.serverInfo.error_pages));
        client.rawReadData.clear();
        client.state = SEND;
        client.writeBuffer = client.response.back().toString();
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
}

static bool checkBytesSent(Client &client)
{
    if (client.response.size() != 0)
    {
        int pos = client.response.back().toString().find("\r\n\r\n");
        std::string responseHeader = client.response.back().toString().substr(0, pos + 4);
        if (client.response.back().body.empty() == false)
        {
            if ((std::stoul(client.response.back().headers["Content-Length"]) + responseHeader.size() > client.bytesSent))
                return false;
        }
        else
            if (client.bytesSent != responseHeader.size())
                return false;
    }
    return true;
}

void EventLoop::handleClientSend(Client &client)
{
    try {
        if (client.state != SEND)
            return ;
        if (client.request.isCGI == true && client.CGI.tempFileName.empty() == false)
        {
            client.writeBuffer.clear();
            ssize_t bytesread = -1;
            if (client.CGI.fileOpen == false)
            {
                client.CGI.readCGIPipe[1] = open(client.CGI.tempFileName.c_str(), O_RDONLY);
                if (client.CGI.readCGIPipe[1] != -1)
                    client.CGI.fileOpen = true;
                char buffer[65536];
                bytesread = read(client.CGI.readCGIPipe[1], buffer, 1000);
                client.writeBuffer.append(buffer, bytesread);
                client.CGI.output = client.writeBuffer;
                client.response.push_back(client.CGI.generateCGIResponse(client.serverInfo.error_pages));
                client.writeBuffer = client.response.back().toString();
            }
            else
            {
                char buffer[65536];
                bytesread = read(client.CGI.readCGIPipe[1], buffer, 1000);
                client.writeBuffer.append(buffer, bytesread);
            }
            if (bytesread == -1)
            {
                wslog.writeToLogFile(ERROR, "500 Internal Server Error", DEBUG_LOGS);
                client.response.push_back(HTTPResponse(500, "Internal Server Error", client.serverInfo.error_pages));
                return;
            }
            else if (bytesread == 0)
            {
                close(client.CGI.readCGIPipe[1]);
                client.CGI.readCGIPipe[1] = -1;
            }
        }
        client.bytesWritten = send(client.fd, client.writeBuffer.c_str(), client.writeBuffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        wslog.writeToLogFile(DEBUG, "Response sent to client FD" + std::to_string(client.fd) + ":\n" + client.writeBuffer, DEBUG_LOGS);
        if (client.bytesWritten <= 0)
        {
            if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                throw std::runtime_error("check connection epoll_ctl DEL failed in SEND");
            wslog.writeToLogFile(DEBUG, "Closing client FD" + std::to_string(client.fd) + " because bytesWritten = " + std::to_string(client.bytesWritten), true);
            close(client.fd);
            clients.erase(client.fd);
            return ; 
        }
        client.bytesSent += client.bytesWritten;
        client.writeBuffer.erase(0, client.bytesWritten);
        if (checkBytesSent(client) == true)
        {
            wslog.writeToLogFile(DEBUG, "Response sent to client FD" + std::to_string(client.fd), true);
            client.response.pop_back();
            if (client.request.headers.find("Connection") != client.request.headers.end())
                checkConnection = client.request.headers.at("Connection");
            if (!checkConnection.empty())
            {
                if (checkConnection == "close" || checkConnection == "Close")
                {
                    if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                        throw std::runtime_error("check connection epoll_ctl DEL failed in SEND::close");
                    wslog.writeToLogFile(DEBUG, "Closing client FD" + std::to_string(client.fd) + " because of connection: close", true);
                    close(client.fd);
                    clients.erase(client.fd);
                }
                else
                {
                    wslog.writeToLogFile(INFO, "Client reset", DEBUG_LOGS);
                    client.reset();
                    toggleEpollEvents(client.fd, loop, EPOLLIN);
                }
            }
            else if (client.request.version == "HTTP/1.0")
            {
                if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                    throw std::runtime_error("check connection epoll_ctl DEL failed in SEND::http");
                wslog.writeToLogFile(DEBUG, "Closing client FD" + std::to_string(client.fd) + " because of http1.0", true);
                close(client.fd);
                clients.erase(client.fd);
            }
            else if (client.erase == true)
            {
                if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                    throw std::runtime_error("check connection epoll_ctl DEL failed in SEND::erase");
                wslog.writeToLogFile(DEBUG, "Closing client FD" + std::to_string(client.fd) + " because erase = true", true);
                close(client.fd);
                clients.erase(client.fd);
            }
            else
            {
                wslog.writeToLogFile(INFO, "Client reset", DEBUG_LOGS);
                client.reset();
                toggleEpollEvents(client.fd, loop, EPOLLIN);
            }
        }
    }
    
    catch (const std::invalid_argument& e)
    {
        closeClient(client.fd);
        wslog.writeToLogFile(ERROR, "Client FD" + std::to_string(client.fd) + " suffered from invalid_argument in SEND, closing client!", DEBUG_LOGS);
        return ;
    }
    catch (const std::bad_alloc& e)
    {
        closeClient(client.fd);
        wslog.writeToLogFile(ERROR, "Client FD" + std::to_string(client.fd) + " suffered from bad_alloc in SEND, closing client!", DEBUG_LOGS);
        return ;
    }
}