#pragma once
#include <vector>
#include <map>
#include <queue>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <csignal>
#include <cstdlib>
#include <stdexcept>
#include "Parser.hpp"
#include "Client.hpp"

#define MAX_CONNECTIONS 1000
#define TIMEOUT 30 //testing only, increase later to 60
#define CHILD_CHECK 3


void eventLoop(std::vector<ServerConfig> serverConfigs);
void handleClientRecv(Client& client, int loop);
bool handleCGI(Client& client);