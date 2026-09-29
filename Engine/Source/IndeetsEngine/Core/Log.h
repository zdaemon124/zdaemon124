#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace ie {

enum class LogLevel { Info, Warning, Error };

struct LogEntry {
    LogLevel level;
    std::string message;
};

namespace Log {

// Prints the message and stores it for the editor console.
void Write(LogLevel level, std::string message);
[[noreturn]] void WriteFatal(std::string message);

// Visits stored entries under a lock (oldest first).
void ForEachEntry(const std::function<void(const LogEntry&)>& visitor);
void Clear();
// Increments whenever entries change; lets UIs skip work.
uint64_t Version();

template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    Write(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    Write(LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    Write(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
[[noreturn]] void Fatal(std::format_string<Args...> fmt, Args&&... args)
{
    WriteFatal(std::format(fmt, std::forward<Args>(args)...));
}

} // namespace Log
} // namespace ie
