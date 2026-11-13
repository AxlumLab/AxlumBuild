/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "core/Process.hpp"
#include "manifest/Manifest.hpp"
#include "packages/PackageManifest.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <rhodium/core/Environment.hpp>
#include <rhodium/diagnostics/DiagnosticSink.hpp>
#include <rhodium/script/LuaBindings.hpp>
#include <rhodium/script/LuaDependencyReader.hpp>
#include <rhodium/script/LuaRuntime.hpp>
#include <set>
#include <sol/sol.hpp>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        nlohmann::json jsonValue(sol::object value, unsigned depth = 0)
        {
            if (depth > 32)
                throw std::runtime_error("IDE JSON patch nesting exceeds 32 levels");
            switch (value.get_type())
            {
            case sol::type::string:
                return value.as<std::string>();
            case sol::type::boolean:
                return value.as<bool>();
            case sol::type::number:
                return value.as<double>();
            case sol::type::table:
            {
                const auto table = value.as<sol::table>();
                bool array = true;
                std::size_t count = 0;
                for (const auto& [key, item] : table)
                {
                    (void)item;
                    ++count;
                    if (key.get_type() != sol::type::number)
                        array = false;
                }
                if (count == 0)
                    array = false;
                auto json = array ? nlohmann::json::array() : nlohmann::json::object();
                if (array)
                {
                    for (std::size_t i = 1; i <= count; ++i)
                    {
                        sol::object item = table[i];
                        if (!item.valid())
                            throw std::runtime_error("IDE array must be contiguous");
                        json.push_back(jsonValue(item, depth + 1));
                    }
                }
                else
                    for (const auto& [key, item] : table)
                    {
                        if (key.get_type() != sol::type::string)
                            throw std::runtime_error("IDE object keys must be strings");
                        json[key.as<std::string>()] = jsonValue(item, depth + 1);
                    }
                return json;
            }
            default:
                throw std::runtime_error("IDE patch accepts JSON-compatible strings/numbers/booleans/tables");
            }
        }

        using Strings = std::vector<std::string>;

        std::string readText(sol::object value)
        {
            if (value.get_type() != sol::type::string)
                throw std::runtime_error("expected a string");
            return value.as<std::string>();
        }

        Strings readArray(sol::table values)
        {
            Strings result;
            for (std::size_t i = 1; i <= values.size(); ++i)
            {
                sol::object value = values[i];
                if (value.get_type() != sol::type::string)
                    throw std::runtime_error("expected an array of strings");
                result.push_back(readText(value));
            }
            if (result.size() != static_cast<std::size_t>(std::distance(values.begin(), values.end())))
                throw std::runtime_error("expected a contiguous array of strings");
            return result;
        }

        Strings readList(sol::object value)
        {
            if (value.get_type() == sol::type::string)
                return {readText(value)};
            if (value.get_type() == sol::type::table)
                return readArray(value.as<sol::table>());
            throw std::runtime_error("expected a string or an array of strings");
        }

        bool readBoolean(sol::object value)
        {
            if (value.get_type() == sol::type::boolean)
                return value.as<bool>();
            if (value.get_type() == sol::type::string)
            {
                auto text = value.as<std::string>();
                std::transform(
                    text.begin(),
                    text.end(),
                    text.begin(),
                    [](unsigned char c)
                    {
                        return static_cast<char>(std::tolower(c));
                    }
                );
                if (text == "on" || text == "true")
                    return true;
                if (text == "off" || text == "false")
                    return false;
            }
            throw std::runtime_error("expected a boolean or On/Off");
        }

        void append(Strings& output, const Strings& values)
        {
            output.insert(output.end(), values.begin(), values.end());
        }

        std::string toLower(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );
            return value;
        }

        // ** crosses directory boundaries; **/ also matches zero directories.
        std::regex glob(const std::string& pattern)
        {
            std::string out = "^";
            for (std::size_t i = 0; i < pattern.size(); ++i)
            {
                const char c = pattern[i];
                if (c == '*' && i + 1 < pattern.size() && pattern[i + 1] == '*')
                {
                    ++i;
                    if (i + 1 < pattern.size() && pattern[i + 1] == '/')
                    {
                        ++i;
                        out += "(.*/)?";
                    }
                    else
                        out += ".*";
                }
                else if (c == '*')
                    out += "[^/]*";
                else if (c == '?')
                    out += "[^/]";
                else
                {
                    if (std::string(".()+|^$[]{}\\").find(c) != std::string::npos)
                        out += '\\';
                    out += c;
                }
            }
            return std::regex(
                out + '$',
                std::regex::ECMAScript
#ifdef _WIN32
                    | std::regex::icase
#endif
            );
        }

        Strings expandFiles(const fs::path& root, const Strings& patterns, const fs::path& buildDirectory)
        {
            Strings result;
            std::set<std::string> seen;
            for (auto pattern : patterns)
            {
                std::replace(pattern.begin(), pattern.end(), '\\', '/');
                Strings matches;
                if (pattern.find_first_of("*?") == std::string::npos)
                {
                    if (!pattern.starts_with("@build/") && !fs::is_regular_file(root / pattern))
                        throw std::runtime_error("file does not exist: " + pattern);
                    matches.push_back(pattern);
                }
                else
                {
                    if (fs::path(pattern).is_absolute() || pattern.starts_with("@build/"))
                        throw std::runtime_error("file globs must be relative to the workspace: " + pattern);
                    const auto matcher = glob(pattern);
                    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it)
                    {
                        if (it->is_directory())
                        {
                            const auto name = it->path().filename().string();
                            if (name == ".git" || name == ".axlum" || name == ".cache" || name == ".rhodium" || name == ".tpkg" || name == "build" ||
                                name == "obj" || name == "bin" || (!buildDirectory.empty() && FileSystem::isWithin(it->path(), buildDirectory)) ||
                                fs::is_regular_file(it->path() / "axlum.state") || fs::is_regular_file(it->path() / "CMakeCache.txt"))
                                it.disable_recursion_pending();
                        }
                        else if (it->is_regular_file())
                        {
                            auto name = it->path().lexically_relative(root).generic_string();
                            if (std::regex_match(name, matcher))
                                matches.push_back(name);
                        }
                    }
                    std::sort(matches.begin(), matches.end());
                }
                for (const auto& match : matches)
                {
                    const auto normalized = fs::path(match).lexically_normal().generic_string();
                    auto key = normalized.starts_with("@build/") ? normalized : fs::absolute(root / normalized).lexically_normal().generic_string();
#ifdef _WIN32
                    key = toLower(key);
#endif
                    if (seen.insert(key).second)
                        result.push_back(normalized);
                }
            }
            return result;
        }

        bool matchesFilter(const Strings& terms, Configuration config, const std::string& architecture, const std::string& platform)
        {
            bool matches = true;
            for (auto term : terms)
            {
                term = toLower(term);
                const auto colon = term.find(':');
                if (colon == std::string::npos)
                    throw std::runtime_error("filter requires key:value terms: " + term);
                const auto key = term.substr(0, colon);
                const auto value = term.substr(colon + 1);
                bool result;
                if (key == "configurations" || key == "configuration")
                    result = toLower(BuildModel::toString(BuildModel::parseConfiguration(value))) == toLower(BuildModel::toString(config));
                else if (key == "system" || key == "platform")
                {
                    if (value != "windows" && value != "linux" && value != "macos" && value != "android" && value != "ios" && value != "ios-simulator")
                        throw std::runtime_error("unsupported filter platform: " + value);
                    result = value == (platform.empty() ? rhodium::Environment::hostPlatformName() : platform);
                }
                else if (key == "architecture")
                {
                    result = BuildModel::normalizeArchitecture(value) == architecture;
                }
                else
                    throw std::runtime_error("unknown filter key: " + key);
                matches = matches && result;
            }
            return matches;
        }

        class Diagnostics final : public rhodium::DiagnosticSink
        {
          public:
            bool m_failed = false;

            void report(const rhodium::Diagnostic& d) override
            {
                if (d.level == rhodium::DiagnosticLevel::Error)
                    m_failed = true;
                std::cerr << "rhodium: " << d.message << '\n';
            }
        };
    } // namespace

    Workspace ManifestLoader::loadLuaManifest(
        const fs::path& filename,
        Configuration config,
        const fs::path& buildDirectory,
        const std::string& architecture,
        const std::string& platform
    )
    {
        Workspace ws;
        ws.m_manifest = fs::absolute(filename).lexically_normal();
        ws.m_root = ws.m_manifest.parent_path();
        if (!fs::is_regular_file(ws.m_manifest))
            throw std::runtime_error("manifest not found: " + ws.m_manifest.string());
        const auto targetPlatform = platform.empty() ? rhodium::Environment::hostPlatformName() : platform;
        ws.m_packageManifest = std::make_shared<PackageManifest>();
        auto& metadata = ws.m_packageManifest->m_model;
        metadata.manifestName = ws.m_manifest.filename().string();
        Diagnostics diagnostics;
        rhodium::LuaRuntime runtime;
        auto& lua = runtime.state();
        rhodium::LuaBindings::bind(lua, metadata, diagnostics, {targetPlatform, ws.m_root, ws.m_manifest});
        std::size_t current = 0;
        bool hasTarget = false, enabled = true, workspaceSeen = false;
        Module* module = nullptr;
        CompileOptions defaults;
        ManagedOptions managedDefaults;
        std::string defaultLanguage;
        auto target = [&]() -> Target&
        {
            if (!hasTarget)
                throw std::runtime_error("target(...) must precede target settings");
            return ws.m_targets.at(current);
        };
        auto compile = [&]() -> CompileOptions&
        {
            if (!workspaceSeen)
                throw std::runtime_error("workspace(...) must precede settings");
            return module ? module->m_compile : hasTarget ? target().m_compile : defaults;
        };
        auto scalar = [&](const char* name, auto setter)
        {
            lua.set_function(
                name,
                [&, setter](sol::object value)
                {
                    if (enabled)
                        setter(readText(value));
                }
            );
        };
        auto list = [&](const char* name, auto destination)
        {
            lua.set_function(
                name,
                [&, destination](sol::object value)
                {
                    if (enabled)
                        append(destination(), readList(value));
                }
            );
        };
        auto boolean = [&](const char* name, auto setter)
        {
            lua.set_function(
                name,
                [&, setter](sol::object value)
                {
                    if (enabled)
                        setter(readBoolean(value));
                }
            );
        };
        auto choice = [&](const char* name, auto destination, std::initializer_list<const char*> choices)
        {
            const std::vector<std::string> accepted(choices.begin(), choices.end());
            scalar(
                name,
                [destination, accepted, name](const std::string& value)
                {
                    const auto normalized = toLower(value);
                    if (std::find(accepted.begin(), accepted.end(), normalized) == accepted.end())
                        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
                    destination() = normalized;
                }
            );
        };
        lua.set_function(
            "workspace",
            [&](sol::object value)
            {
                if (workspaceSeen)
                    throw std::runtime_error("workspace may only be declared once");
                ws.m_name = readText(value);
                metadata.rootPackage.name = ws.m_name;
                workspaceSeen = true;
            }
        );
        lua.set_function(
            "configuration",
            [&](sol::variadic_args args)
            {
                if (!workspaceSeen || hasTarget)
                    throw std::runtime_error("configuration belongs to workspace scope");
                if (args.size() == 0)
                    throw std::runtime_error("configuration requires Debug and/or Release");
                for (auto arg : args)
                {
                    sol::object value = arg;
                    const auto values = readList(value);
                    if (values.empty())
                        throw std::runtime_error("configuration requires Debug and/or Release");
                    for (const auto& name : values)
                    {
                        const auto configuration = BuildModel::parseConfiguration(name);
                        if (std::any_of(
                                ws.m_configurations.begin(),
                                ws.m_configurations.end(),
                                [&](const auto& other)
                                {
                                    return toLower(BuildModel::toString(other)) == toLower(BuildModel::toString(configuration));
                                }
                            ))
                            throw std::runtime_error("duplicate configuration: " + name);
                        ws.m_configurations.push_back(configuration);
                    }
                }
            }
        );
        lua.set_function(
            "architecture",
            [&](sol::object value)
            {
                if (!workspaceSeen || hasTarget)
                    throw std::runtime_error("architecture belongs to workspace scope");
                const auto values = readList(value);
                if (values.empty())
                    throw std::runtime_error("architecture requires values");
                for (const auto& arch : values)
                {
                    const auto normalized = BuildModel::normalizeArchitecture(arch);
                    if (std::find(ws.m_architectures.begin(), ws.m_architectures.end(), normalized) != ws.m_architectures.end())
                        throw std::runtime_error("duplicate architecture: " + arch);
                    ws.m_architectures.push_back(normalized);
                }
            }
        );
        lua.set_function(
            "preset",
            [&](const std::string& name, sol::table options)
            {
                if (!workspaceSeen || hasTarget)
                    throw std::runtime_error("preset belongs to workspace scope");
                BuildPreset preset;
                preset.m_name = name;
                for (auto [key, value] : options)
                {
                    const auto field = readText(key), text = readText(value);
                    if (field == "config")
                        preset.m_configuration = BuildModel::toString(BuildModel::parseConfiguration(text));
                    else if (field == "architecture")
                        preset.m_architecture = BuildModel::normalizeArchitecture(text);
                    else if (field == "platform")
                        preset.m_platform = text;
                    else if (field == "toolchain")
                        preset.m_toolchain = text;
                    else if (field == "compiler")
                        preset.m_compiler = text;
                    else
                        throw std::runtime_error("unknown preset field: " + field);
                }
                if (!std::regex_match(name, std::regex("[A-Za-z_][A-Za-z0-9_-]*")) || std::any_of(
                                                                                          ws.m_presets.begin(),
                                                                                          ws.m_presets.end(),
                                                                                          [&](const auto& p)
                                                                                          {
                                                                                              return p.m_name == name;
                                                                                          }
                                                                                      ))
                    throw std::runtime_error("invalid or duplicate preset: " + name);
                ws.m_presets.push_back(std::move(preset));
            }
        );
        lua.set_function(
            "workspace_reference",
            [&](const std::string& alias, sol::table options)
            {
                if (!workspaceSeen || hasTarget)
                    throw std::runtime_error("workspace_reference belongs to workspace scope");
                if (!std::regex_match(alias, std::regex("[A-Za-z_][A-Za-z0-9_-]*")))
                    throw std::runtime_error("invalid workspace alias: " + alias);
                ws.m_references.push_back({alias, ws.m_root / readText(options["file"])});
            }
        );
        lua.set_function(
            "filter",
            [&](sol::object value)
            {
                if (!workspaceSeen)
                    throw std::runtime_error("workspace(...) must precede filter");
                enabled = matchesFilter(readList(value), config, architecture, platform);
            }
        );
        lua.set_function(
            "target",
            [&](sol::object value)
            {
                if (!workspaceSeen)
                    throw std::runtime_error("workspace(...) must precede target");
                ws.m_targets.emplace_back();
                current = ws.m_targets.size() - 1;
                hasTarget = true;
                module = nullptr;
                enabled = true;
                ws.m_targets.back().m_name = readText(value);
                ws.m_targets.back().m_compile = defaults;
                ws.m_targets.back().m_language = defaultLanguage;
            }
        );
        lua.set_function(
            "module",
            [&](sol::object value)
            {
                const auto owner = target().m_name;
                ws.m_modules.emplace_back();
                module = &ws.m_modules.back();
                module->m_name = readText(value);
                module->m_targetName = owner;
                module->m_compile.m_cppStandard.clear();
                module->m_compile.m_cStandard.clear();
                enabled = true;
            }
        );
        auto targetSetting = [&]() -> Target&
        {
            if (module)
                throw std::runtime_error("setting belongs to target scope, not module scope");
            return target();
        };
        lua.set_function(
            "ide_customize",
            [&](const std::string& format, sol::table values)
            {
                if (!enabled)
                    return;
                if (!workspaceSeen || module)
                    throw std::runtime_error("ide_customize belongs to workspace/target scope");
                if (format != "vs" && format != "vscode" && format != "xcode" && format != "android-studio")
                    throw std::runtime_error("unknown IDE format: " + format);
                IDECustomization custom;
                for (const auto& [key, value] : values)
                {
                    const auto name = readText(key);
                    if (name == "properties" || name == "debugger")
                    {
                        if (value.get_type() != sol::type::table)
                            throw std::runtime_error("IDE properties/debugger requires a table");
                        auto& map = name == "properties" ? custom.m_properties : custom.m_debugger;
                        for (const auto& [property, text] : value.as<sol::table>())
                        {
                            const auto propertyName = readText(property);
                            if (!std::regex_match(propertyName, std::regex("[A-Za-z_][A-Za-z0-9_.-]*")))
                                throw std::runtime_error("invalid IDE property: " + propertyName);
                            map[propertyName] = readText(text);
                        }
                    }
                    else if (name == "patch")
                    {
                        auto patch = value.get_type() == sol::type::string ? nlohmann::json::parse(readText(value)) : jsonValue(value);
                        if (!patch.is_object())
                            throw std::runtime_error("IDE patch must be a JSON object");
                        custom.m_jsonPatch = patch.dump();
                    }
                    else
                    {
                        auto* field = name == "project_begin"   ? &custom.m_projectBegin
                                      : name == "project_end"   ? &custom.m_projectEnd
                                      : name == "filters_begin" ? &custom.m_filtersBegin
                                      : name == "filters_end"   ? &custom.m_filtersEnd
                                      : name == "user_begin"    ? &custom.m_userBegin
                                      : name == "user_end"      ? &custom.m_userEnd
                                                                : nullptr;
                        if (!field)
                            throw std::runtime_error("unknown ide_customize field: " + name);
                        *field = readText(value);
                    }
                }
                if (format != "vscode" && !custom.m_jsonPatch.empty())
                    throw std::runtime_error("IDE JSON patch applies to vscode; use properties/fragments for other formats");
                if (format != "vs" &&
                    (!custom.m_filtersBegin.empty() || !custom.m_filtersEnd.empty() || !custom.m_userBegin.empty() || !custom.m_userEnd.empty()))
                    throw std::runtime_error("filters/user fragments apply to Visual Studio");
                BuildModel::mergeIDECustomization((hasTarget ? targetSetting().m_ide : ws.m_ide)[format], custom);
            }
        );
        scalar(
            "kind",
            [&](const std::string& value)
            {
                auto& t = targetSetting();
                const bool wasManaged = t.m_kind == TargetKind::DotNetProject;
                if (value == "ConsoleApp" || value == "Executable")
                    t.m_kind = TargetKind::Executable;
                else if (value == "StaticLibrary")
                    t.m_kind = TargetKind::StaticLibrary;
                else if (value == "SharedLibrary")
                    t.m_kind = TargetKind::SharedLibrary;
                else if (value == "HeaderOnly")
                    t.m_kind = TargetKind::HeaderOnly;
                else if (value == "CSharpLibrary" || value == "CSharpConsole")
                {
                    t.m_kind = TargetKind::DotNetProject;
                    t.m_managedExecutable = value == "CSharpConsole";
                }
                else if (value == "DotNetProject")
                    t.m_kind = TargetKind::DotNetProject;
                else if (value == "Codegen")
                    t.m_kind = TargetKind::Codegen;
                else
                    throw std::runtime_error("unsupported kind: " + value);
                if (t.m_kind == TargetKind::DotNetProject && !wasManaged && value != "DotNetProject")
                    t.m_managed = managedDefaults;
            }
        );
        auto limits = [&](const char* name, auto member, auto normalize)
        {
            lua.set_function(
                name,
                [&, member, normalize](sol::object value)
                {
                    if (!enabled)
                        return;
                    auto values = readList(value);
                    if (values.empty())
                        throw std::runtime_error("target support list must not be empty");
                    auto& output = targetSetting().*member;
                    for (auto item : values)
                    {
                        item = normalize(item);
                        if (std::find(output.begin(), output.end(), item) != output.end())
                            throw std::runtime_error("duplicate target support value: " + item);
                        output.push_back(item);
                    }
                }
            );
        };
        limits(
            "platforms",
            &Target::m_platforms,
            [](const std::string& v)
            {
                const auto p = toLower(v);
                if (p != "windows" && p != "linux" && p != "macos" && p != "android" && p != "ios" && p != "ios-simulator")
                    throw std::runtime_error("unsupported target platform: " + v);
                return p;
            }
        );
        limits(
            "architectures",
            &Target::m_architectures,
            [](const std::string& v)
            {
                return BuildModel::normalizeArchitecture(v);
            }
        );
        limits(
            "configurations",
            &Target::m_configurations,
            [](const std::string& v)
            {
                return BuildModel::toString(BuildModel::parseConfiguration(v));
            }
        );
        auto externalMap = [&](const char* name, auto member)
        {
            lua.set_function(
                name,
                [&, member](sol::table values)
                {
                    if (!enabled)
                        return;
                    auto& destination = targetSetting().m_nativeProject.*member;
                    for (const auto& [key, value] : values)
                    {
                        auto name = readText(key);
                        if (member == &NativeProjectOptions::m_configurations)
                            name = BuildModel::toString(BuildModel::parseConfiguration(name));
                        else if (member == &NativeProjectOptions::m_architectures)
                            name = BuildModel::normalizeArchitecture(name);
                        destination[name] = readText(value);
                    }
                }
            );
        };
        externalMap("external_configurations", &NativeProjectOptions::m_configurations);
        externalMap("external_architectures", &NativeProjectOptions::m_architectures);
        externalMap("external_properties", &NativeProjectOptions::m_properties);
        scalar(
            "language",
            [&](const std::string& value)
            {
                if (!workspaceSeen)
                    throw std::runtime_error("workspace(...) must precede language");
                if (value != "C" && value != "C++" && value != "C#" && value != "Objective-C" && value != "Objective-C++")
                    throw std::runtime_error("unsupported language: " + value);
                (hasTarget ? targetSetting().m_language : defaultLanguage) = value;
            }
        );
        scalar(
            "cppdialect",
            [&](const std::string& value)
            {
                const auto standard = toLower(value);
                if (standard != "c++14" && standard != "c++17" && standard != "c++20" && standard != "c++23" && standard != "latest" && standard != "c++latest")
                    throw std::runtime_error("unsupported C++ dialect: " + value);
                compile().m_cppStandard = standard == "c++latest" ? "latest" : standard;
            }
        );
        scalar(
            "cdialect",
            [&](const std::string& value)
            {
                const auto standard = toLower(value);
                if (standard != "c11" && standard != "c17")
                    throw std::runtime_error("unsupported C dialect: " + value);
                compile().m_cStandard = standard;
            }
        );
        list(
            "files",
            [&]() -> Strings&
            {
                return module ? module->m_sources : target().m_sources;
            }
        );
        list(
            "removefiles",
            [&]() -> Strings&
            {
                return module ? module->m_excludedSources : target().m_excludedSources;
            }
        );
        list(
            "includedirs",
            [&]() -> Strings&
            {
                return compile().m_privateIncludes;
            }
        );
        list(
            "public_includedirs",
            [&]() -> Strings&
            {
                return compile().m_publicIncludes;
            }
        );
        list(
            "defines",
            [&]() -> Strings&
            {
                return compile().m_privateDefines;
            }
        );
        list(
            "public_defines",
            [&]() -> Strings&
            {
                return compile().m_publicDefines;
            }
        );
        list(
            "cflags",
            [&]() -> Strings&
            {
                return compile().m_cFlags;
            }
        );
        list(
            "cxxflags",
            [&]() -> Strings&
            {
                return compile().m_cxxFlags;
            }
        );
        choice(
            "optimize",
            [&]() -> std::string&
            {
                return compile().m_optimization;
            },
            {"off", "on", "speed", "size", "full"}
        );
        choice(
            "symbols",
            [&]() -> std::string&
            {
                return compile().m_symbols;
            },
            {"on", "off", "full"}
        );
        choice(
            "warnings",
            [&]() -> std::string&
            {
                return compile().m_warnings;
            },
            {"off", "default", "extra", "all"}
        );
        choice(
            "runtime",
            [&]() -> std::string&
            {
                return compile().m_runtime;
            },
            {"static", "dynamic"}
        );
        boolean(
            "warningsaserrors",
            [&](bool v)
            {
                compile().m_warningsAsErrors = v;
            }
        );
        boolean(
            "exceptions",
            [&](bool v)
            {
                compile().m_exceptions = v;
            }
        );
        boolean(
            "rtti",
            [&](bool v)
            {
                compile().m_rtti = v;
            }
        );
        boolean(
            "lto",
            [&](bool v)
            {
                compile().m_lto = v;
            }
        );
        boolean(
            "pic",
            [&](bool v)
            {
                compile().m_pic = v;
            }
        );
        choice(
            "inlining",
            [&]() -> std::string&
            {
                return compile().m_inlining;
            },
            {"off", "default", "on", "aggressive"}
        );
        lua.set_function(
            "sanitizers",
            [&](sol::object value)
            {
                if (enabled)
                    for (const auto& name : readList(value))
                        compile().m_sanitizers.push_back(toLower(name));
            }
        );
        boolean(
            "functionlevellinking",
            [&](bool v)
            {
                compile().m_functionLevelLinking = v;
            }
        );
        boolean(
            "runtimechecks",
            [&](bool v)
            {
                compile().m_runtimeChecks = v;
            }
        );
        boolean(
            "stringpooling",
            [&](bool v)
            {
                compile().m_stringPooling = v;
            }
        );
        boolean(
            "intrinsics",
            [&](bool v)
            {
                compile().m_intrinsicFunctions = v;
            }
        );
        boolean(
            "buffersecuritycheck",
            [&](bool v)
            {
                compile().m_bufferSecurityCheck = v;
            }
        );
        boolean(
            "debugcrt",
            [&](bool v)
            {
                compile().m_debugCRT = v;
            }
        );
        boolean(
            "sharedpch",
            [&](bool v)
            {
                compile().m_sharedPch = v;
            }
        );
        boolean(
            "staticruntime",
            [&](bool v)
            {
                compile().m_runtime = v ? "static" : "dynamic";
            }
        );
        boolean(
            "pch",
            [&](bool v)
            {
                compile().m_pchEnabled = v;
            }
        );
        scalar(
            "pchfile",
            [&](const std::string& v)
            {
                compile().m_pchFile = v;
            }
        );
        scalar(
            "pchobject",
            [&](const std::string& v)
            {
                compile().m_pchObject = v;
            }
        );
        boolean(
            "cppdocumentation",
            [&](bool v)
            {
                compile().m_documentation = v;
            }
        );
        scalar(
            "documentationdir",
            [&](const std::string& v)
            {
                targetSetting().m_documentationDirectory = v;
            }
        );
        scalar(
            "pchheader",
            [&](const std::string& v)
            {
                compile().m_pchHeader = v;
            }
        );
        scalar(
            "pchsource",
            [&](const std::string& v)
            {
                if (!hasTarget)
                    throw std::runtime_error("pchsource belongs to target/module scope");
                compile().m_pchSource = v;
            }
        );
        list(
            "links",
            [&]() -> Strings&
            {
                return targetSetting().m_link.m_libraries;
            }
        );
        list(
            "frameworks",
            [&]() -> Strings&
            {
                return targetSetting().m_link.m_frameworks;
            }
        );
        list(
            "frameworkdirs",
            [&]() -> Strings&
            {
                return targetSetting().m_link.m_frameworkDirectories;
            }
        );
        list(
            "public_modules",
            [&]() -> Strings&
            {
                return targetSetting().m_publicModules;
            }
        );
        list(
            "private_modules",
            [&]() -> Strings&
            {
                return targetSetting().m_privateModules;
            }
        );
        list(
            "libdirs",
            [&]() -> Strings&
            {
                return targetSetting().m_link.m_libraryDirectories;
            }
        );
        list(
            "linkoptions",
            [&]() -> Strings&
            {
                return targetSetting().m_link.m_linkFlags;
            }
        );
        choice(
            "debuglink",
            [&]() -> std::string&
            {
                return targetSetting().m_link.m_debugLink;
            },
            {"default", "full", "fastlink"}
        );
        boolean(
            "incrementallink",
            [&](bool v)
            {
                targetSetting().m_link.m_incremental = v;
            }
        );
        choice(
            "subsystem",
            [&]() -> std::string&
            {
                return targetSetting().m_link.m_subsystem;
            },
            {"console", "windows"}
        );
        scalar(
            "entrypoint",
            [&](const std::string& v)
            {
                targetSetting().m_link.m_entryPoint = v;
            }
        );
        scalar(
            "moduledefinition",
            [&](const std::string& v)
            {
                targetSetting().m_link.m_moduleDefinition = v;
            }
        );
        scalar(
            "win32manifest",
            [&](const std::string& v)
            {
                targetSetting().m_link.m_manifest = v;
            }
        );
        list(
            "delayloaddlls",
            [&]() -> Strings&
            {
                return targetSetting().m_link.m_delayLoadLibraries;
            }
        );
        list(
            "exports",
            [&]() -> Strings&
            {
                return targetSetting().m_link.m_exports;
            }
        );
        boolean(
            "importlibrary",
            [&](bool v)
            {
                targetSetting().m_link.m_importLibrary = v;
            }
        );
        scalar(
            "android_applicationid",
            [&](const std::string& v)
            {
                targetSetting().m_android.m_applicationId = v;
            }
        );
        scalar(
            "android_activity",
            [&](const std::string& v)
            {
                targetSetting().m_android.m_activity = v;
            }
        );
        scalar(
            "android_manifest",
            [&](const std::string& v)
            {
                targetSetting().m_android.m_manifest = v;
            }
        );
        lua.set_function(
            "android_min_sdk",
            [&](int v)
            {
                if (enabled)
                {
                    if (v < 21 || v > 100)
                        throw std::runtime_error("invalid Android min SDK");
                    targetSetting().m_android.m_minSdk = v;
                }
            }
        );
        lua.set_function(
            "android_target_sdk",
            [&](int v)
            {
                if (enabled)
                {
                    if (v < 21 || v > 100)
                        throw std::runtime_error("invalid Android target SDK");
                    targetSetting().m_android.m_targetSdk = v;
                }
            }
        );
        scalar(
            "win32icon",
            [&](const std::string& v)
            {
                targetSetting().m_icon = v;
            }
        );
        list(
            "win32resourcefile",
            [&]() -> Strings&
            {
                return targetSetting().m_sources;
            }
        );
        scalar(
            "targetname",
            [&](const std::string& v)
            {
                targetSetting().m_outputName = v;
            }
        );
        list(
            "installheaders",
            [&]() -> Strings&
            {
                return targetSetting().m_install.m_headers;
            }
        );
        list(
            "installfiles",
            [&]() -> Strings&
            {
                return targetSetting().m_install.m_files;
            }
        );
        scalar(
            "installheaderbase",
            [&](const std::string& value)
            {
                targetSetting().m_install.m_headerBase = value;
            }
        );
        scalar(
            "installbindir",
            [&](const std::string& value)
            {
                targetSetting().m_install.m_bindir = value;
            }
        );
        scalar(
            "installlibdir",
            [&](const std::string& value)
            {
                targetSetting().m_install.m_libdir = value;
            }
        );
        scalar(
            "installincludedir",
            [&](const std::string& value)
            {
                targetSetting().m_install.m_includedir = value;
            }
        );
        scalar(
            "installdatadir",
            [&](const std::string& value)
            {
                targetSetting().m_install.m_datadir = value;
            }
        );
        boolean(
            "installable",
            [&](bool value)
            {
                targetSetting().m_install.m_enabled = value;
            }
        );
        scalar(
            "targetdir",
            [&](const std::string& v)
            {
                targetSetting().m_outputDirectory = v;
            }
        );
        scalar(
            "objdir",
            [&](const std::string& v)
            {
                targetSetting().m_objectDirectory = v;
            }
        );
        scalar(
            "implibdir",
            [&](const std::string& v)
            {
                targetSetting().m_importLibraryDirectory = v;
            }
        );
        list(
            "runtimefiles",
            [&]() -> Strings&
            {
                return targetSetting().m_runtimeFiles;
            }
        );
        list(
            "optionalruntimefiles",
            [&]() -> Strings&
            {
                return targetSetting().m_optionalRuntimeFiles;
            }
        );
        list(
            "resources",
            [&]() -> Strings&
            {
                return targetSetting().m_sources;
            }
        );
        list(
            "resourceinputs",
            [&]() -> Strings&
            {
                return targetSetting().m_resourceInputs;
            }
        );
        list(
            "public_deps",
            [&]() -> Strings&
            {
                return module ? module->m_publicDependencies : target().m_publicDependencies;
            }
        );
        list(
            "private_deps",
            [&]() -> Strings&
            {
                return module ? module->m_privateDependencies : target().m_privateDependencies;
            }
        );
        list(
            "dependson",
            [&]() -> Strings&
            {
                return module ? module->m_privateDependencies : target().m_privateDependencies;
            }
        );
        choice(
            "linktype",
            [&]() -> std::string&
            {
                return targetSetting().m_linkType;
            },
            {"monolithic", "modular"}
        );
        scalar(
            "binarymodulename",
            [&](const std::string& v)
            {
                if (!module)
                    throw std::runtime_error("binarymodulename belongs to module scope");
                module->m_binaryModuleName = v;
            }
        );
        scalar(
            "apidefine",
            [&](const std::string& v)
            {
                if (!module || !std::regex_match(v, std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                    throw std::runtime_error("apidefine requires a C identifier in module scope");
                module->m_apiDefine = v;
            }
        );
        list(
            "interface_deps",
            [&]() -> Strings&
            {
                return targetSetting().m_interfaceDependencies;
            }
        );
        scalar(
            "framework",
            [&](const std::string& s)
            {
                targetSetting().m_framework = s;
            }
        );
        scalar(
            "project",
            [&](const std::string& s)
            {
                targetSetting().m_project = s;
            }
        );
        list(
            "references",
            [&]() -> Strings&
            {
                return targetSetting().m_references;
            }
        );
        auto managed = [&]() -> ManagedOptions&
        {
            if (!workspaceSeen || module || (hasTarget && target().m_kind != TargetKind::DotNetProject))
                throw std::runtime_error("managed settings require workspace defaults or a C# target");
            return hasTarget ? target().m_managed : managedDefaults;
        };
        auto property = [&](const char* name, const char* sdk)
        {
            scalar(
                name,
                [&, sdk](const std::string& value)
                {
                    managed().m_properties[sdk] = value;
                }
            );
        };
        auto managedBool = [&](const char* name, const char* sdk)
        {
            boolean(
                name,
                [&, sdk](bool value)
                {
                    managed().m_properties[sdk] = value ? "true" : "false";
                }
            );
        };
        for (const auto& [name, sdk] : std::vector<std::pair<const char*, const char*>>{
                 {"cslangversion", "LangVersion"},
                 {"analysislevel", "AnalysisLevel"},
                 {"assemblyname", "AssemblyName"},
                 {"rootnamespace", "RootNamespace"},
                 {"assemblyversion", "AssemblyVersion"},
                 {"fileversion", "FileVersion"},
                 {"version", "Version"},
                 {"informationalversion", "InformationalVersion"},
                 {"assemblytitle", "Title"},
                 {"description", "Description"},
                 {"company", "Company"},
                 {"product", "Product"},
                 {"copyright", "Copyright"}
             })
            property(name, sdk);
        for (const auto& [name, sdk] : std::vector<std::pair<const char*, const char*>>{
                 {"implicitusings", "ImplicitUsings"},
                 {"allowunsafe", "AllowUnsafeBlocks"},
                 {"checkoverflow", "CheckForOverflowUnderflow"},
                 {"deterministic", "Deterministic"},
                 {"documentationfile", "GenerateDocumentationFile"},
                 {"generateassemblyinfo", "GenerateAssemblyInfo"},
                 {"enableanalyzers", "EnableNETAnalyzers"},
                 {"enforcecodestyle", "EnforceCodeStyleInBuild"},
                 {"emitgeneratedfiles", "EmitCompilerGeneratedFiles"}
             })
            managedBool(name, sdk);
        choice(
            "nullable",
            [&]() -> std::string&
            {
                return managed().m_properties["Nullable"];
            },
            {"enable", "disable", "warnings", "annotations"}
        );
        lua.set_function(
            "warninglevel",
            [&](sol::object value)
            {
                if (!enabled)
                    return;
                const auto number = value.get_type() == sol::type::number ? value.as<int>() : std::stoi(readText(value));
                if (number < 0 || number > 9999)
                    throw std::runtime_error("invalid C# warning level");
                managed().m_properties["WarningLevel"] = std::to_string(number);
            }
        );
        list(
            "csdefines",
            [&]() -> Strings&
            {
                return managed().m_defines;
            }
        );
        list(
            "nowarn",
            [&]() -> Strings&
            {
                return managed().m_noWarn;
            }
        );
        list(
            "warnings_as_errors",
            [&]() -> Strings&
            {
                return managed().m_warningsAsErrors;
            }
        );
        list(
            "warnings_not_as_errors",
            [&]() -> Strings&
            {
                return managed().m_warningsNotAsErrors;
            }
        );
        list(
            "analyzers",
            [&]() -> Strings&
            {
                return managed().m_analyzers;
            }
        );
        list(
            "sourcegenerators",
            [&]() -> Strings&
            {
                return managed().m_analyzers;
            }
        );
        list(
            "analyzer_projects",
            [&]() -> Strings&
            {
                return managed().m_analyzerProjects;
            }
        );
        list(
            "additionalfiles",
            [&]() -> Strings&
            {
                return managed().m_additionalFiles;
            }
        );
        list(
            "editorconfigs",
            [&]() -> Strings&
            {
                return managed().m_editorConfigs;
            }
        );
        list(
            "nativelibraries",
            [&]() -> Strings&
            {
                return managed().m_nativeLibraries;
            }
        );
        list(
            "directpinvoke",
            [&]() -> Strings&
            {
                return managed().m_directPInvoke;
            }
        );
        lua.set_function(
            "assembly_attribute",
            [&](const std::string& type, sol::object args)
            {
                if (!enabled)
                    return;
                if (!std::regex_match(type, std::regex("[A-Za-z_][A-Za-z0-9_.+]*")))
                    throw std::runtime_error("invalid assembly attribute type");
                managed().m_attributes.push_back({type, readList(args)});
            }
        );
        lua.set_function(
            "nuget",
            [&](const std::string& name, sol::table options)
            {
                if (!enabled)
                    return;
                for (auto [key, value] : options)
                {
                    (void)value;
                    const auto field = readText(key);
                    if (field != "version" && field != "private_assets" && field != "include_assets" && field != "exclude_assets")
                        throw std::runtime_error("unknown NuGet reference field: " + field);
                }
                auto read = [&](const char* key)
                {
                    sol::object value = options[key];
                    return value.get_type() == sol::type::nil ? std::string{} : readText(value);
                };
                NuGetReference reference{name, read("version"), read("private_assets"), read("include_assets"), read("exclude_assets")};
                if (name.empty() || reference.m_version.empty())
                    throw std::runtime_error("NuGet references require name and version");
                managed().m_packages.push_back(std::move(reference));
            }
        );
        auto publish = [&]() -> PublishOptions&
        {
            auto& t = targetSetting();
            if (t.m_kind != TargetKind::DotNetProject)
                throw std::runtime_error("publish settings require a .NET target");
            return t.m_publish;
        };
        scalar(
            "runtimeidentifier",
            [&](const std::string& value)
            {
                publish().m_rid = value;
            }
        );
        scalar(
            "publishdir",
            [&](const std::string& value)
            {
                publish().m_directory = value;
            }
        );
        scalar(
            "native_library",
            [&](const std::string& value)
            {
                if (toLower(value) != "shared")
                    throw std::runtime_error("official NativeAOT libraries support NativeLib=Shared");
                publish().m_nativeLibrary = "Shared";
            }
        );
        boolean(
            "publishaot",
            [&](bool value)
            {
                publish().m_aot = value;
            }
        );
        boolean(
            "selfcontained",
            [&](bool value)
            {
                publish().m_selfContained = value;
            }
        );
        boolean(
            "publishtrimmed",
            [&](bool value)
            {
                publish().m_trimmed = value;
            }
        );
        boolean(
            "publishsinglefile",
            [&](bool value)
            {
                publish().m_singleFile = value;
            }
        );
        boolean(
            "stripsymbols",
            [&](bool value)
            {
                publish().m_stripSymbols = value;
            }
        );
        list(
            "native_exports",
            [&]() -> Strings&
            {
                return publish().m_exports;
            }
        );
        list(
            "native_dependencies",
            [&]() -> Strings&
            {
                return publish().m_dependencies;
            }
        );
        list(
            "system_dependencies",
            [&]() -> Strings&
            {
                return publish().m_systemDependencies;
            }
        );
        scalar(
            "tool",
            [&](const std::string& s)
            {
                targetSetting().m_tool = s;
            }
        );
        list(
            "args",
            [&]() -> Strings&
            {
                return targetSetting().m_args;
            }
        );
        list(
            "inputs",
            [&]() -> Strings&
            {
                return targetSetting().m_inputs;
            }
        );
        list(
            "outputs",
            [&]() -> Strings&
            {
                return targetSetting().m_outputs;
            }
        );
        scalar(
            "depfile",
            [&](const std::string& s)
            {
                targetSetting().m_depfile = s;
            }
        );
        list(
            "packages",
            [&]() -> Strings&
            {
                return targetSetting().m_packages;
            }
        );
        list(
            "public_packages",
            [&]() -> Strings&
            {
                return targetSetting().m_publicPackages;
            }
        );
        auto hook = [&](bool before, const std::string& name, sol::table options)
        {
            if (!enabled)
                return;
            auto& t = targetSetting();
            BuildHook action;
            action.m_name = name;
            for (auto [key, value] : options)
            {
                (void)value;
                const auto field = readText(key);
                if (field != "tool" && field != "args" && field != "inputs" && field != "outputs" && field != "depfile")
                    throw std::runtime_error("unknown build action field: " + field);
            }
            sol::object tool = options["tool"], args = options["args"], inputs = options["inputs"], outputs = options["outputs"], depfile = options["depfile"];
            if (tool.valid() && tool.get_type() != sol::type::nil)
                action.m_tool = readText(tool);
            if (args.valid() && args.get_type() != sol::type::nil)
                action.m_args = readList(args);
            if (inputs.valid() && inputs.get_type() != sol::type::nil)
                action.m_inputs = readList(inputs);
            if (outputs.valid() && outputs.get_type() != sol::type::nil)
                action.m_outputs = readList(outputs);
            if (depfile.valid() && depfile.get_type() != sol::type::nil)
                action.m_depfile = readText(depfile);
            if (name.empty() || action.m_tool.empty() || action.m_outputs.empty())
                throw std::runtime_error("pre/post-build actions require name, tool and declared outputs");
            (before ? t.m_preBuild : t.m_postBuild).push_back(std::move(action));
        };
        lua.set_function(
            "prebuild",
            [hook](const std::string& name, sol::table options)
            {
                hook(true, name, options);
            }
        );
        lua.set_function(
            "postbuild",
            [hook](const std::string& name, sol::table options)
            {
                hook(false, name, options);
            }
        );
        auto require = [&](const std::string& name, sol::table options, bool local)
        {
            if (!enabled)
                return;
            if (!workspaceSeen)
                throw std::runtime_error("workspace(...) must precede package requirements");
            if (options["version"].valid())
            {
                const auto version = readText(options["version"]);
                if (options["ref"].valid() && readText(options["ref"]) != version)
                    throw std::runtime_error("package version and ref disagree: " + name);
                options["ref"] = version;
            }
            if (!local && !options["source"].valid() && name.find("://") == std::string::npos && !name.starts_with("git@"))
                throw std::runtime_error("package requires an explicit source; Rhodium has no package registry: " + name);
            metadata.rootPackage.dependencies.push_back(rhodium::LuaDependencyReader(targetPlatform, ws.m_root).read(name, options, local));
        };
        lua.set_function(
            "require",
            [&](const std::string& name, sol::table options)
            {
                require(name, options, false);
            }
        );
        lua.set_function(
            "require_local",
            [&](const std::string& name, sol::table options)
            {
                require(name, options, true);
            }
        );
        lua.set_function(
            "dependency_overrides",
            [&](sol::table values)
            {
                if (enabled)
                    rhodium::LuaDependencyReader::overrides(values, metadata, diagnostics);
            }
        );
        auto rhodium = lua.create_named_table("rhodium");
        rhodium["require"] = lua["require"];
        rhodium["require_local"] = lua["require_local"];
        rhodium["dependency_overrides"] = lua["dependency_overrides"];
        const auto result = lua.safe_script_file(ws.m_manifest.string(), sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error error = result;
            throw std::runtime_error(error.what());
        }
        if (diagnostics.m_failed)
            throw std::runtime_error("Lua manifest contains Rhodium DSL errors");
        if (config.m_name != "__inspect" && !ws.m_configurations.empty() &&
            std::find(ws.m_configurations.begin(), ws.m_configurations.end(), config) == ws.m_configurations.end())
            throw std::runtime_error("configuration " + BuildModel::toString(config) + " is not declared by the workspace");
        if (!rhodium::ModelValidator::validate(metadata, diagnostics))
            throw std::runtime_error("invalid Rhodium package declarations");
        if (config.m_name == "__inspect")
            return ws;
        auto expand = [&](Strings& sources, const Strings& excluded, bool managed, bool headerOnly)
        {
            sources = expandFiles(ws.m_root, sources, buildDirectory);
            for (const auto& pattern : excluded)
            {
                const auto matcher = glob(fs::absolute(ws.m_root / pattern).lexically_normal().generic_string());
                std::erase_if(
                    sources,
                    [&](const std::string& source)
                    {
                        return std::regex_match(fs::absolute(ws.m_root / source).lexically_normal().generic_string(), matcher);
                    }
                );
            }
            std::erase_if(
                sources,
                [&](const std::string& source)
                {
                    const auto ext = toLower(fs::path(source).extension().string());
                    if (managed)
                        return ext != ".cs";
                    if (headerOnly)
                        return true;
                    if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".inl")
                        return true;
                    if (ext != ".c" && ext != ".cc" && ext != ".cpp" && ext != ".cxx" && ext != ".m" && ext != ".mm" && ext != ".rc")
                        throw std::runtime_error("unsupported source file: " + source);
                    return false;
                }
            );
        };
        for (auto& t : ws.m_targets)
        {
            if (!BuildModel::targetUnsupportedReason(t, config, architecture, targetPlatform).empty())
                continue;
            t.m_install.m_headers = expandFiles(ws.m_root, t.m_install.m_headers, buildDirectory);
            t.m_install.m_files = expandFiles(ws.m_root, t.m_install.m_files, buildDirectory);
            t.m_ideFiles = expandFiles(ws.m_root, t.m_sources, buildDirectory);
            expand(t.m_sources, t.m_excludedSources, t.m_kind == TargetKind::DotNetProject, t.m_kind == TargetKind::HeaderOnly);
            if ((!t.m_language.empty() && t.m_language != "C#" && t.m_kind == TargetKind::DotNetProject) ||
                (t.m_language == "C#" && t.m_kind != TargetKind::DotNetProject))
                throw std::runtime_error("language does not match target kind: " + t.m_name);
            if (t.m_kind != TargetKind::DotNetProject &&
                (!t.m_framework.empty() || (!t.m_project.empty() && !BuildModel::isNativeProject(t)) || !t.m_references.empty()))
                throw std::runtime_error("managed settings require a C# target: " + t.m_name);
            const bool native = t.m_kind == TargetKind::Executable || t.m_kind == TargetKind::StaticLibrary || t.m_kind == TargetKind::SharedLibrary;
            if (!native &&
                (!t.m_outputName.empty() || !t.m_outputDirectory.empty() || !t.m_objectDirectory.empty() || !t.m_importLibraryDirectory.empty() ||
                 !t.m_link.m_subsystem.empty() || !t.m_link.m_entryPoint.empty() || !t.m_link.m_moduleDefinition.empty() || !t.m_link.m_manifest.empty() ||
                 !t.m_link.m_delayLoadLibraries.empty() || !t.m_link.m_exports.empty() || !t.m_link.m_importLibrary || !t.m_icon.empty() ||
                 t.m_link.m_incremental.has_value() || !t.m_resourceInputs.empty() || t.m_linkType != "monolithic"))
                throw std::runtime_error("native output/link settings require a native compile target: " + t.m_name);
            if ((t.m_kind == TargetKind::DotNetProject || t.m_kind == TargetKind::Codegen) && (!t.m_packages.empty() || !t.m_publicPackages.empty()))
                throw std::runtime_error("packages require a native compile target: " + t.m_name);
            if (t.m_kind == TargetKind::DotNetProject && !t.m_project.empty() && (!t.m_framework.empty() || !t.m_sources.empty() || !t.m_references.empty()))
                throw std::runtime_error("existing project controls its own framework, files and references: " + t.m_name);
            std::set<std::string> packageNames;
            for (const auto* group : {&t.m_packages, &t.m_publicPackages})
                for (const auto& name : *group)
                {
                    if (!packageNames.insert(name).second)
                        throw std::runtime_error("package listed more than once/across scopes: " + name);
                    if (std::none_of(
                            metadata.rootPackage.dependencies.begin(),
                            metadata.rootPackage.dependencies.end(),
                            [&](const auto& dep)
                            {
                                return dep.name == name;
                            }
                        ))
                        throw std::runtime_error("undeclared package: " + name);
                }
            for (const auto& ref : t.m_references)
            {
                const auto found = std::find_if(
                    ws.m_targets.begin(),
                    ws.m_targets.end(),
                    [&](const Target& other)
                    {
                        return other.m_name == ref;
                    }
                );
                if (found != ws.m_targets.end())
                {
                    if (found->m_kind != TargetKind::DotNetProject)
                        throw std::runtime_error("managed reference must name a C# target: " + ref);
                    if (std::find(t.m_privateDependencies.begin(), t.m_privateDependencies.end(), ref) == t.m_privateDependencies.end())
                        t.m_privateDependencies.push_back(ref);
                }
            }
        }
        for (auto& m : ws.m_modules)
        {
            const auto owner = std::find_if(
                ws.m_targets.begin(),
                ws.m_targets.end(),
                [&](const auto& t)
                {
                    return t.m_name == m.m_targetName;
                }
            );
            if (owner != ws.m_targets.end() && !BuildModel::targetUnsupportedReason(*owner, config, architecture, targetPlatform).empty())
                continue;
            m.m_ideFiles = expandFiles(ws.m_root, m.m_sources, buildDirectory);
            expand(m.m_sources, m.m_excludedSources, false, false);
        }
        if (ws.m_references.empty())
        {
            auto active = ws;
            BuildModel::selectTargets(active, config, architecture, targetPlatform);
            BuildModel::validateWorkspace(active);
        }
        return ws;
    }

} // namespace axlum
