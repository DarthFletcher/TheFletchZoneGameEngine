#pragma once

#include <string>
#include <unordered_map>

class Config {
public:
    static void Load(const std::string& fileName);
    static std::string GetString(const std::string& key, const std::string& defaultValue = "");
    static int GetInt(const std::string& key, int defaultValue = 0);
    static float GetFloat(const std::string& key, float defaultValue = 0.0f);
    static bool GetBool(const std::string& key, bool defaultValue = false);

private:
    static std::unordered_map<std::string, std::string> settings;

    static void ParseConfigFile(const std::string& fileName);
};
