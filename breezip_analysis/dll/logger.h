#pragma once
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
    static void Log(const std::string& message);
    static void Init(const std::string& filename);

private:
    static std::string logFile;
    static std::mutex logMutex;
};
