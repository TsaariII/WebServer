#pragma once

#include <fstream>
#include <string>

#define RED 	"\033[31m"
#define GREEN	"\033[32m"
#define BLUE	"\033[34m"
#define PURPLE	"\033[35m"
#define CYAN   "\033[36m"
#define YELLOW  "\033[33m"
#define LIGHT_GREEN "\033[92m"
#define ORANGE "\033[38;5;214m"
#define RESET	"\033[0m"

enum LoggerStatus
{
	INFO,
	ERROR,
	DEBUG,
	WARNING
};

class Logger
{
    private:
        std::ofstream logstream;
    public:
        Logger();
        Logger(const Logger& src) = delete;
        Logger& operator=(const Logger& src) = delete;
        ~Logger();
        std::string get_current_time_string();
        void writeToLogFile(LoggerStatus status, const std::string message, bool toTerminal);
};

extern Logger wslog;