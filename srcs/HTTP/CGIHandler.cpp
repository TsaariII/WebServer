#include "CGIHandler.hpp"

CGIHandler::CGIHandler() 
{
	writeCGIPipe[1] = -1;
	writeCGIPipe[0] = -1;
	readCGIPipe[1] = -1;
	readCGIPipe[0] = -1;
}

int CGIHandler::getWritePipe() { return writeCGIPipe[1]; }

int CGIHandler::getReadPipe() { return readCGIPipe[0]; }

int CGIHandler::getChildPid() { return childPid; }

void CGIHandler::setEnvValues(HTTPRequest& request, ServerConfig server)
{
	std::string server_name = server.server_names.empty() ? "localhost"
			: server.server_names.at(0);
	char absPath[PATH_MAX];
	std::string localPath = joinPaths(server.routes.at(request.location).abspath, request.file);
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
	std::string conLen = request.headers.count("Content-Length") > 0 ? request.headers.at("Content-Length") : "0";
	envVariables.push_back("CONTENT_LENGTH=" + conLen);
	for (size_t i = 0; i < envVariables.size(); i++)
		envArray[i] = (char *)envVariables.at(i).c_str();
	envArray[envVariables.size()] = NULL;
	exceveArgs[0] = (char *)server.routes.at(request.location).cgiexecutable.c_str();
	exceveArgs[1] = absPath;
	exceveArgs[2] = NULL;
}

HTTPResponse CGIHandler::generateCGIResponse(std::map<int, std::string> error_pages)
{
	std::string::size_type end = output.find("\r\n\r\n");
	if (end == std::string::npos)
		return HTTPResponse(500, "Invalid CGI output", error_pages);
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
	wslog.writeToLogFile(INFO, "CGI successful", DEBUG_LOGS);
	return res;
}

void CGIHandler::collectCGIOutput(int childReadPipeFd)
{
    char buffer[65536];

    int n = read(childReadPipeFd, buffer, sizeof(buffer));
    if (n > 0)
        output.append(buffer, n);
}

void CGIHandler::writeBodyToChild(HTTPRequest& request)
{
    int written = write(writeCGIPipe[1], request.body.c_str(), request.body.size());
    if (written > 0) 
        request.body = request.body.substr(written);
    if (request.body.empty() == true)
	{
        close(writeCGIPipe[1]);
		writeCGIPipe[1] = -1;
	}
}
