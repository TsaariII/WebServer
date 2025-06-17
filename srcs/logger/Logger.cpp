#include "Logger.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

Logger::Logger()
{
     // Ensure the log directory exists
    std::filesystem::create_directories("logfiles");
    std::string name;
    name = "logfiles/" + get_current_time_string() + "logfile.log";
    logstream.open(name);
    if (!logstream.is_open())
    {
        throw std::runtime_error("Error opening log file:" + name);
    }
}

std::string Logger::get_current_time_string()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now_c), "%F_%T ");  // Format: YYYY-MM-DD_HH:MM:SS
    return oss.str();
}

void Logger::writeToLogFile(LoggerStatus status, const std::string message, bool toTerminal)
{
    std::ostringstream logmessage;

    switch(status)
    {
        case INFO:
        {
            logmessage << get_current_time_string() << GREEN << message << RESET << std::endl;
            break;
        }
        case ERROR:
        {
            logmessage << get_current_time_string() << RED << message << RESET << std::endl;
            break;
        }
        case DEBUG:
        {
            logmessage << get_current_time_string() << PURPLE << message << RESET << std::endl;
            break;
        }
        case WARNING:
        {
            logmessage << get_current_time_string() << YELLOW << message << RESET << std::endl;
            break;
        }
        default:
        {
            logmessage << get_current_time_string() << GREEN << message << RESET << std::endl;
        }
    }

    if (toTerminal)
    {
        std::cout << logmessage.str();
    }
    logstream << logmessage.str();
    // This flush is important.
    logstream.flush();
}


Logger::~Logger()
{
    logstream.close();
}