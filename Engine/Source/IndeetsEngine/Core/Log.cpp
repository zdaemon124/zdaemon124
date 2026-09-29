#include "IndeetsEngine/Core/Log.h"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>

namespace ie::Log {
namespace {

constexpr size_t kMaxEntries = 2000;

std::mutex g_Mutex;
std::deque<LogEntry> g_Entries;
uint64_t g_Version = 0;

} // namespace

void Write(LogLevel level, std::string message)
{
    std::lock_guard lock(g_Mutex);
    switch (level) {
    case LogLevel::Info: std::cout << "[IndeetsEngine] " << message << '\n'; break;
    case LogLevel::Warning: std::cerr << "[IndeetsEngine][warn] " << message << '\n'; break;
    case LogLevel::Error: std::cerr << "[IndeetsEngine][error] " << message << '\n'; break;
    }
    g_Entries.push_back({level, std::move(message)});
    if (g_Entries.size() > kMaxEntries)
        g_Entries.pop_front();
    ++g_Version;
}

void WriteFatal(std::string message)
{
    std::cerr << "[IndeetsEngine][fatal] " << message << std::endl;
    std::abort();
}

void ForEachEntry(const std::function<void(const LogEntry&)>& visitor)
{
    std::lock_guard lock(g_Mutex);
    for (const LogEntry& entry : g_Entries)
        visitor(entry);
}

void Clear()
{
    std::lock_guard lock(g_Mutex);
    g_Entries.clear();
    ++g_Version;
}

uint64_t Version()
{
    std::lock_guard lock(g_Mutex);
    return g_Version;
}

} // namespace ie::Log
