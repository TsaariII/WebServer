#pragma once

#include "HTTPRequest.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <atomic>
#include <csignal>

std::string joinPaths(std::filesystem::path path1, std::filesystem::path path2);
bool validateHeader(HTTPRequest req);
void handleSignals(int signum);
std::vector<std::string> split(const std::string& s, const std::string& s2);
std::string extractFilename(const std::string& path, int method);
std::string extractContent(const std::string& part);
std::string getFileExtension(const std::string& path);
extern std::atomic<int> signum;
