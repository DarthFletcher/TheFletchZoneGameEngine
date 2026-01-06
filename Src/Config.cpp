#include "Config.h"
#include <fstream>
#include <sstream>

std::unordered_map<std::string, std::string> Config::settings;

void Config::Load(const std::string& fileName) {
    ParseConfigFile(fileName);
}

std::string Config::GetString(const std::string& key, const std::string& defaultValue) {
    if (settings.find(key) != settings.end()) {
        return settings[key];
    }
    return defaultValue;
}

int Config::GetInt(const std::string& key, int defaultValue) {
    if (settings.find(key) != settings.end()) {
        return std::stoi(settings[key]);
    }
    return defaultValue;
}

float Config::GetFloat(const std::string& key, float defaultValue) {
    if (settings.find(key) != settings.end()) {
        return std::stof(settings[key]);
    }
    return defaultValue;
}

bool Config::GetBool(const std::string& key, bool defaultValue) {
    if (settings.find(key) != settings.end()) {
        return settings[key] == "true";
    }
    return defaultValue;
}

void Config::ParseConfigFile(const std::string& fileName) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + fileName);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue; // Skip comments and empty lines

        size_t delimiterPos = line.find('=');
        if (delimiterPos != std::string::npos) {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);
            settings[key] = value;
        }
    }
}
