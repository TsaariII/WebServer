#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <iostream>
#include <fstream>
#include <regex>
#include <filesystem>
#include <unistd.h>

// Erilaisia redirect status koodeja ja käyttötarkoituksia
/*
Koodi | Nimi | Käyttötarkoitus
301 | Moved Permanently | Pysyvä uudelleenohjaus
302 | Found (aiemmin "Moved Temporarily") | Tilapäinen uudelleenohjaus
307 | Temporary Redirect | Kuten 302, mutta metodit säilyvät
308 | Permanent Redirect | Kuten 301, mutta metodit säilyvät
*/

struct Redirect 
{
    int status_code;              // Status koodi redirectionille
    std::string target_url;       // Esim. url uuteen osoitteeseen esim /new
};

struct Route
{
    std::string path;
    std::string abspath;
    std::vector<std::string> accepted_methods;
    Redirect redirect;
    bool autoindex;
    std::string index_file;
    std::vector<std::string> cgi_extension;
    std::string upload_path;
    std::string cgipathpython;
    std::string cgipathphp;
};

struct ServerConfig 
{
    std::string host;
    int port;
    int fd;
    std::vector<std::string> server_names;
    std::map<int, std::string> error_pages;
    size_t client_max_body_size;
    std::map<std::string, Route> routes;
};

class Parser
{

    private:
        // Variables
        std::vector<ServerConfig> server_configs;
        std::string extension;
        // Parsing functions
        void parseListenDirective(const std::string& line, ServerConfig& server_config);
        void parseServerNameDirective(const std::string& line, ServerConfig& server_config);
        void parseClientMaxBodySizeDirective(const std::string& line, ServerConfig& server_config);
        void parseErrorPageDirective(const std::string& line, ServerConfig& server_config);
        void parseLocationDirective(std::ifstream& file, std::string& line, ServerConfig& server_config);
        void parseAbsPathDirective(const std::string& line, Route& route);
        void parseIndexDirective(const std::string& line, Route& route);
        void parseAutoIndexDirective(const std::string& line, Route& route);
        void parseAllowMethodsDirective(const std::string& line, Route& route);
        void parseReturnDirective(const std::string& line, Route& route);
        void parseUploadPathDirective(const std::string& line, Route& route);
        void parseCgiExtensionDirective(const std::string& line, Route& route);
        void parseCgiPathPython(const std::string& line, Route& route);
        void parseCgiPathPhp(const std::string& line, Route& route);
        // Validation functions
        bool validateServerDirective(const std::string& line);
        bool validateListenDirective(const std::string& line);
        bool validateServerNameDirective(const std::string& line);
        bool validateClientMaxBodySizeDirective(const std::string& line);
        bool validateErrorPageDirective(const std::string& line);
        bool validateLocationDirective(const std::string& line);
        bool validateAbsPathDirective(const std::string& line);
        bool validateIndexDirective(const std::string& line);
        bool validateAutoIndexDirective(const std::string& line);
        bool validateAllowMethodsDirective(const std::string& line);
        bool validateReturnDirective(const std::string& line);
        bool validateUploadPathDirective(const std::string& line);
        bool validateCgiExtensionDirective(const std::string& line);
        bool validateDirectives(const std::string& line);
        bool validateBrackets(const std::string& config_file);
        bool validateFile(const std::string& config_file);
        bool validateExtension(const std::string& filename, const std::string& expectedExt);
    public:
        Parser(const std::string& config_file);
        Parser(const Parser& src) = delete; // Disable copy constructor
        Parser& operator=(const Parser& src) = delete; // Disable copy assignment operator¨
        ~Parser();
        bool parseConfigFile(const std::string& config_file);
        std::vector<ServerConfig> getServerConfigs();
        void printServerConfigs() const;
        void printRoute(const Route& route) const;
        void printServerConfig(const ServerConfig& server_config) const;
        void trimLeadingAndTrailingSpaces(std::string& str);
        ServerConfig getServerConfig(std::string servername);
};
