/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/NativePaths.hpp"
#include "core/Process.hpp"
#include <stdexcept>

namespace axlum
{
    std::string NativePaths::expandBuildValue(std::string value, const Workspace& ws, const fs::path& build, Configuration config, const std::string& target)
    {
        for (const auto& [key, replacement] : std::vector<std::pair<std::string, std::string>>{
                 {"${workspace}", ws.m_root.string()}, {"${build}", build.string()}, {"${config}", BuildModel::toString(config)}, {"${target}", target}
             })
        {
            std::size_t pos = 0;
            while ((pos = value.find(key, pos)) != std::string::npos)
            {
                value.replace(pos, key.size(), replacement);
                pos += replacement.size();
            }
        }
        if (value.find("${") != std::string::npos)
            throw std::runtime_error("unsupported build substitution: " + value);
        return value;
    }

    fs::path NativePaths::resolveBuildInput(const Workspace& ws, const fs::path& build, const std::string& value)
    {
        return fs::absolute(value.starts_with("@build/") ? build / value.substr(7) : ws.m_root / value).lexically_normal();
    }

    namespace
    {
        std::string artifactName(const Target& target)
        {
            const auto name = target.m_outputName.empty() ? target.m_name : target.m_outputName;
            if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:\r\n") != std::string::npos)
                throw std::runtime_error("targetname must be a filename without a directory: " + target.m_name);
            return name;
        }

        fs::path artifactDirectory(std::string value, const Target& t, const Workspace& ws, const fs::path& build, Configuration cfg)
        {
            value = NativePaths::expandBuildValue(value, ws, build, cfg, t.m_name);
            if (value.starts_with("@build/"))
                value.erase(0, 7);
            const auto path = fs::absolute(build / value).lexically_normal();
            if (!FileSystem::isWithin(path, build))
                throw std::runtime_error("output directory must stay under build directory: " + value);
            return path;
        }
    } // namespace

    fs::path NativePaths::targetArtifact(const Target& t, const Toolchain& tc, const Workspace& ws, const fs::path& build, Configuration cfg)
    {
        const bool msvc = tc.m_kind == CompilerKind::MSVC;
        const auto name = artifactName(t);
        const auto filename = t.m_kind == TargetKind::Executable
                                  ? name + tc.m_extensionExecutable
                                  : (msvc ? "" : "lib") + name + (t.m_kind == TargetKind::StaticLibrary ? tc.m_extensionStatic : tc.m_extensionShared);
        const auto directory = t.m_outputDirectory.empty()
                                   ? (t.m_kind == TargetKind::StaticLibrary || (t.m_kind == TargetKind::SharedLibrary && !msvc) ? "lib" : "bin")
                                   : t.m_outputDirectory;
        return artifactDirectory(directory, t, ws, build, cfg) / filename;
    }

    fs::path NativePaths::targetObjects(const Target& t, const Workspace& ws, const fs::path& build, Configuration cfg)
    {
        return artifactDirectory(t.m_objectDirectory.empty() ? "obj/" + t.m_name : t.m_objectDirectory, t, ws, build, cfg);
    }

    fs::path NativePaths::targetImportLibrary(const Target& t, const Workspace& ws, const fs::path& build, Configuration cfg)
    {
        return artifactDirectory(t.m_importLibraryDirectory.empty() ? "lib" : t.m_importLibraryDirectory, t, ws, build, cfg) / (artifactName(t) + ".lib");
    }
} // namespace axlum
