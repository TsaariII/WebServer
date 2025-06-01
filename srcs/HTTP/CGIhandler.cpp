#include "CGIhandler.hpp"
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>

std::string join_paths(std::filesystem::path path1, std::filesystem::path path2);

CGIHandler::CGIHandler() 
{
	writeFileOpen = false;
	cgiReady = false;
	cgiTempFile = "/tmp/webserv_temp_cgi_file";
}

int CGIHandler::getWritePipe() { return writeCGIPipe[1]; }

int CGIHandler::getChildPid() { return childPid; }

// std::string  join_paths(const std::string& a, const std::string& b)
// {
// 	if (a.empty()) return b;
// 	if (b.empty()) return a;
// 	if (a[a.size() - 1] == '/' && b[0] =='/')
// 		return a + b.substr(1);
// 	if (a[a.size() -1] != '/' && b[0] != '/')
// 		return a + "/" + b;
// 	return a + b;
// }

void CGIHandler::setEnvValues(HTTPRequest& request, ServerConfig server)
{
	std::string server_name = server.server_names.empty() ? "localhost"
			: server.server_names.at(0);
	char absPath[PATH_MAX];
	std::string localPath = join_paths(server.routes.at(request.location).abspath, request.file);
	fullPath = "." + localPath;
	realpath(fullPath.c_str(), absPath);
	envVariables.clear();
	std::string PATH_INFO = request.pathInfo.empty() ? request.path : request.pathInfo;
	envVariables = {"REQUEST_METHOD=" + request.method,
					"SCRIPT_FILENAME=" + std::string(absPath),
					"SCRIPT_NAME=" + request.path,
					"QUERY_STRING=" + request.query,
					"PATH_INFO=" + PATH_INFO,
					"REDIRECT_STATUS=200",
					"SERVER_PROTOCOL=HTTP/1.1",
					"GATEWAY_INTERFACE=CGI/1.1",
					"REMOTE_ADDR=" + server.host,
					"SERVER_NAME=" + server_name,
					"SERVER_PORT=" + server.port};
	std::string conType =  request.headers.count("Content-Type") > 0 ? request.headers.at("Content-Type") : "text/plain";
	envVariables.push_back("CONTENT_TYPE=" + conType);
	if (request.headers.count("Transfer-Encoding") > 0 &&
		request.headers.at("Transfer-Encoding") == "chunked" &&
		request.BodyFd != -1)
	{
		struct stat st;
		if (stat(request.tempBodyFile.c_str(), &st) == 0)
			envVariables.push_back("CONTENT_LENGTH=" + std::to_string(st.st_size));
		else
			envVariables.push_back("CONTENT_LENGTH=0");
	}
	else
	{
		std::string conLen = request.headers.count("Content-Length") > 0 ? request.headers.at("Content-Length") : "0";
		envVariables.push_back("CONTENT_LENGTH=" + conLen);
	}
	for (size_t i = 0; i < envVariables.size(); i++)
	{
		envArray[i] = (char *)envVariables.at(i).c_str();
		wslog.writeToLogFile(DEBUG, "CGIHandler::setEnvValues envArray[" + std::to_string(i) + "] = " + envVariables.at(i), true);
	}
	envArray[envVariables.size()] = NULL;
	exceveArgs[0] = (char *)server.routes.at(request.location).cgiexecutable.c_str();
	exceveArgs[1] = absPath;
	exceveArgs[2] = NULL;
}

HTTPResponse CGIHandler::generateCGIResponse()
{
	// std::cout << "GENERATING CGI RESPONSE\n";
	std::string::size_type end = output.find("\r\n\r\n");
	if (end == std::string::npos)
		return HTTPResponse(500, "Invalid CGI output");
	std::string headers = output.substr(0, end);
	std::string body = output.substr(end + 4);
	HTTPResponse res(200, "OK");
	res.body = body;
	std::istringstream header(headers);
	std::string line;
	while (std::getline(header, line))
	{
		if (line.back() == '\r')
			line.pop_back();
		size_t colon = line.find(':');
		if (colon != std::string::npos)
			res.headers[line.substr(0, colon)] = line.substr(colon + 2);
	}
	res.headers["Content-Length"] = std::to_string(res.body.size());
	return res;
}

bool CGIHandler::isFdReadable(int fd) 
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0; // Non-blocking

    int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
    if (ret > 0 && FD_ISSET(fd, &readfds)) {
        // Data is available to read
        return true;
    }
    return false; // No data available
}

bool CGIHandler::isFdWritable(int fd) 
{
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(fd, &writefds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0; // Non-blocking

    int ret = select(fd + 1, NULL, &writefds, NULL, &timeout);
    if (ret > 0 && FD_ISSET(fd, &writefds)) {
        // FD is writable
        return true;
    }
    return false; // Not writable
}

void CGIHandler::collectCGIOutput(int readFd)
{
    char buffer[4096];
    ssize_t n;

    while ((n = read(readFd, buffer, sizeof(buffer))) > 0)
        output.append(buffer, n);
}

int CGIHandler::executeCGI(HTTPRequest& request, ServerConfig server)
{
	// wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI called", true);
	// wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI fullPath is: " + fullPath, true);
	// wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI pipes created", true);
	if (request.tempFileOpen)
	{
		cgiTempFile = "/tmp/webserv_temp_cgi_response_file";
		tempBodyFd = open(cgiTempFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (tempBodyFd == -1)
		{
			wslog.writeToLogFile(ERROR, "Failed to create temporary file for CGI body", true);
			return -1;
		}
		writeFileFd = tempBodyFd;
		readFileFd = request.BodyFd;
	}
	else
	{
		if (pipe(writeCGIPipe) == -1 || pipe(readCGIPipe) == -1)
			return -1;
	}
	// needs to be here because of the way of setEnvValues it checks the body length of file if its chunked request
	setEnvValues(request, server);
	if (access(fullPath.c_str(), X_OK) != 0)
		return -1; // ERROR PAGE access forbidden
	childPid = fork();
	if (childPid != 0)
		// wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI childPid is: " + std::to_string(childPid), true);
	if (childPid == -1)
		return -1;
	if (childPid == 0)
	{
		if (request.tempFileOpen)
		{
			// wslog.writeToLogFile(DEBUG, "CGIHandler::executeCGI child process using temp file", true);
			dup2(readFileFd, STDIN_FILENO);
			dup2(writeFileFd, STDOUT_FILENO);
		}
		else
		{
			dup2(writeCGIPipe[0], STDIN_FILENO);
			dup2(readCGIPipe[1], STDOUT_FILENO);
			close(writeCGIPipe[1]);
			close(readCGIPipe[0]);
		}
		execve(server.routes[request.location].cgiexecutable.c_str(), exceveArgs, envArray);
		// std::cout << "I WILL NOT GET HERE IF CHILD SCRIPT WAS SUCCESSFUL\n";
		_exit(1);
	}
	// childPid = childPid;
	if (request.tempFileOpen == false)
	{
		close(writeCGIPipe[0]);
		close(readCGIPipe[1]);
	}
	else
	{
		// Close the write and read end file descriptors from parents file descriptor table
		// the file descriptor table is copied but the underlying file descriptors are shared
		// so we need to close the read and write ends in the parent process
		close(readFileFd);
		close(writeFileFd);
	}
	return readCGIPipe[0];
}

// void registerCGI(Client& client, int epollFd, CGIHandler& cgi, int cgiOutFd)
// {
//     client.cgiPid = cgi.childPid;
//     client.cgiStdoutFd = cgiOutFd; //or register eventfd instead?
//     client.isCGI = true;

//     struct epoll_event ev;
//     ev.events = EPOLLIN;
//     ev.data.fd = cgiOutFd;
//     if (epoll_ctl(epollFd, EPOLL_CTL_ADD, cgiOutFd, &ev) < 0)
//         throw std::runtime_error("Failed to add CGI pipe to epoll");
// }

// HTTPResponse CGIHandler::executeCGI(Client& client)
// {
//     if (access(fullPath.c_str(), X_OK) != 0)
//         return  HTTPResponse(403, "Forbidden");
//     // int inPipe[2], outPipe[2];
//     pipe(writeCGIPipe);//inPipe
//     pipe(readCGIPipe);//outPipe
//     pid_t pid = fork();
//     if (pid == 0)
//     {
//         dup2(writeCGIPipe[0], STDIN_FILENO);
//         dup2(readCGIPipe[1], STDOUT_FILENO);
//         close(writeCGIPipe[1]);
//         close(readCGIPipe[0]);
//         execve(client.serverInfo.routes[client.request.location].cgiexecutable.c_str(), exceveArgs, envArray);
//         _exit(1);
//     }
//     close(writeCGIPipe[0]);
//     close(readCGIPipe[1]);
//     write(writeCGIPipe[1], client.request.body.c_str(), client.request.body.size());
//     close(readCGIPipe[1]);
//     char buffer[4096];
//     std::string output;
//     ssize_t n;
//     while ((n = read(readCGIPipe[0], buffer, sizeof(buffer))) > 0)
//         output.append(buffer, n);
//     close(readCGIPipe[0]);
//     int childPid;
//     childPid = waitpid(pid, NULL, WNOHANG);
//     std::string::size_type end = output.find("\r\n\r\n");
//     if (end == std::string::npos)
//     return HTTPResponse(500, "Invalid CGI output");
//     if (childPid == 0)
//     {
//         std::string headers = output.substr(0, end);
//         std::string body = output.substr(end + 4);
//         HTTPResponse res(200, "OK");
//         res.body = body;
//         std::istringstream header(headers);
//         std::string line;
//         while (std::getline(header, line))
//         {
//             if (line.back() == '\r')
//                 line.pop_back();
//             size_t colon = line.find(':');
//             if (colon != std::string::npos)
//                 res.headers[line.substr(0, colon)] = line.substr(colon + 2);
//         }
//         res.headers["Content-Length"] = std::to_string(res.body.size());
//         return res;
//     }
// }