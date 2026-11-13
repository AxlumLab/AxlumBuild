/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "manifest/Manifest.hpp"
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace axlum
{
    namespace
    {
        using Strings = std::vector<std::string>;

        struct Value
        {
            bool m_array = false;
            std::string m_single;
            Strings m_list;
        };

        std::string trim(std::string s)
        {
            const auto first = s.find_first_not_of(" \r\t\n");
            if (first == std::string::npos)
                return {};
            const auto last = s.find_last_not_of(" \r\t\n");
            return s.substr(first, last - first + 1);
        }

        std::string removeComment(const std::string& s)
        {
            bool quoted = false, literal = false, escape = false;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                char c = s[i];
                if (escape)
                {
                    escape = false;
                    continue;
                }
                if (quoted && !literal && c == '\\')
                {
                    escape = true;
                    continue;
                }
                if (!quoted && (c == '"' || c == '\''))
                {
                    quoted = true;
                    literal = c == '\'';
                    continue;
                }
                if (quoted && c == (literal ? '\'' : '"'))
                {
                    quoted = false;
                    continue;
                }
                if (!quoted && c == '#')
                    return s.substr(0, i);
            }
            return s;
        }

        std::string parseQuoted(std::string_view s, std::size_t& pos)
        {
            if (pos >= s.size() || (s[pos] != '"' && s[pos] != '\''))
                throw std::runtime_error("expected quoted string");
            char delimiter = s[pos++];
            std::string result;
            while (pos < s.size())
            {
                char c = s[pos++];
                if (c == delimiter)
                    return result;
                if (c == '\\' && delimiter == '"')
                {
                    if (pos == s.size())
                        throw std::runtime_error("trailing backslash in string");
                    switch (char e = s[pos++])
                    {
                    case '"':
                        result += '"';
                        break;
                    case '\\':
                        result += '\\';
                        break;
                    case 'n':
                        result += '\n';
                        break;
                    case 'r':
                        result += '\r';
                        break;
                    case 't':
                        result += '\t';
                        break;
                    default:
                        throw std::runtime_error(std::string("unsupported TOML escape \\") + e + " (use single quotes for Windows paths)");
                    }
                }
                else
                    result += c;
            }
            throw std::runtime_error("unterminated quoted string");
        }

        void spaces(std::string_view s, std::size_t& pos)
        {
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                ++pos;
        }

        Value parseValue(const std::string& s)
        {
            std::size_t pos = 0;
            spaces(s, pos);
            Value value;
            if (pos < s.size() && s[pos] == '[')
            {
                value.m_array = true;
                ++pos;
                for (;;)
                {
                    spaces(s, pos);
                    if (pos >= s.size())
                        throw std::runtime_error("unterminated array; expected ']'");
                    if (s[pos] == ']')
                    {
                        ++pos;
                        break;
                    }
                    value.m_list.push_back(parseQuoted(s, pos));
                    spaces(s, pos);
                    if (pos >= s.size())
                        throw std::runtime_error("unterminated array");
                    if (s[pos] == ']')
                    {
                        ++pos;
                        break;
                    }
                    if (s[pos] != ',')
                        throw std::runtime_error("expected ',' or ']' in array");
                    ++pos;
                }
            }
            else
                value.m_single = parseQuoted(s, pos);
            spaces(s, pos);
            if (pos != s.size())
                throw std::runtime_error("unexpected characters after value");
            return value;
        }

        std::string asString(const Value& val, const std::string& key)
        {
            if (val.m_array)
                throw std::runtime_error("field '" + key + "' must be a string");
            return val.m_single;
        }

        Strings asStrings(const Value& val, const std::string& key)
        {
            if (!val.m_array)
                throw std::runtime_error("field '" + key + "' must be an array of strings");
            return val.m_list;
        }

        bool setCompile(CompileOptions& o, const std::string& key, const Value& v)
        {
            if (key == "public_includes")
                o.m_publicIncludes = asStrings(v, key);
            else if (key == "private_includes")
                o.m_privateIncludes = asStrings(v, key);
            else if (key == "public_defines")
                o.m_publicDefines = asStrings(v, key);
            else if (key == "private_defines")
                o.m_privateDefines = asStrings(v, key);
            else if (key == "c_flags")
                o.m_cFlags = asStrings(v, key);
            else if (key == "cxx_flags")
                o.m_cxxFlags = asStrings(v, key);
            else if (key == "cpp_standard")
                o.m_cppStandard = asString(v, key);
            else if (key == "c_standard")
                o.m_cStandard = asString(v, key);
            else
                return false;
            return true;
        }

        void setTarget(Target& t, const std::string& key, const Value& v)
        {
            if (setCompile(t.m_compile, key, v))
                return;
            if (key == "name")
                t.m_name = asString(v, key);
            else if (key == "kind")
                t.m_kind = BuildModel::parseTargetKind(asString(v, key));
            else if (key == "sources")
                t.m_sources = asStrings(v, key);
            else if (key == "public_dependencies")
                t.m_publicDependencies = asStrings(v, key);
            else if (key == "private_dependencies")
                t.m_privateDependencies = asStrings(v, key);
            else if (key == "interface_dependencies")
                t.m_interfaceDependencies = asStrings(v, key);
            else if (key == "libraries")
                t.m_link.m_libraries = asStrings(v, key);
            else if (key == "library_directories")
                t.m_link.m_libraryDirectories = asStrings(v, key);
            else if (key == "link_flags")
                t.m_link.m_linkFlags = asStrings(v, key);
            else if (key == "project")
                t.m_project = asString(v, key);
            else if (key == "tool")
                t.m_tool = asString(v, key);
            else if (key == "args")
                t.m_args = asStrings(v, key);
            else if (key == "inputs")
                t.m_inputs = asStrings(v, key);
            else if (key == "outputs")
                t.m_outputs = asStrings(v, key);
            else if (key == "depfile")
                t.m_depfile = asString(v, key);
            else
                throw std::runtime_error("unknown [[target]] field '" + key + "'");
        }

        void setModule(Module& m, const std::string& key, const Value& v)
        {
            if (setCompile(m.m_compile, key, v))
                return;
            if (key == "name")
                m.m_name = asString(v, key);
            else if (key == "target")
                m.m_targetName = asString(v, key);
            else if (key == "sources")
                m.m_sources = asStrings(v, key);
            else
                throw std::runtime_error("unknown [[module]] field '" + key + "'");
        }
    } // namespace

    Workspace ManifestLoader::loadSingleManifest(
        const fs::path& filename,
        Configuration config,
        const fs::path& buildDirectory,
        const std::string& architecture,
        const std::string& platform
    )
    {
        if (filename.extension() == ".lua")
            return ManifestLoader::loadLuaManifest(filename, config, buildDirectory, architecture, platform);
        Workspace ws;
        ws.m_manifest = fs::absolute(filename).lexically_normal();
        ws.m_root = ws.m_manifest.parent_path();
        std::ifstream f(ws.m_manifest, std::ios::binary);
        if (!f)
            throw std::runtime_error("manifest not found: " + ws.m_manifest.string());
        enum class Section
        {
            None,
            Workspace,
            Target,
            Module
        } section = Section::None;
        std::unordered_set<std::string> seenKeys;
        std::string line;
        int lineNo = 0, sectionLine = 0;
        bool workspaceSeen = false;
        bool targetHasKind = false;
        auto checkTargetSection = [&]
        {
            if (section == Section::Target && (!seenKeys.contains("name") || !targetHasKind))
                throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(sectionLine) + ": [[target]] requires name and kind");
            if (section == Section::Module && (!seenKeys.contains("name") || !seenKeys.contains("target")))
                throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(sectionLine) + ": [[module]] requires name and target");
        };
        while (std::getline(f, line))
        {
            ++lineNo;
            if (lineNo == 1 && line.size() >= 3 && line.substr(0, 3) == "\xef\xbb\xbf")
                line.erase(0, 3);
            std::string s = trim(removeComment(line));
            if (s.empty())
                continue;
            if (s[0] == '[')
            {
                checkTargetSection();
                if (s == "[workspace]")
                {
                    if (workspaceSeen)
                        throw std::runtime_error("duplicate [workspace] section at line " + std::to_string(lineNo));
                    workspaceSeen = true;
                    section = Section::Workspace;
                }
                else if (s == "[[target]]")
                {
                    ws.m_targets.emplace_back();
                    section = Section::Target;
                    ws.m_targets.back().m_declarationLine = lineNo;
                }
                else if (s == "[[module]]")
                {
                    ws.m_modules.emplace_back();
                    section = Section::Module;
                    ws.m_modules.back().m_declarationLine = lineNo;
                    ws.m_modules.back().m_compile.m_cppStandard.clear();
                    ws.m_modules.back().m_compile.m_cStandard.clear();
                }
                else
                    throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(lineNo) + ": unsupported section '" + s + "'");
                seenKeys.clear();
                targetHasKind = false;
                sectionLine = lineNo;
                continue;
            }
            if (section == Section::None)
                throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(lineNo) + ": expected [workspace] or [[target]] first");
            std::size_t eq = s.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(lineNo) + ": expected 'key = value'");
            std::string key = trim(s.substr(0, eq));
            for (unsigned char c : key)
                if (!std::isalnum(c) && c != '_')
                    throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(lineNo) + ": invalid field name '" + key + "'");
            if (!seenKeys.insert(key).second)
                throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(lineNo) + ": duplicate field '" + key + "'");
            if (section == Section::Target && key == "kind")
                targetHasKind = true;
            try
            {
                Value value = parseValue(trim(s.substr(eq + 1)));
                if (section == Section::Workspace)
                {
                    if (key != "name")
                        throw std::runtime_error("unknown [workspace] field '" + key + "'");
                    ws.m_name = asString(value, key);
                }
                else if (section == Section::Target)
                    setTarget(ws.m_targets.back(), key, value);
                else
                    setModule(ws.m_modules.back(), key, value);
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error(ws.m_manifest.string() + ":" + std::to_string(lineNo) + ": " + e.what());
            }
        }
        checkTargetSection();
        try
        {
            BuildModel::validateWorkspace(ws);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(ws.m_manifest.string() + ": " + e.what());
        }
        return ws;
    }
} // namespace axlum
