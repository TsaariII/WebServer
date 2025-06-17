#include "Parser.hpp"
#include "Logger.hpp"
#include <fstream>
#include <stack>
#include <filesystem>
#include <iostream>


Parser::Parser(const std::string& config_file)
{
    extension = ".conf";
    if (!parseConfigFile(config_file))
    {
        throw std::runtime_error("Failed to parse config file: " + config_file);
    }
}

void Parser::trimLeadingAndTrailingSpaces(std::string& str)
{
    // This function trims leading and trailing whitespaces
    // and also replaces multiple tabs or whitespaces with only single /tab or whitespace in between..
    // For example "     WEBSERVER     LOCATION   " becomes "WEBSERVER LOCATION"
    str = std::regex_replace(str, std::regex("^\\s+"), "");  // Remove leading spaces
    str = std::regex_replace(str, std::regex("\\s+$"), "");  // Remove trailing spaces
    str = std::regex_replace(str, std::regex("\\s+"), " ");  // Collapse spaces

}

void Parser::parseListenDirective(const std::string& line, ServerConfig& server_config)
{
    // Extract host and port from the line
    size_t pos = line.find("listen ") + 7; // Skip "listen "
    size_t end_pos = line.find(";", pos);
    std::string host_port = line.substr(pos, end_pos - pos);
    size_t colon_pos = host_port.find(":");
    if (colon_pos != std::string::npos)
    {
        server_config.host = host_port.substr(0, colon_pos);
        server_config.port = host_port.substr(colon_pos + 1);
    }
    else
    {
        // If no port is specified, assume default HTTP port 80
        // not sure if not specied host can you only specify port
        server_config.host = host_port;
        server_config.port = 80; // Default port
    }
}

void Parser::parseServerNameDirective(const std::string& line, ServerConfig& server_config)
{
    // Extract server names from the line
    size_t pos = line.find("server_name ") + 12; // Skip "server_name "
    size_t end_pos = line.find(";", pos);
    std::string server_names = line.substr(pos, end_pos - pos);
    size_t space_pos = 0;
    while ((space_pos = server_names.find(" ")) != std::string::npos)
    {
        server_config.server_names.push_back(server_names.substr(0, space_pos));
        server_names.erase(0, space_pos + 1);
    }
    server_config.server_names.push_back(server_names); // Add the last name
}

void Parser::parseClientMaxBodySizeDirective(const std::string& line, ServerConfig& server_config)
{
    // Extract client_max_body_size from the line
    size_t pos = line.find("client_max_body_size ") + 21; // Skip "client_max_body_size "
    size_t end_pos = line.find(";", pos);
    std::string size_str = line.substr(pos, end_pos - pos);
    if (size_str.find("K") != std::string::npos)
    {
        size_str.erase(size_str.find("K"));
        server_config.client_max_body_size = std::stoul(size_str) * 1024; // Convert to bytes
    }
    else if (size_str.find("M") != std::string::npos)
    {
        size_str.erase(size_str.find("M"));
        server_config.client_max_body_size = std::stoul(size_str) * 1024 * 1024; // Convert to bytes
    }
    else
    {
        server_config.client_max_body_size = std::stoul(size_str); // Assume bytes
    }
}

void Parser::parseClientMaxBodySizeDirective(const std::string& line, Route& route)
{
    // Extract client_max_body_size from the line
    size_t pos = line.find("client_max_body_size ") + 21; // Skip "client_max_body_size "
    size_t end_pos = line.find(";", pos);
    std::string size_str = line.substr(pos, end_pos - pos);
    if (size_str.find("K") != std::string::npos)
    {
        size_str.erase(size_str.find("K"));
        route.client_max_body_size = std::stoul(size_str) * 1024; // Convert to bytes
    }
    else if (size_str.find("M") != std::string::npos)
    {
        size_str.erase(size_str.find("M"));
        route.client_max_body_size = std::stoul(size_str) * 1024 * 1024; // Convert to bytes
    }
    else
    {
        route.client_max_body_size = std::stoul(size_str); // Assume bytes
    }
}
void Parser::parseErrorPageDirective(const std::string& line, ServerConfig& server_config)
{
    // Extract error pages from the line
    size_t pos = line.find("error_page ") + 11; // Skip "error_page "
    size_t end_pos = line.find(";", pos); // need to check npos if ; not found
    size_t space_pos = line.find(" ", pos);
    int error_code = std::stoi(line.substr(pos, space_pos - pos));
    std::string str = line.substr(space_pos, end_pos - space_pos);
    trimLeadingAndTrailingSpaces(str);
    std::string error_page = str;
    server_config.error_pages[error_code] = error_page;
}

void Parser::parseAbsPathDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("abspath ") + 8; // Skip "abspath "
    size_t end_pos = line.find(";");
    route.abspath = line.substr(pos, end_pos - pos);
}

void Parser::parseIndexDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("index ") + 6; // Skip "index "
    size_t end_pos = line.find(";");
    std::string str;
    str = line.substr(pos, end_pos - pos);
    trimLeadingAndTrailingSpaces(str);
    route.index_file = str;
}

void Parser::parseAutoIndexDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("autoindex ") + 10; //skip "autoindex "
    size_t end_pos = line.find(";");
    std::string onoff = line.substr(pos, end_pos - pos);
    if (onoff.compare("off") == 0)
        route.autoindex = false;
    else if (onoff.compare("on") == 0)
        route.autoindex = true;
}

void Parser::parseCgiMethodsDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("cgi_methods ") + 12; // Skip "cgi_methods "
    size_t end_pos = line.find(";");
    std::string methods = line.substr(pos, end_pos - pos);
    size_t space_pos = 0;
    while ((space_pos = methods.find(" ")) != std::string::npos)
    {
        route.cgi_methods.push_back(methods.substr(0, space_pos));
        methods.erase(0, space_pos + 1);
    }
    route.cgi_methods.push_back(methods); // Add the last method
}

void Parser::parseAllowMethodsDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("allow_methods ") + 14; // Skip "allow_methods "
    size_t end_pos = line.find(";");
    std::string methods = line.substr(pos, end_pos - pos);
    size_t space_pos = 0;
    while ((space_pos = methods.find(" ")) != std::string::npos)
    {
        route.accepted_methods.push_back(methods.substr(0, space_pos));
        methods.erase(0, space_pos + 1);
    }
    route.accepted_methods.push_back(methods); // Add the last method
}

void Parser::parseReturnDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("return ") + 7; // Skip "return "
    size_t space_pos = line.find(" ", pos);
    route.redirect.status_code = std::stoi(line.substr(pos, space_pos - pos));
    route.redirect.target_url = line.substr(space_pos + 1, line.find(";") - space_pos - 1);
}

void Parser::parseUploadPathDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("upload_path ") + 12; // Skip "upload_path "
    size_t end_pos = line.find(";");
    route.upload_path = line.substr(pos, end_pos - pos);
}

void Parser::parseCgiExtensionDirective(const std::string& line, Route& route)
{
    size_t pos = line.find("cgi_extension ") + 14; // Skip "cgi_extension "
    size_t end_pos = line.find(";");
    std::string cgi_extensions = line.substr(pos, end_pos - pos);
    size_t space_pos = 0;
    while ((space_pos = cgi_extensions.find(" ")) != std::string::npos)
    {
        route.cgi_extension.push_back(cgi_extensions.substr(0, space_pos));
        cgi_extensions.erase(0, space_pos + 1);
    }
    route.cgi_extension.push_back(cgi_extensions); // Add the last cgi_extension
}

void Parser::parseCgiExecutable(const std::string&line, Route& route)
{
    size_t pos = line.find("cgiexecutable ") + 14; // Skip "cgiexecutable "
    size_t end_pos = line.find(";");
    route.cgiexecutable = line.substr(pos, end_pos - pos);
}

bool Parser::parseLocationDirective(std::ifstream& file, std::string& line, ServerConfig& server_config, bool serverMaxBodySizeSet)
{
    std::unordered_set<std::string> foundkeys;
    Route route{}; // alustaa default arvoihin struktin siksi kaarisulkeet
    int maxBodySizeSet = false;
    size_t pos = line.find("location ") + 9; // Skip "location /"
    size_t end_pos = line.find("{");
    line = line.substr(pos, end_pos - pos);
    trimLeadingAndTrailingSpaces(line);
    route.path = line;
    while (getline(file, line) && line.find("}") == std::string::npos)
    {
        trimLeadingAndTrailingSpaces(line);
        if (line.find("abspath ") != std::string::npos)
        {
            parseAbsPathDirective(line, route);
            auto result = foundkeys.insert("abspath");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple abspaths", true);
                return false;
            }
        }
        else if (line.find("index ") != std::string::npos && line.find("autoindex") == std::string::npos)
        {
            parseIndexDirective(line, route);
            auto result = foundkeys.insert("index");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple index files", true);
                return false;
            }
        }
        else if (line.find("autoindex ") != std::string::npos)
        {
            auto result = foundkeys.insert("autoindex");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple autoindex", true);
                return false;
            }
            parseAutoIndexDirective(line, route);
        }
        else if (line.find("cgi_methods ") != std::string::npos)
        {
            auto result = foundkeys.insert("cgi_methods");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple cgimethods", true);
                return false;
            }
            parseCgiMethodsDirective(line, route);
        }
        else if (line.find("allow_methods ") != std::string::npos)
        {
            auto result = foundkeys.insert("allow_methods");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple allow_methods", true);
                return false;
            }
            parseAllowMethodsDirective(line, route);
        }
        else if (line.find("return ") != std::string::npos)
        {
            auto result = foundkeys.insert("return ");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple return", true);
                return false;
            }
            parseReturnDirective(line, route);
        }
        else if (line.find("upload_path ") != std::string::npos)
        {
            auto result = foundkeys.insert("upload_path");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple upload_path", true);
                return false;
            }
           parseUploadPathDirective(line, route);
        }
        else if (line.find("cgi_extension ") != std::string::npos)
        {
            auto result = foundkeys.insert("cgi_extension");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple cgi_extension", true);
                return false;
            }
            parseCgiExtensionDirective(line, route);
        }
        else if (line.find("cgiexecutable ") != std::string::npos)
        {
            auto result = foundkeys.insert("cgiexecutable");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple cgiexecutable", true);
                return false;
            }
            parseCgiExecutable(line, route);
        }
        else if (line.find("client_max_body_size ") != std::string::npos)
        {
            auto result = foundkeys.insert("client_max_body_size");
            if (result.second == false)
            {
                wslog.writeToLogFile(ERROR, "multiple client_max_body_size", true);
                return false;
            }
            parseClientMaxBodySizeDirective(line, route);
            maxBodySizeSet = true;
        }
    }
    if (maxBodySizeSet == false)
    {
        if (serverMaxBodySizeSet == true)
            route.client_max_body_size = server_config.client_max_body_size;
        else
            route.client_max_body_size = DEFAULT_MAX_BODY_SIZE;
    }
    server_config.routes[route.path] = route;
    return true;
}

bool Parser::validateServerDirective(const std::string& line)
{
    std::regex server_regex(R"(^\s*server\s*\{$)");
    if (std::regex_match(line, server_regex))
        return true;
    else
        return false;
}

bool Parser::validateListenDirective(const std::string& line)
{
    std::regex listen_regex(R"(^\s*listen \d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d{1,5};$)");
    if (std::regex_match(line, listen_regex))
        return true;
    else
        return false;
}


bool Parser::validateServerNameDirective(const std::string& line)
{
/*

    ### Explanation of the Regex

    1. **`^`**:
    - Matches the **start of the line**.

    2. **`server_name`**:
    - Matches the literal text `server_name`.

    3. **`\s+`**:
    - Matches **one or more whitespace characters** (spaces, tabs, etc.).

    4. **`([\w.-]+\s*)+`**:
    - Matches one or more domain names:
        - `[\w.-]+`: Matches a domain name, which can include:
        - `\w`: Alphanumeric characters (letters, digits, and underscores).
        - `.`: Dots (e.g., `example.com`).
        - `-`: Hyphens (e.g., `www-example.com`).
        - `\s*`: Matches optional whitespace after each domain name.
        - `(...)`: Groups the domain name and optional whitespace together.
        - `+`: Ensures that one or more domain names are matched.

    5. **`;`**:
    - Matches the literal semicolon (`;`) at the end of the directive.

    6. **`$`**:
    - Matches the **end of the line**.

    ---
*/
    std::regex server_name_regex(R"(^\s*server_name\s+([\w.-]+\s*)+;$)");
    if (std::regex_match(line, server_name_regex))
        return true;
    else
        return false;
}

bool Parser::validateClientMaxBodySizeDirective(const std::string& line)
{
    std::regex client_max_body_size_regex(R"(^\s*client_max_body_size\s+\d+[KM]?;$)");
    if (std::regex_match(line, client_max_body_size_regex))
        return true;
    else
        return false;
}

bool Parser::validateErrorPageDirective(const std::string& line)
{
    /// need to check all the possible erro codes now any 3 digits is ok
    std::regex error_page_regex(R"(^\s*error_page\s+\d{3}\s/[^\s]+;$)");
    if (std::regex_match(line, error_page_regex))
        return true;
    else
        return false;
}

bool Parser::validateLocationDirective(const std::string& line)
{
    std::regex location_regex(R"(^\s*location\s+\/[^\s]*\s*\{$)");
    if (std::regex_match(line, location_regex))
        return true;
    else
        return false;
}

bool Parser::validateAbsPathDirective(const std::string& line)
{
    /*
    ### Explanation of the Regex:
        1. **`^abspath`**: Ensures the line starts with the word "abspath".
        2. **`\s+`**: Matches one or more whitespace characters after "abspath".
        3. **`(/[^\s;]+)+`**: Matches one or more paths:
        - `/`: Each path starts with a forward slash.
        - `[^\s;]+`: Matches one or more characters that are not whitespace or a semicolon.
        4. **`;`**: Ensures the line ends with a semicolon.
        5. **`$`**: Ensures the match goes to the end of the line.
    */
    std::regex abspath_regex(R"(^\s*abspath\s+(/[^\s;]+)+;$)");
    if (std::regex_match(line, abspath_regex))
        return true;
    else
        return false;
}

bool Parser::validateIndexDirective(const std::string& line)
{
    std::regex index_regex(R"(^\s*index\s+[\w.-]+\.[a-zA-Z0-9._-]+;$)");
    if (std::regex_match(line, index_regex))
        return true;
    else
        return false;
}

bool Parser::validateAutoIndexDirective(const std::string& line)
{
    std::regex autoindex_regex(R"(^\s*autoindex\s+(on|off);$)");
    if (std::regex_match(line, autoindex_regex))
        return true;
    else
        return false;
}

bool Parser::validateCgiMethodsDirective(const std::string& line)
{
    std::regex cgi_methods_regex(R"(^\s*cgi_methods\s+(GET|POST|DELETE)(\s+(GET|POST|DELETE))*;$)");
    if (std::regex_match(line, cgi_methods_regex))
        return true;
    else
        return false;
}

bool Parser::validateAllowMethodsDirective(const std::string& line)
{
    std::regex allow_methods_regex(R"(^\s*allow_methods\s+(GET|POST|DELETE)(\s+(GET|POST|DELETE))*;$)");
    if (std::regex_match(line, allow_methods_regex))
        return true;
    else
        return false;
}

bool Parser::validateReturnDirective(const std::string& line)
{
    std::regex return_regex(R"(^\s*return\s+(301|302|307|308) .+;$)");
    if (std::regex_match(line, return_regex))
        return true;
    else
        return false;
}

bool Parser::validateUploadPathDirective(const std::string& line)
{
    std::regex upload_path_regex(R"(^\s*upload_path\s+(/[^\s;]+)+;$)");
    if (std::regex_match(line, upload_path_regex))
        return true;
    else
        return false;
}

bool Parser::validateCgiExtensionDirective(const std::string& line)
{
    std::regex cgi_extension_regex(R"(^\s*cgi_extension\s+\.[a-zA-Z0-9_.-]+;$)");
    if (std::regex_match(line, cgi_extension_regex))
        return true;
    else
        return false;
}

bool Parser::validateBrackets(const std::string& config_file)
{
    std::ifstream file(config_file);
    if (!file.is_open())
    {
        wslog.writeToLogFile(ERROR, "Error opening config file: " + config_file, DEBUG_LOGS);
        return false;
    }
    std::stack<char> bracket_stack;
    std::string line;
    while (getline(file, line))
    {
        if (line.empty() || line.at(0) == '#')
            continue;
        for (char ch: line)
        {
            if (ch == '{')
                bracket_stack.push(ch);
            else if (ch == '}')
            {
                if (bracket_stack.empty())
                {
                    wslog.writeToLogFile(ERROR, "Unmatched closing bracket '}' found.", DEBUG_LOGS);
                    file.close();
                    return false;
                }
                bracket_stack.pop();
            }
        }
    }
    if (!bracket_stack.empty())
    {
        wslog.writeToLogFile(ERROR, "Unmatched opening bracket '{' found.", DEBUG_LOGS);
        file.close();
        return false;
    }
    file.close();
    return true;
}

bool Parser::validateDirectives(const std::string& line)
{
    if (line.find("cgiexecutable ") != std::string::npos)
        return true;
    // Check if the line contains any of the directives
    if (validateServerDirective(line) || validateListenDirective(line) || validateServerNameDirective(line) ||
        validateClientMaxBodySizeDirective(line) || validateErrorPageDirective(line) || validateLocationDirective(line) ||
        validateAbsPathDirective(line) || validateIndexDirective(line) || validateAutoIndexDirective(line) ||
        validateAllowMethodsDirective(line) || validateCgiMethodsDirective(line) || validateReturnDirective(line) || validateUploadPathDirective(line) ||
        validateCgiExtensionDirective(line))
    {
        return true;
    }
    return false;
}

bool Parser::validateExtension(const std::string& filename, const std::string& expectedExt)
{
    return std::filesystem::path(filename).extension() == expectedExt;
}

bool Parser::validateFile(const std::string& config_file)
{
    if (!validateExtension(config_file, extension))
    {
        wslog.writeToLogFile(ERROR, "Error invalid extension in file: " + config_file, DEBUG_LOGS);
        wslog.writeToLogFile(INFO, "Check configurationfiles/configurationfile_rules.conf for manual how to write the configuration file", DEBUG_LOGS);
        return false;
    }

    if (!validateBrackets(config_file))
        return false;
    std::ifstream file(config_file);
    if (!file.is_open())
    {
        wslog.writeToLogFile(ERROR, "Error opening config file: " + config_file, DEBUG_LOGS);
        return false;
    }
    std::string line;
    std::regex spaces_and_tabs_regex(R"(^[ \t]*$)");
    while (getline(file, line))
    {
        trimLeadingAndTrailingSpaces(line);
        // line is a comment
        if (line.empty() || line.at(0) == '#')
            continue;
        else if (std::regex_match(line, spaces_and_tabs_regex))
            continue;
        else if (line.find_first_not_of(" \t") == line.find('}') && line.find('}') != std::string::npos && line.find_first_not_of(" \t}") == std::string::npos)
            continue;
        else if (validateDirectives(line))
            continue;
        else
        {
            file.close();
            wslog.writeToLogFile(ERROR, "Error invalid line", DEBUG_LOGS);
            wslog.writeToLogFile(ERROR, "Error failed to parse line: " + line, DEBUG_LOGS);
            wslog.writeToLogFile(ERROR, "Error file validation failed ", DEBUG_LOGS);
            wslog.writeToLogFile(INFO, "Check configurationfiles/configurationfile_rules.conf for manual how to write the configuration file", DEBUG_LOGS);
            return false;
        }
    }
    file.close();
    return true;
}

bool Parser::parseConfigFile(const std::string& config_file)
{
    std::unordered_set<std::string> foundkeys;
     if (!validateFile(config_file))
        return false;
    std::ifstream file(config_file);
    if (!file.is_open())
    {
        wslog.writeToLogFile(ERROR, "Error opening config file: " + config_file, DEBUG_LOGS);
        return false;
    }
    std::string line;
    while (getline(file, line))
    {
        trimLeadingAndTrailingSpaces(line);
        if (line.empty() || line.at(0) == '#')
            continue;
        if (line.find("server {") != std::string::npos)
        {
            bool maxBodySizeSet = false;
            ServerConfig server_config{};
            while (getline(file, line) && line.find("}") == std::string::npos)
            {
                trimLeadingAndTrailingSpaces(line);
                if (line.find("listen ") != std::string::npos)
                {
                    parseListenDirective(line, server_config);
                }
                else if (line.find("server_name ") != std::string::npos)
                {
                    parseServerNameDirective(line, server_config);
                }
                else if (line.find("error_page ") != std::string::npos)
                {
                    parseErrorPageDirective(line, server_config);
                }
                else if (line.find("location /") != std::string::npos)
                {
                   if (parseLocationDirective(file, line, server_config, maxBodySizeSet) == false)
                       return false;
                }
                else if (line.find("client_max_body_size ") != std::string::npos)
                {
                    parseClientMaxBodySizeDirective(line, server_config);
                    maxBodySizeSet = true;
                }
            }
            if (maxBodySizeSet == false)
                server_config.client_max_body_size = DEFAULT_MAX_BODY_SIZE;
            server_configs.push_back(server_config);
        }
    }
    file.close();
    return true;
}

std::vector<ServerConfig> Parser::getServerConfigs()
{
    return server_configs;
}

ServerConfig Parser::getServerConfig(std::string servername)
{
    for (auto i: server_configs)
    {
        if (i.host == servername)
            return i;
    }
    // Throw an exception if the config is not found
    throw std::runtime_error("Server configuration not found for: " + servername);
}

void Parser::printRoute(const Route& route) const
{
    std::cout << "\033[1;34mPrinting Route struct\033[0m" << std::endl;
    std::cout << "Route Path: " << route.path << std::endl;
    std::cout << "Abspath: " << route.abspath << std::endl;
    std::cout << "Accepted Methods: ";
    for (const auto& method : route.accepted_methods)
    {
        std::cout << method << " ";
    }
    std::cout << std::endl;
    std::cout << "Redirect status code: " << route.redirect.status_code << std::endl;
    std::cout << "Redirect To: " << route.redirect.target_url << std::endl;
    std::cout << "Autoindex: " << (route.autoindex ? "on" : "off") << std::endl;
    std::cout << "Index File: " << route.index_file << std::endl;
    for (const auto& cgi_extension : route.cgi_extension)
    {
        std::cout << cgi_extension << " ";
    }
    std::cout << "Upload Path: " << route.upload_path << std::endl;
    std::cout << "cgiexecutable : " << route.cgiexecutable << std::endl;

}

void Parser::printServerConfig(const ServerConfig& server_config) const
{
    std::cout << "\033[1;34mPrinting ServerConfig struct\033[0m" << std::endl;
    std::cout << "Host: " << server_config.host << std::endl;
    std::cout << "Port: " << server_config.port << std::endl;
    std::cout << "Server Names: ";
    for (const auto& name : server_config.server_names)
    {
        std::cout << name << " ";
    }
    std::cout << std::endl;
    std::cout << "Client Max Body Size: " << server_config.client_max_body_size << std::endl;
    for (const auto& route : server_config.routes)
    {
        printRoute(route.second);
    }
}

void Parser::printServerConfigs() const
{
    for (const auto& server_config : server_configs)
    {
        printServerConfig(server_config);
    }
}

Parser::~Parser()
{
    // Destructor implementation (if needed)
}
