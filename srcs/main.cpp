#include "Parser.hpp"
#include "Logger.hpp"
#include "HTTPRequest.hpp"
#include "HTTPResponse.hpp"
#include "RequestHandler.hpp"
#include "eventLoop.hpp"
#include <sys/socket.h>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>

Logger wslog;

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage program name + file" << std::endl;
        return 1;
    }
    try
    {
        Parser parser(argv[1]);
        parser.printServerConfigs();

        wslog.writeToLogFile(INFO, "Parsing config file successfully", true);
        //SocketsHandler sockethandler(parser.getServerConfigs());
        eventLoop(parser.getServerConfigs());
        //sockethandler.Run();
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        return (1);
    }
    catch (const std::out_of_range& e)
    {
        std::cerr << "Out of range error: " << e.what() << std::endl;
        return (1);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return (1);
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "Memory allocation error: " << e.what() << std::endl;
        return (1);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return (1);
    }
    return 0;
}