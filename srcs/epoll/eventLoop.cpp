#include "eventLoop.hpp"
#include "timeout.hpp"
#include "Client.hpp"
#include "HTTPResponse.hpp"
#include "RequestHandler.hpp"

void        eventLoop(std::vector<ServerConfig> servers);
static int  initServerSocket(ServerConfig server);
static int  acceptNewClient(int loop, int serverSocket, std::map<int, Client>& clients);
static void handleClientRecv(Client &client, int loop);
static void handleClientSend(Client &client, int loop);
static void toggleEpollEvents(int fd, int loop, uint32_t events);
static int  findOldestClient(std::map<int, Client>& clients);

void eventLoop(std::vector<ServerConfig> serverConfigs)
{
    std::map<int, ServerConfig> servers;
    std::map<int, Client> clients;
    int serverSocket;
    struct epoll_event setup;
    std::vector<epoll_event> eventLog(MAX_CONNECTIONS);

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
        std::cout << "New server #" << i << " connected, got FD " << newServer.fd << std::endl;
    }

    int timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerFd < 0)
        std::runtime_error("failed to create timerfd");
    std::cout << "Timerfd created, it got FD" << timerFd << std::endl;
    setup.data.fd = timerFd;
    epoll_ctl(loop, EPOLL_CTL_ADD, timerFd, &setup);
    struct itimerspec timerValues { };
    timerValues.it_value.tv_sec = TIMEOUT;
    timerValues.it_interval.tv_sec = TIMEOUT / 2;
    bool timerOn = false;

    while (true)
    {
        int nReady = epoll_wait(loop, eventLog.data(), MAX_CONNECTIONS, -1);
        if (nReady == -1)
            throw std::runtime_error("epoll_wait failed");
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
                std::cout << "New client with FD "<< clients[clientFd].fd << " connected to server with FD " << serverSocket << std::endl;
                if (timerOn == false)
                {
                    timerfd_settime(timerFd, 0, &timerValues, 0); //start timeout timer
                    timerOn = true;
                }
            }

            else if (fd == timerFd)
            {
                std::cout << "Time to check timeouts!" << std::endl;
                checkTimeouts(timerFd, clients);
            }

            else
            {
                Client& client = clients[fd];
                if (eventLog[i].events & EPOLLIN)
                {
                    std::cout << "EPOLLIN" << std::endl;
                    client.timestamp = std::chrono::steady_clock::now();
                    handleClientRecv(client, loop);
                }
                if (eventLog[i].events & EPOLLOUT)
                {
                    std::cout << "EPOLLOUT" << std::endl;
                    client.timestamp = std::chrono::steady_clock::now();
                    handleClientSend(client, loop);
                }
                if (client.erase == true)
                {
                    std::cout << "client erased" << std::endl;
                    clients.erase(client.fd);
                }
            }
        }
    }
}

static int initServerSocket(ServerConfig server)
{
    int serverSocket = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //REMOVE LATER

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(server.port);
    bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress));
    listen(serverSocket, SOMAXCONN);

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
            if (epoll_ctl(loop, EPOLL_CTL_DEL, oldFd, nullptr) < 0)
                throw std::runtime_error("oldFd epoll_ctl DEL failed");
            close(oldFd);
            clients.at(oldFd).reset();
            newFd = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLen);
        }
        else
            throw std::runtime_error("accept failed");
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

static bool isUnsignedLongLong(std::string str) 
{
    std::stringstream ss(str);
    long long unsigned value; 
    ss >> value; 
    return !ss.fail();
}

static long long unsigned StrToUnsignedLongLong(std::string str) 
{
    std::stringstream ss(str);
    long long unsigned value; 
    ss >> value;
    return value;
}


static bool validateChunkedBody(Client &client)
{
    long long unsigned bytes;
    std::string str = client.chunkBuffer;
    while (true)
    {
        if (!isUnsignedLongLong(str))
        {
            return false;
        }
        bytes = StrToUnsignedLongLong(str);
        int i = 0;
        while (str[i] != '\r')
        {
            if (!::isdigit(str[i]))
                return false;
            i++;
        }
        if (bytes == 0)
        {
            if (str.substr(1, 4) == "\r\n\r\n")
                return true;
            else
                return false;
        }
        if (str[i + 1] != '\n')
            return false;
        str = str.substr(i + 2);
        str = str.substr(bytes);
        if (str.substr(0, 2) != "\r\n")
            return false;
        else
            str = str.substr(2);
    }
    return true;
}

static void readChunkedBody(Client &client, int loop)
{
    client.chunkBuffer += client.rawReadData;
    client.rawReadData.clear();

    if (client.chunkBuffer.size() >= 5 && client.chunkBuffer.substr(client.chunkBuffer.size() - 5) == "0\r\n\r\n")
    {
        validateChunkedBody(client);
        client.state = SEND;  // Kaikki chunkit luettu
        client.request.body = client.chunkBuffer;  // Tallenna body
        client.chunkBuffer = "";
        client.response = RequestHandler::handleRequest(client);
        if (client.response.getStatusCode() >= 400)
            client.response = client.response.generateErrorResponse(client.response);
        client.writeBuffer = client.response.toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return;
    }
}

static void checkBody(Client &client, int loop)
{
    if (client.request.headers["Transfer-Encoding"] == "chunked")
        readChunkedBody(client, loop);
    else if (client.rawReadData.size() >= stoul(client.request.headers["Content-Length"])) //or end of chunks?
    {
        client.request.body = client.rawReadData;
        client.response = RequestHandler::handleRequest(client);
        if (client.response.getStatusCode() >= 400)
            client.response = client.response.generateErrorResponse(client.response);
        client.writeBuffer = client.response.toString();
        client.state = SEND;
        toggleEpollEvents(client.fd, loop, EPOLLOUT);
        return ;
    }
}


static void handleClientRecv(Client& client, int loop)
{
    switch (client.state)
    {
        case IDLE:
            client.state = READ_HEADER;

        case READ_HEADER:
        {
            std::cout << "IN READ_HEADER" << std::endl;
            client.bytesRead = 0;
            char buffer[READ_BUFFER_SIZE];
            client.bytesRead = recv(client.fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);

            if (client.bytesRead < 0)
            {
                close(client.fd);
                client.erase = true;
                // if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                //     throw std::runtime_error("epoll_ctl DEL failed");
                return ;
            }

			if (client.bytesRead == 0)
            {
                std::cout << "Client disconnected FD" << client.fd << std::endl;
                close(client.fd);
                client.erase = true;
                // if (epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr) < 0)
                //     throw std::runtime_error("epoll_ctl DEL failed");
                return ;
            }

            buffer[client.bytesRead] = '\0';
            std::string temp(buffer, client.bytesRead);
            client.rawReadData += temp;
            if (client.bytesRead >= 4)
            {
                size_t headerEnd = client.rawReadData.find("\r\n\r\n");
                if (headerEnd != std::string::npos) 
                {
                        client.headerString = client.rawReadData.substr(0, headerEnd + 4);
                        client.request = HTTPRequest(client.headerString);
                        client.bytesRead = 0;
                        client.rawReadData = client.rawReadData.substr(headerEnd + 4);
                        if (client.request.method == "POST")
                        {
                            client.state = READ_BODY;
                            checkBody(client, loop);
                            return;
                        }
                        else
                        {
                            client.state = SEND;
                            client.request.body = client.rawReadData;
                            HTTPResponse response = RequestHandler::handleRequest(client);
                            client.writeBuffer = response.toString();
                            toggleEpollEvents(client.fd, loop, EPOLLOUT);
                            return ;
                        }
                }
                else
                    return ;
            }
            else
                return ;
        }

        case READ_BODY:
        {
            client.bytesRead = 0;
            char buffer2[READ_BUFFER_SIZE];
            client.bytesRead = recv(client.fd, buffer2, sizeof(buffer2) - 1, MSG_DONTWAIT);
            if (client.bytesRead == 0)
            {
				// Client disconnected
                std::cout << "ClientFD disconnected " << client.fd << std::endl;
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
        }

		case SEND:
			return;
    }
}

static void handleClientSend(Client &client, int loop)
{
    std::cout << "IN SEND" << std::endl;
    if (client.state != SEND)
        return ;
    client.bytesWritten = send(client.fd, client.writeBuffer.data(), client.writeBuffer.size(), MSG_DONTWAIT);
    if (client.bytesWritten == 0)
    {
        close(client.fd);
        client.erase = true;
        // epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr);
        return ;
    }
    if (client.bytesWritten < 0) {
        close(client.fd);
        client.erase = true;
        // epoll_ctl(loop, EPOLL_CTL_DEL, client.fd, nullptr);
        return;
    }
    client.writeBuffer.erase(0, client.bytesWritten);
    if (client.writeBuffer.empty())
    {
        std::string checkConnection = client.request.headers["Connection"];
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
                std::cout << "Client reset" << std::endl;
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
            std::cout << "Client reset" << std::endl;
            client.reset();
            toggleEpollEvents(client.fd, loop, EPOLLIN);
        }
    }
}

static void toggleEpollEvents(int fd, int loop, uint32_t events) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    if (epoll_ctl(loop, EPOLL_CTL_MOD, fd, &ev) < 0)
        throw std::runtime_error("epoll_ctl MOD failed");
}
