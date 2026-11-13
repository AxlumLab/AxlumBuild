/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "core/Process.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#undef FindExecutable
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace axlum
{
    namespace
    {
#ifdef _WIN32
        std::wstring wide(const std::string& s)
        {
            if (s.empty())
                return {};
            int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
            if (!n)
                throw std::runtime_error("invalid UTF-8 process argument");
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, w.data(), n);
            w.pop_back();
            return w;
        }

        std::wstring quoteWindows(const std::wstring& value)
        {
            std::wstring result = L"\"";
            unsigned slashCount = 0;
            for (wchar_t c : value)
            {
                if (c == L'\\')
                {
                    ++slashCount;
                    continue;
                }
                if (c == L'"')
                {
                    result.append(slashCount * 2 + 1, L'\\');
                    result += c;
                }
                else
                {
                    result.append(slashCount, L'\\');
                    result += c;
                }
                slashCount = 0;
            }
            result.append(slashCount * 2, L'\\');
            result += L'"';
            return result;
        }
#else
        std::string quotePosix(const std::string& value)
        {
            std::string result = "'";
            for (char c : value)
                result += c == '\'' ? "'\\''" : std::string(1, c);
            return result + "'";
        }
#endif
    } // namespace

    std::string Process::quoteShellArg(const std::string& arg)
    {
#ifdef _WIN32
        std::wstring wideArg = quoteWindows(wide(arg));
        int n = WideCharToMultiByte(CP_UTF8, 0, wideArg.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideArg.c_str(), -1, s.data(), n, nullptr, nullptr);
        s.pop_back();
        return s;
#else
        return quotePosix(arg);
#endif
    }

    std::string Process::quoteRspArg(const std::string& arg, bool gnu)
    {
        if (gnu)
        {
            std::string result = "\"";
            for (const auto c : arg)
            {
                if (c == '\\' || c == '"')
                    result += '\\';
                result += c;
            }
            return result + '"';
        }
        // GCC/Clang response file syntax and MSVC's command-line quoting agree for
        // whitespace, quotes and trailing backslashes in ordinary file/flag arguments.
        std::string s = "\"";
        std::size_t backslashes = 0;
        for (char c : arg)
        {
            if (c == '\\')
            {
                ++backslashes;
                continue;
            }
            if (c == '"')
            {
                s.append(backslashes * 2 + 1, '\\');
                s += c;
            }
            else
            {
                s.append(backslashes, '\\');
                s += c;
            }
            backslashes = 0;
        }
        s.append(backslashes * 2, '\\');
        return s + '"';
    }

    std::string NinjaSyntax::escapeNinjaValue(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            if (c == '$')
                out += "$$";
            else if (c == '\n' || c == '\r')
                throw std::runtime_error("newline in Ninja command/value");
            else
                out += c;
        }
        return out;
    }

    std::string NinjaSyntax::escapeNinjaPath(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
            case '$':
                out += "$$";
                break;
            case ' ':
                out += "$ ";
                break;
            case ':':
                out += "$:";
                break;
            case '\n':
            case '\r':
                throw std::runtime_error("newline in Ninja file path");
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    std::string FileSystem::readAll(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("cannot open '" + path.string() + "'");
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    void FileSystem::writeMsvcDepfile(const fs::path& jsonFile, const fs::path& depfile, const fs::path& object)
    {
        const auto data = nlohmann::json::parse(FileSystem::readAll(jsonFile)).at("Data");
        auto escape = [](const fs::path& path)
        {
            std::string result;
            for (const char c : path.generic_string())
            {
                if (c == '$')
                    result += "$$";
                else
                {
                    if (c == ' ' || c == '#' || c == ':')
                        result += '\\';
                    result += c;
                }
            }
            return result;
        };
        std::string text = escape(object) + ": " + escape(fs::path(data.at("Source").get<std::string>()));
        for (const auto& include : data.at("Includes"))
            text += " " + escape(fs::path(include.get<std::string>()));
        FileSystem::writeIfChanged(depfile, text + "\n");
    }

    void FileSystem::writeEnvironment(const fs::path& file, const std::map<std::string, std::string>& environment)
    {
        FileSystem::writeIfChanged(file, nlohmann::json(environment).dump(2) + "\n");
    }

    std::map<std::string, std::string> FileSystem::readEnvironment(const fs::path& file)
    {
        return nlohmann::json::parse(FileSystem::readAll(file)).get<std::map<std::string, std::string>>();
    }

    void FileSystem::writeIfChanged(const fs::path& path, const std::string& contents)
    {
        if (fs::exists(path) && FileSystem::readAll(path) == contents)
            return;
        if (!path.parent_path().empty())
            fs::create_directories(path.parent_path());
        const fs::path tmp = fs::path(path.string() + ".tmp");
        {
            std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
            if (!file)
                throw std::runtime_error("cannot write '" + tmp.string() + "'");
            file << contents;
            if (!file)
                throw std::runtime_error("write failed for '" + tmp.string() + "'");
        }
        // Windows does not allow rename over an existing destination.
        std::error_code ec;
        fs::rename(tmp, path, ec);
        if (ec)
        {
            fs::remove(path, ec);
            fs::rename(tmp, path);
        }
    }

    std::string FileSystem::shortHash(std::string_view input)
    {
        std::uint64_t h = 14695981039346656037ull;
        for (unsigned char c : input)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        std::ostringstream o;
        o << std::hex << std::setfill('0') << std::setw(16) << h;
        return o.str().substr(0, 12);
    }

    bool FileSystem::isWithin(const fs::path& child, const fs::path& parent)
    {
        const auto a = fs::absolute(child).lexically_normal();
        const auto b = fs::absolute(parent).lexically_normal();
        auto ia = a.begin();
        for (auto ib = b.begin(); ib != b.end(); ++ib, ++ia)
        {
            if (ia == a.end())
                return false;
#ifdef _WIN32
            auto x = ia->wstring(), y = ib->wstring();
            std::transform(x.begin(), x.end(), x.begin(), ::towlower);
            std::transform(y.begin(), y.end(), y.begin(), ::towlower);
            if (x != y)
                return false;
#else
            if (*ia != *ib)
                return false;
#endif
        }
        return true;
    }

    std::string Process::findExecutable(const std::string& executable)
    {
        if (executable.empty())
            return {};
        fs::path exe(executable);
        if (exe.has_parent_path())
        {
            return fs::exists(exe) && fs::is_regular_file(exe) ? fs::absolute(exe).string() : std::string{};
        }
        const char* pathEnv = std::getenv("PATH");
        if (!pathEnv)
            return {};
#ifdef _WIN32
        constexpr char delim = ';';
        const std::vector<std::string> extensions = {"", ".exe", ".cmd", ".bat"};
#else
        constexpr char delim = ':';
        const std::vector<std::string> extensions = {""};
#endif
        std::stringstream ss(pathEnv);
        std::string part;
        while (std::getline(ss, part, delim))
        {
            for (const auto& ext : extensions)
            {
                fs::path candidate = (part.empty() ? fs::current_path() : fs::path(part)) / (executable + ext);
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec))
                {
#ifndef _WIN32
                    if (access(candidate.c_str(), X_OK) != 0)
                        continue;
#endif
                    return fs::absolute(candidate).string();
                }
            }
        }
        return {};
    }

    int Process::run(
        const std::vector<std::string>& args,
        const fs::path& workingDir,
        const std::map<std::string, std::string>& environment,
        const std::function<void(std::string_view)>& output
    )
    {
        if (args.empty() || args[0].empty())
            throw std::runtime_error("empty process command");
#ifdef _WIN32
        std::wstring command;
        for (const auto& arg : args)
        {
            if (!command.empty())
                command += L' ';
            command += quoteWindows(wide(arg));
        }
        std::wstring cwd = workingDir.empty() ? L"" : workingDir.wstring();
        std::vector<wchar_t> envBlock;
        if (!environment.empty())
        {
            auto less = [](const std::wstring& a, const std::wstring& b)
            {
                return _wcsicmp(a.c_str(), b.c_str()) < 0;
            };
            std::map<std::wstring, std::wstring, decltype(less)> values(less);
            LPWCH base = GetEnvironmentStringsW();
            if (!base)
                throw std::runtime_error("GetEnvironmentStringsW failed");
            for (LPCWCH p = base; *p; p += wcslen(p) + 1)
            {
                std::wstring kv(p);
                auto pos = kv.find(L'=', kv.size() > 0 && kv[0] == L'=' ? 1 : 0);
                if (pos != std::wstring::npos)
                    values[kv.substr(0, pos)] = kv.substr(pos + 1);
            }
            FreeEnvironmentStringsW(base);
            for (const auto& [k, v] : environment)
                values[wide(k)] = wide(v);
            for (const auto& [k, v] : values)
            {
                const std::wstring kv = k + L'=' + v;
                envBlock.insert(envBlock.end(), kv.begin(), kv.end());
                envBlock.push_back(L'\0');
            }
            envBlock.push_back(L'\0');
        }
        HANDLE readPipe = nullptr, writePipe = nullptr;
        if (output)
        {
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            if (!CreatePipe(&readPipe, &writePipe, &security, 0))
                throw std::runtime_error("CreatePipe failed");
            if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
            {
                CloseHandle(readPipe);
                CloseHandle(writePipe);
                throw std::runtime_error("SetHandleInformation failed");
            }
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        if (output)
        {
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = startup.hStdError = writePipe;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        }
        PROCESS_INFORMATION proc{};
        if (!CreateProcessW(
                nullptr,
                command.data(),
                nullptr,
                nullptr,
                TRUE,
                environment.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT,
                environment.empty() ? nullptr : envBlock.data(),
                cwd.empty() ? nullptr : cwd.c_str(),
                &startup,
                &proc
            ))
        {
            if (readPipe)
                CloseHandle(readPipe);
            if (writePipe)
                CloseHandle(writePipe);
            throw std::runtime_error("CreateProcessW failed for '" + args[0] + "' (Win32=" + std::to_string(GetLastError()) + ")");
        }
        std::exception_ptr callbackError;
        if (output)
        {
            CloseHandle(writePipe);
            char buffer[8192];
            DWORD count = 0;
            while (ReadFile(readPipe, buffer, sizeof(buffer), &count, nullptr) && count)
            {
                try
                {
                    output(std::string_view(buffer, count));
                }
                catch (...)
                {
                    callbackError = std::current_exception();
                }
            }
            CloseHandle(readPipe);
        }
        WaitForSingleObject(proc.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(proc.hProcess, &exitCode);
        CloseHandle(proc.hThread);
        CloseHandle(proc.hProcess);
        if (callbackError)
            std::rethrow_exception(callbackError);
        return static_cast<int>(exitCode);
#else
        int descriptors[2] = {-1, -1};
        if (output && pipe(descriptors) == -1)
            throw std::runtime_error("pipe failed");
        pid_t child = fork();
        if (child == -1)
        {
            if (output)
            {
                close(descriptors[0]);
                close(descriptors[1]);
            }
            throw std::runtime_error("fork failed");
        }
        if (child == 0)
        {
            if (output)
            {
                close(descriptors[0]);
                if (dup2(descriptors[1], STDOUT_FILENO) == -1 || dup2(descriptors[1], STDERR_FILENO) == -1)
                    _exit(126);
                close(descriptors[1]);
            }
            if (!workingDir.empty() && chdir(workingDir.c_str()) != 0)
                _exit(126);
            for (const auto& [k, v] : environment)
                setenv(k.c_str(), v.c_str(), 1);
            std::vector<char*> ptrs;
            ptrs.reserve(args.size() + 1);
            for (const auto& s : args)
                ptrs.push_back(const_cast<char*>(s.c_str()));
            ptrs.push_back(nullptr);
            execvp(ptrs[0], ptrs.data());
            std::cerr << "axlum-build: could not execute " << args[0] << ": " << std::strerror(errno) << '\n';
            _exit(127);
        }
        std::exception_ptr callbackError;
        if (output)
        {
            close(descriptors[1]);
            char buffer[8192];
            while (true)
            {
                const auto count = read(descriptors[0], buffer, sizeof(buffer));
                if (count < 0 && errno == EINTR)
                    continue;
                if (count <= 0)
                    break;
                try
                {
                    output(std::string_view(buffer, static_cast<std::size_t>(count)));
                }
                catch (...)
                {
                    callbackError = std::current_exception();
                }
            }
            close(descriptors[0]);
        }
        int status = 0;
        while (waitpid(child, &status, 0) == -1)
        {
            if (errno != EINTR)
                throw std::runtime_error("waitpid failed");
        }
        if (callbackError)
            std::rethrow_exception(callbackError);
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        return 1;
#endif
    }
} // namespace axlum
