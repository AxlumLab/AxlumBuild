/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "managed/NativeImage.hpp"
#include "core/Process.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <openssl/evp.h>
#include <set>
#include <sstream>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        struct Reader
        {
            std::string m_data;

            void check(std::uint64_t p, std::uint64_t size) const
            {
                if (p > m_data.size() || size > m_data.size() - p)
                    throw std::runtime_error("truncated native image");
            }

            std::uint64_t u(std::uint64_t p, unsigned size) const
            {
                check(p, size);
                std::uint64_t n = 0;
                for (unsigned i = 0; i < size; ++i)
                    n |= std::uint64_t(static_cast<unsigned char>(m_data[static_cast<std::size_t>(p + i)])) << (i * 8);
                return n;
            }

            std::string z(std::uint64_t p) const
            {
                check(p, 1);
                const auto end = m_data.find('\0', static_cast<std::size_t>(p));
                if (end == std::string::npos)
                    throw std::runtime_error("unterminated native image string");
                return m_data.substr(static_cast<std::size_t>(p), end - static_cast<std::size_t>(p));
            }
        };

        void unique(std::vector<std::string>& values)
        {
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
        }
    } // namespace

    NativeImage NativeImageInspector::inspectNativeImage(const fs::path& file)
    {
        Reader r{FileSystem::readAll(file)};
        NativeImage image;
        if (r.u(0, 2) == 0x5a4d)
        {
            image.m_format = "PE";
            const auto pe = r.u(0x3c, 4);
            if (r.u(pe, 4) != 0x4550)
                throw std::runtime_error("invalid PE signature");
            const auto machine = r.u(pe + 4, 2);
            image.m_architecture = machine == 0x8664 ? "x64" : machine == 0xaa64 ? "arm64" : "unknown";
            const auto optional = pe + 24, magic = r.u(optional, 2);
            if (magic != 0x20b && magic != 0x10b)
                throw std::runtime_error("invalid PE optional header");
            const auto directories = optional + (magic == 0x20b ? 112 : 96);
            const auto count = r.u(optional + (magic == 0x20b ? 108 : 92), 4);
            const auto sections = optional + r.u(pe + 20, 2), number = r.u(pe + 6, 2);
            auto offset = [&](std::uint64_t address)
            {
                if (address < r.u(optional + 60, 4))
                {
                    r.check(address, 1);
                    return address;
                }
                for (std::uint64_t i = 0; i < number; ++i)
                {
                    const auto s = sections + i * 40, va = r.u(s + 12, 4), size = std::max(r.u(s + 8, 4), r.u(s + 16, 4));
                    if (address >= va && address - va < size)
                    {
                        const auto p = r.u(s + 20, 4) + address - va;
                        r.check(p, 1);
                        return p;
                    }
                }
                throw std::runtime_error("PE address is outside sections");
            };
            image.m_managed = count > 14 && r.u(directories + 14 * 8, 4) != 0;
            if (count && r.u(directories, 4))
            {
                const auto table = offset(r.u(directories, 4)), names = r.u(table + 24, 4);
                if (names > 1000000)
                    throw std::runtime_error("invalid PE export count");
                if (names)
                {
                    const auto pointers = offset(r.u(table + 32, 4));
                    for (std::uint64_t i = 0; i < names; ++i)
                        image.m_exports.push_back(r.z(offset(r.u(pointers + i * 4, 4))));
                }
            }
            if (count > 1 && r.u(directories + 8, 4))
            {
                const auto table = offset(r.u(directories + 8, 4));
                for (std::uint64_t i = 0; i < 100000; ++i)
                {
                    const auto p = table + i * 20, name = r.u(p + 12, 4);
                    if (!name)
                        break;
                    image.m_imports.push_back(r.z(offset(name)));
                }
            }
            if (count > 13 && r.u(directories + 13 * 8, 4))
            {
                const auto table = offset(r.u(directories + 13 * 8, 4));
                const auto base = r.u(optional + (magic == 0x20b ? 24 : 28), magic == 0x20b ? 8 : 4);
                for (std::uint64_t i = 0; i < 100000; ++i)
                {
                    const auto p = table + i * 32, name = r.u(p + 4, 4);
                    if (!name)
                        break;
                    image.m_imports.push_back(r.z(offset((r.u(p, 4) & 1) ? name : name - base)));
                }
            }
        }
        else if (r.u(0, 4) == 0x464c457f)
        {
            image.m_format = "ELF";
            if (r.u(4, 1) != 2 || r.u(5, 1) != 1)
                throw std::runtime_error("native inspection requires little-endian ELF64");
            const auto machine = r.u(18, 2);
            image.m_architecture = machine == 62 ? "x64" : machine == 183 ? "arm64" : "unknown";
            const auto sections = r.u(40, 8), stride = r.u(58, 2), count = r.u(60, 2);
            if (stride < 64 && count)
                throw std::runtime_error("invalid ELF section size");
            for (std::uint64_t i = 0; i < count; ++i)
            {
                const auto section = sections + i * stride, type = r.u(section + 4, 4);
                if (type != 11 && type != 6)
                    continue;
                const auto offset = r.u(section + 24, 8), size = r.u(section + 32, 8), link = r.u(section + 40, 4), item = r.u(section + 56, 8);
                if (link >= count || !item)
                    throw std::runtime_error("invalid ELF dynamic table");
                const auto strings = r.u(sections + link * stride + 24, 8);
                r.check(offset, size);
                if (type == 11 && item >= 24)
                    for (std::uint64_t at = 0; at + item <= size; at += item)
                    {
                        const auto p = offset + at, binding = r.u(p + 4, 1) >> 4;
                        if ((binding == 1 || binding == 2) && r.u(p + 6, 2) && (r.u(p + 5, 1) & 3) != 2 && r.u(p, 4))
                            image.m_exports.push_back(r.z(strings + r.u(p, 4)));
                    }
                if (type == 6 && item >= 16)
                    for (std::uint64_t at = 0; at + item <= size; at += item)
                        if (r.u(offset + at, 8) == 1)
                            image.m_imports.push_back(r.z(strings + r.u(offset + at + 8, 8)));
            }
        }
        else if (r.u(0, 4) == 0xfeedfacf)
        {
            image.m_format = "Mach-O";
            const auto cpu = r.u(4, 4);
            image.m_architecture = cpu == 0x1000007 ? "x64" : cpu == 0x100000c ? "arm64" : "unknown";
            auto p = std::uint64_t{32};
            for (std::uint64_t i = 0; i < r.u(16, 4); ++i)
            {
                const auto command = r.u(p, 4), size = r.u(p + 4, 4);
                if (size < 8)
                    throw std::runtime_error("invalid Mach-O command");
                r.check(p, size);
                if (command == 0xc || command == 0x80000018 || command == 0x8000001f)
                    image.m_imports.push_back(r.z(p + r.u(p + 8, 4)));
                if (command == 2)
                {
                    const auto symbols = r.u(p + 8, 4), count = r.u(p + 12, 4), strings = r.u(p + 16, 4);
                    if (count > 10000000)
                        throw std::runtime_error("invalid Mach-O symbol count");
                    for (std::uint64_t n = 0; n < count; ++n)
                    {
                        const auto entry = symbols + n * 16, type = r.u(entry + 4, 1);
                        if (!(type & 0xe0) && (type & 1) && (type & 0xe) == 0xe && r.u(entry, 4))
                        {
                            auto name = r.z(strings + r.u(entry, 4));
                            if (name.starts_with('_'))
                                name.erase(0, 1);
                            image.m_exports.push_back(std::move(name));
                        }
                    }
                }
                p += size;
            }
        }
        else
            throw std::runtime_error("unrecognized native image: " + file.string());
        unique(image.m_exports);
        unique(image.m_imports);
        return image;
    }

    std::string NativeImageInspector::fileSha256(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot hash file: " + file.string());
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("SHA256 initialization failed");
        char buffer[65536];
        while (stream)
        {
            stream.read(buffer, sizeof(buffer));
            if (EVP_DigestUpdate(ctx.get(), buffer, static_cast<std::size_t>(stream.gcount())) != 1)
                throw std::runtime_error("SHA256 update failed");
        }
        if (!stream.eof())
            throw std::runtime_error("cannot read file for hash: " + file.string());
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned length = 0;
        if (EVP_DigestFinal_ex(ctx.get(), hash, &length) != 1)
            throw std::runtime_error("SHA256 finalization failed");
        std::ostringstream hex;
        hex << std::hex << std::setfill('0');
        for (unsigned i = 0; i < length; ++i)
            hex << std::setw(2) << static_cast<unsigned>(hash[i]);
        return hex.str();
    }

    bool NativeImageInspector::systemNativeDependency(const std::string& value)
    {
        std::string name = fs::path(value).filename().string();
        std::transform(
            name.begin(),
            name.end(),
            name.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            }
        );
        static const std::set<std::string> s_windows = {"kernel32.dll", "kernelbase.dll",   "ntdll.dll",         "advapi32.dll", "bcrypt.dll",
                                                        "crypt32.dll",  "ncrypt.dll",       "ole32.dll",         "oleaut32.dll", "combase.dll",
                                                        "ws2_32.dll",   "secur32.dll",      "user32.dll",        "shell32.dll",  "version.dll",
                                                        "winhttp.dll",  "wininet.dll",      "normaliz.dll",      "iphlpapi.dll", "mswsock.dll",
                                                        "ucrtbase.dll", "vcruntime140.dll", "vcruntime140_1.dll"};
        if (s_windows.contains(name) || name.starts_with("api-ms-win-") || name.starts_with("ext-ms-win-"))
            return true;
        if (value.starts_with("/usr/lib/") || value.starts_with("/System/Library/"))
            return true;
        for (const auto* base :
             {"libc.so",
              "libm.so",
              "libdl.so",
              "libpthread.so",
              "librt.so",
              "libgcc_s.so",
              "libstdc++.so",
              "libresolv.so",
              "libutil.so",
              "ld-linux",
              "libSystem."})
            if (name.starts_with(base))
                return true;
        return false;
    }
} // namespace axlum
