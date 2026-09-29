#pragma once

#include <cstdlib>
#include <format>
#include <iostream>
#include <utility>

namespace ze::Log {

template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    std::cout << "[ZEngine] " << std::format(fmt, std::forward<Args>(args)...) << '\n';
}

template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    std::cerr << "[ZEngine][warn] " << std::format(fmt, std::forward<Args>(args)...) << '\n';
}

template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    std::cerr << "[ZEngine][error] " << std::format(fmt, std::forward<Args>(args)...) << '\n';
}

template <class... Args>
[[noreturn]] void Fatal(std::format_string<Args...> fmt, Args&&... args)
{
    std::cerr << "[ZEngine][fatal] " << std::format(fmt, std::forward<Args>(args)...) << std::endl;
    std::abort();
}

} // namespace ze::Log
