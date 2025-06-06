#include "EventLoop.hpp"

void print_fd_flags(int fd) {
    int fd_flags = fcntl(fd, F_GETFD);
    int fl_flags = fcntl(fd, F_GETFL);

    std::cout << "FD " << fd << " F_GETFD: " << fd_flags;
    if (fd_flags & FD_CLOEXEC) std::cout << " (FD_CLOEXEC)";
    std::cout << std::endl;

    std::cout << "FD " << fd << " F_GETFL: " << fl_flags;
    if (fl_flags & O_NONBLOCK) std::cout << " (O_NONBLOCK)";
    if ((fl_flags & O_ACCMODE) == O_RDONLY) std::cout << " (O_RDONLY)";
    if ((fl_flags & O_ACCMODE) == O_WRONLY) std::cout << " (O_WRONLY)";
    if ((fl_flags & O_ACCMODE) == O_RDWR)   std::cout << " (O_RDWR)";
    std::cout << std::endl;
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

static void toggleEpollEvents(int fd, int loop, uint32_t events)
{
    struct epoll_event ev {};
    ev.data.fd = fd;
    ev.events = events;
    if (epoll_ctl(loop, EPOLL_CTL_MOD, fd, &ev) < 0)
        throw std::runtime_error("epoll_ctl MOD failed " + std::to_string(errno));
}

EventLoop::EventLoop(std::vector<ServerConfig> serverConfigs) : eventLog(MAX_CONNECTIONS), timerValues {}
{
    signal(SIGPIPE, handleSignals);
    signal(SIGINT, handleSignals);
    struct epoll_event setup {};
    nChildren = 0;
    loop = epoll_create1(0);
    for (size_t i = 0; i < serverConfigs.size(); i++)
    {
        serverSocket = initServerSocket(serverConfigs[i]);
        serverConfigs[i].fd = serverSocket;
        setup.data.fd = serverConfigs[i].fd;
        setup.events = EPOLLIN;
        if (epoll_ctl(loop, EPOLL_CTL_ADD, serverSocket, &setup) < 0)
            throw std::runtime_error("serverSocket epoll_ctl ADD failed");
        servers[serverSocket] = serverConfigs[i];
    }
    timerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerFD < 0)
        std::runtime_error("failed to create timerfd");
    setup.data.fd = timerFD;
    if (epoll_ctl(loop, EPOLL_CTL_ADD, timerFD, &setup) < 0)
        throw std::runtime_error("Failed to add timerFd to epoll");
    timerOn = false;
    childTimerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (childTimerFD < 0)
        std::runtime_error("failed to create childTimerFD");
    setup.data.fd = childTimerFD;
    if (epoll_ctl(loop, EPOLL_CTL_ADD, childTimerFD, &setup) < 0)
        throw std::runtime_error("Failed to add childTimerFD to epoll");
}

void EventLoop::closeFds()
{
    close(timerFD);
    close(childTimerFD);
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

void EventLoop::startLoop()
{
    
    while (signum == 0)
    {
        // print_fd_flags(timerFD);
        int nReady = epoll_wait(loop, eventLog.data(), MAX_CONNECTIONS, -1);
        if (nReady == -1)
        {
            if (errno == EINTR)
            {
                wslog.writeToLogFile(INFO, "epoll_wait interrupted by signal", true);
                if (signum != 0)
                    break ;
                else
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
                struct epoll_event setup {};
                Client newClient(loop, fd, clients, servers[fd]);
                auto result =  clients.emplace(newClient.fd, std::move(newClient));
                if (!result.second)
                    throw std::runtime_error("Client insert failed or duplicate fd");
                setup.data.fd = newClient.fd;
                setup.events = EPOLLIN;
                if (epoll_ctl(loop, EPOLL_CTL_ADD, newClient.fd, &setup) < 0)
                    throw std::runtime_error("newClient epoll_ctl ADD failed");
                if (timerOn == false)
                    setTimerValues(1);
                wslog.writeToLogFile(INFO, "Creating a new client FD" + std::to_string(newClient.fd), true);
            }
            else if (fd == timerFD)
            {
                if (clients.empty())
                    setTimerValues(2);
                else
                    checkTimeouts();
            }
            else if (fd == childTimerFD)
            {
                checkChildrenStatus();
                if (nChildren == 0)
                    setTimerValues(3);
            }
            else if (clients.find(fd) != clients.end())
            {
                if (eventLog[i].events & EPOLLIN)
                {
                    std::cout << "EPOLLIN\n";
                    clients.at(fd).timestamp = std::chrono::steady_clock::now();
                    handleClientRecv(clients.at(fd));
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

void EventLoop::setTimerValues(int n)
{
    if (n == 1)
    {
        timerValues.it_value.tv_sec = TIMEOUT;
        timerValues.it_interval.tv_sec = TIMEOUT / 2; 
        timerfd_settime(timerFD, 0, &timerValues, 0); //start timeout timer
        timerOn = true;
    }
    else if (n == 2)
    {
        wslog.writeToLogFile(INFO, "No more clients connected, not checking timeouts anymore until new connections", true);
        timerValues.it_value.tv_sec = 0;
        timerValues.it_interval.tv_sec = 0;
        timerfd_settime(timerFD, 0, &timerValues, 0); //stop timer
        timerOn = false;
    }
    if (n == 3)
    {
        timerValues.it_value.tv_sec = 0;
        timerValues.it_interval.tv_sec = 0;
        wslog.writeToLogFile(INFO, "no children left, not checking their status anymore", true);
        timerfd_settime(childTimerFD, 0, &timerValues, 0);
    }
}

void EventLoop::checkTimeouts()//int timerFd, std::map<int, Client>& clients, int& children, int loop)
{
    uint64_t tempBuffer;
    ssize_t bytesRead = read(timerFD, &tempBuffer, sizeof(tempBuffer)); //reading until timerfd event stops
    if (bytesRead != sizeof(tempBuffer))
        throw std::runtime_error("timerfd recv failed");
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
        if ((client.state == READ && client.headerString.size() > 8192) || (client.state == READ && (client.rawReadData.size() - client.headerString.size()) > client.serverInfo.client_max_body_size))
        {
            createErrorResponse(client, 413, "Entity too large", " disconnected, size too big!");
            continue ;
        }
        if (client.state == SEND && elapsedTime > 0) //make more comprehensive later
        {
            int dataSent = client.previousDataAmount - client.writeBuffer.size();
            int dataRate = dataSent / elapsedTime;
            if (client.writeBuffer.size() > 1024 && dataRate < 1024) //what is proper amount?
            {
                wslog.writeToLogFile(INFO, "Client " + std::to_string(client.fd) + " disconnected, client received data too slowly!", true);
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
    client.response.push_back(HTTPResponse(code, msg));
    client.writeBuffer = client.response.back().toString();
    client.bytesWritten = send(client.fd, client.writeBuffer.data(), client.writeBuffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    wslog.writeToLogFile(INFO, "Client " + std::to_string(client.fd) + logMsg, true);
    closeClient(client.fd);
}

void EventLoop::closeClient(int fd)//Client& client, std::map<int, Client>& clients, int& children, int loop)
{
    if (epoll_ctl(loop, EPOLL_CTL_DEL, fd, nullptr) < 0)
        throw std::runtime_error("timeout epoll_ctl DEL failed in closeClient");
    if (clients.at(fd).request.isCGI == true)
        nChildren--;
    // if (clients.at(fd).fd != -1)
    close(clients.at(fd).fd);
    // clients.at(fd).fd = -1;
    clients.erase(clients.at(fd).fd);
    wslog.writeToLogFile(INFO, "Client FD" + std::to_string(fd) + " closed!", true);
}

void EventLoop::checkChildrenStatus()//int timerFd, std::map<int, Client>& clients, int loop, int& children)
{
    uint64_t tempBuffer;
    ssize_t bytesRead = read(childTimerFD, &tempBuffer, sizeof(tempBuffer)); //reading until childtimerfd event stops
     std::string errmsg = strerror(errno);
        wslog.writeToLogFile(DEBUG, "after read: " + errmsg, true);
    if (bytesRead != sizeof(tempBuffer))
        throw std::runtime_error("childTimerFd recv failed");
    for (auto it = clients.begin(); it != clients.end(); ++it)
    {
        auto& client = it->second;
        if (nChildren > 0 && client.request.isCGI == true)
        {
            handleClientRecv(client);
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
        wslog.writeToLogFile(DEBUG, "size of chunkbuffer " + std::to_string(client.chunkBuffer.size()), true);
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
        if (client.request.fileUsed == true && client.request.isCGI == true) 
        {
            // Write existing body to file
            write(client.request.fileFd, str.substr(0, bytes).data(), bytes);
        }
        else
            client.request.body += str.substr(0, bytes);
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

int EventLoop::executeCGI(Client& client, ServerConfig server)
{
    wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI called", true);
    wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI fullPath is: " + client.CGI.fullPath, true);
	if (client.request.fileUsed)
	{
		client.CGI.tempFileName = "/tmp/tempCGIouput_" + std::to_string(std::time(NULL)); 
		client.CGI.readCGIPipe[1] =  open(client.CGI.tempFileName.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		client.CGI.fileOpen = true;
		client.request.fileFd = open(client.request.tempFileName.c_str(), O_RDONLY | O_CLOEXEC, 0644);
		if (client.request.fileFd == -1)
		{
			/// error
			return -1;
		}
		client.CGI.writeCGIPipe[0] = client.request.fileFd;
	}
    if (access(client.CGI.fullPath.c_str(), X_OK) != 0)
    {
        wslog.writeToLogFile(ERROR, "CGIHandler::executeCGI access to cgi script forbidden: " + client.CGI.fullPath, true);
        return -403;
    }
    if (!client.request.fileUsed && (pipe2(client.CGI.writeCGIPipe, O_CLOEXEC) == -1 || pipe2(client.CGI.readCGIPipe, O_CLOEXEC) == -1))
	{
        return -500;
	}
    wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI pipes created", true);
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
        execve(server.routes[client.request.location].cgiexecutable.c_str(), client.CGI.exceveArgs, client.CGI.envArray);
        _exit(1);
    }
	if (!client.request.fileUsed)
	{
		wslog.writeToLogFile(ERROR, "Closing writeCGIPipe[0] FD = " + std::to_string(client.CGI.writeCGIPipe[0]), true);
		wslog.writeToLogFile(ERROR, "Closing readCGIPipe[1] FD = " + std::to_string(client.CGI.readCGIPipe[1]), true);
        if (client.CGI.writeCGIPipe[0] != -1)
		    close(client.CGI.writeCGIPipe[0]);
		client.CGI.writeCGIPipe[0] = -1;
        if (client.CGI.readCGIPipe[1] != -1)
		    close(client.CGI.readCGIPipe[1]);
		client.CGI.readCGIPipe[1] = -1;

		int flags = fcntl(client.CGI.writeCGIPipe[1], F_GETFL); //save the previous flags if any
		fcntl(client.CGI.writeCGIPipe[1], F_SETFL, flags | O_NONBLOCK); //add non-blocking flag
		flags = fcntl(client.CGI.readCGIPipe[0], F_GETFL);
		fcntl(client.CGI.readCGIPipe[0], F_SETFL, flags | O_NONBLOCK);
	}
	return 0;
}

void EventLoop::handleCGI(Client& client)
{
    if (client.request.body.empty())
    {
        if (client.CGI.writeCGIPipe[1] != -1)
            close(client.CGI.writeCGIPipe[1]);
        client.CGI.writeCGIPipe[1] = -1;
    }
    if (!client.request.fileUsed && client.request.body.empty() == false)
    {
        std::cout << "WRITING\n"; //REMOVE LATER
        client.CGI.writeBodyToChild(client.request);
    }
    else if (client.request.fileUsed == false)
    {
        std::cout << "READING\n"; //REMOVE LATER
        client.CGI.collectCGIOutput(client.CGI.getReadPipe());
    }
    pid = waitpid(client.CGI.getChildPid(), &status, WNOHANG);
    wslog.writeToLogFile(DEBUG, "Handling CGI for client FD: " + std::to_string(client.fd), true);
    wslog.writeToLogFile(DEBUG, "client.childPid is: " + std::to_string(client.CGI.getChildPid()), true);
    wslog.writeToLogFile(DEBUG, "waitpid returned: " + std::to_string(pid), true);
    if (pid == client.CGI.getChildPid())
    {
        wslog.writeToLogFile(DEBUG, "CGI process finished", true);
        nChildren--;
        client.request.isCGI = false;
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            wslog.writeToLogFile(DEBUG, "Child failed!", true);
            client.response.push_back( HTTPResponse(500, "Internal Server Error"));
            client.writeBuffer = client.response.back().body;
            return ;
        }
        if (!client.request.fileUsed)
            client.CGI.collectCGIOutput(client.CGI.getReadPipe());
        client.response.push_back(client.CGI.generateCGIResponse());
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
        client.state = SEND;
        if (!client.request.fileUsed)
            client.request.isCGI = false;
        client.writeBuffer = client.response.back().toString();
        return ;
    }
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

static void readChunkedBody(Client &client, int loop)
{
    client.chunkBuffer += client.rawReadData;
    if (client.request.fileUsed == false && client.request.isCGI == true) // 1MB limit for chunked body
    {
        client.request.tempFileName = "/tmp/tempSaveFile " + std::to_string(std::time(NULL));
        client.request.fileFd = open(client.request.tempFileName.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (client.request.fileFd == -1)
            wslog.writeToLogFile(ERROR, "Opening temporary file for chunked request failed", true);
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
            // ota kaikki alusta "0\r\n\r\n" asti
            client.chunkBuffer = client.chunkBuffer.substr(0, endPos + 5);
        }
        if (!validateChunkedBody(client))
        {
            if (client.request.isCGI == false)
            {
                if (checkMethods(client, loop) == false)
                    return ;
            }
            else
            {
                client.response.push_back(HTTPResponse(400, "Bad request"));
                client.writeBuffer = client.response.back().toString();
            }
            client.state = SEND;
            toggleEpollEvents(client.fd, loop, EPOLLOUT);
            return ;
        }
        if (client.request.fileUsed == true)
        {
            struct stat st;
            if (stat(client.request.tempFileName.c_str(), &st) == 0)
                client.request.headers["Content-Length"] = std::to_string(st.st_size);
            else
                client.request.headers["Content-Length"] = "0";
            if (client.request.fileIsOpen == false && client.request.fileFd != -1)
                close(client.request.fileFd);
        }
        if (client.request.isCGI == true)
            return ;
        client.state = SEND;  // Kaikki chunkit luettu
        client.response.push_back(RequestHandler::handleRequest(client));
        client.writeBuffer = client.response.back().toString();
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return;
    }
    client.rawReadData.clear();
}

void EventLoop::checkBody(Client& client)
{
    if (!client.rawReadData.empty() && client.request.method == "POST")
    {
        auto TE = client.request.headers.find("Transfer-Encoding");
        if (TE != client.request.headers.end() && TE->second == "chunked")
            return readChunkedBody(client, loop);
        auto CL = client.request.headers.find("Content-Length");
        if (CL != client.request.headers.end() && client.rawReadData.size() >= stoul(CL->second)) //or end of chunks?
        {
            client.request.body = client.rawReadData;
            client.rawReadData = client.rawReadData.substr(client.request.body.size());
        }
        else
            return ;
    }
    if (client.request.isCGI == true)
    {
        std::cout << "CGI IS TRUE\n";
        client.state = HANDLE_CGI;
        client.CGI.setEnvValues(client.request, client.serverInfo);
        if (checkMethods(client, loop) == false)
            return ;
        int error = executeCGI(client, client.serverInfo);
        if (error < 0)
        {
            if (error == -500)
                client.response.push_back(HTTPResponse(500, "Internal Server Error"));
            else if (error == -403)
                client.response.push_back(HTTPResponse(403, "Forbidden"));
            client.writeBuffer = client.response.back().body;
            client.state = SEND;
            toggleEpollEvents(client.fd, loop, EPOLLOUT);
            return ;
        }
        if (nChildren == 0)
        {
            timerValues.it_value.tv_sec = CHILD_CHECK;
            timerValues.it_interval.tv_sec = CHILD_CHECK;
            timerfd_settime(childTimerFD, 0, &timerValues, 0);
            wslog.writeToLogFile(INFO, "ChildTimer turned on", true);
        }
        nChildren++;
        handleCGI(client);
        return ;
    }
    else
    {
        std::cout << "CGI IS FALSE\n";
        client.response.push_back(RequestHandler::handleRequest(client));
        client.writeBuffer = client.response.back().toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
}

void EventLoop::handleClientRecv(Client& client)
{
    switch (client.state)
    {
        case IDLE:
        {
            wslog.writeToLogFile(INFO, "IN IDLE", true);
            client.state = READ;        
            return ;
        }
        case READ:
        {
            client.bytesRead = 0;
            char buffer[READ_BUFFER_SIZE];
            // wslog.writeToLogFile(INFO, "READING HEADER", true);
            client.bytesRead = recv(client.fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
            // wslog.writeToLogFile(INFO, "Bytes read = " + std::to_string(client.bytesRead), true);
            if (client.bytesRead <= 0)
            {
                if (client.bytesRead == 0)
                    wslog.writeToLogFile(INFO, "Client disconnected FD" + std::to_string(client.fd), true);
                // client.erase = true;
                if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                {
                    std::cout << "errno = " << errno << std::endl;
                    throw std::runtime_error("epoll_ctl DEL failed in READ");
                }
                if (client.fd != -1)
                    close(client.fd);
                clients.erase(client.fd);
                client.fd = -1;
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
                    client.rawReadData.clear();
                    client.state = SEND;
                    client.writeBuffer = client.response.back().toString();
                    toggleEpollEvents(client.fd, loop, EPOLLOUT);
                    return ;
                }
                client.bytesRead = 0;
                client.rawReadData = client.rawReadData.substr(headerEnd + 4);
            }
            checkBody(client);
            return ;
        }
        case HANDLE_CGI:
        {
            wslog.writeToLogFile(INFO, "IN HANDLE CGI", true);
            return handleCGI(client);
        }
        case SEND:
            return;
    }
}

void EventLoop::handleClientSend(Client &client)
{
    if (client.state != SEND)
        return ;
    if (client.rawReadData.empty() == false)
    {
        client.response.front() = HTTPResponse(501, "Not implemented");
        client.writeBuffer = client.response.front().toString();
    }
    wslog.writeToLogFile(INFO, "IN SEND", true);
    wslog.writeToLogFile(INFO, "To be sent = " + client.writeBuffer + " to client FD" + std::to_string(client.fd), true);
    client.bytesWritten = send(client.fd, client.writeBuffer.data(), client.writeBuffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    wslog.writeToLogFile(INFO, "Bytes sent = " + std::to_string(client.bytesWritten), true);
    if (client.bytesWritten <= 0)
    {
        if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
            throw std::runtime_error("check connection epoll_ctl DEL failed in SEND");
        // if (client.fd != -1)
        close(client.fd);
        // client.fd = -1;
        clients.erase(client.fd);
        return ; 
    }
    client.writeBuffer.erase(0, client.bytesWritten);
    // wslog.writeToLogFile(INFO, "Remaining to send = " + std::to_string(client.writeBuffer.size()), true);
    if (client.writeBuffer.empty())
    {
        if (client.request.headers.find("Connection") != client.request.headers.end())
            checkConnection = client.request.headers.at("Connection");
        if (!checkConnection.empty())
        {
            if (checkConnection == "close" || checkConnection == "Close")
            {
                if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                    throw std::runtime_error("check connection epoll_ctl DEL failed in SEND::close");
                // if (client.fd != -1)
                close(client.fd);
                // client.fd = -1;
                clients.erase(client.fd);
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
            client.erase = true;
            if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                throw std::runtime_error("check connection epoll_ctl DEL failed in SEND::http");
            // if (client.fd != -1)
            close(client.fd);
            // client.fd = -1;
            clients.erase(client.fd);
        }
        else
        {
            // wslog.writeToLogFile(INFO, "Client reset", true);
            client.reset();
            toggleEpollEvents(client.fd, loop, EPOLLIN);
        }
    }
}