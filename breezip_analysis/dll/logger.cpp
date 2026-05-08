#include "logger.h"
#include <chrono>
#include <iomanip>
#include <ctime>

std::string Logger::logFile = "C:\\temp\\breezip_log.txt";
std::mutex Logger::logMutex;

void Logger::Init(const std::string& filename) {
    logFile = filename;
}

void Logger::Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream ofs(logFile, std::ios::app);
    if (ofs.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::tm bt{};
        localtime_s(&bt, &in_time_t); // Sicherere Version für MSVC

        ofs << std::put_time(&bt, "%Y-%m-%d %X") << " - " << message << std::endl;
    }
}
