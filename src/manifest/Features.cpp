/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */
#include "manifest/Features.hpp"
#include "core/Process.hpp"
#include "graph/NativePaths.hpp"
#include "manifest/Manifest.hpp"
#include "packages/PackageManifest.hpp"
#include "packages/Rhodium.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace axlum
{
    using Json = nlohmann::json;

    namespace
    {
        std::vector<std::string> strings(const Json& j)
        {
            if (j.is_null())
                return {};
            if (j.is_string())
                return {j.get<std::string>()};
            if (!j.is_array())
                throw std::runtime_error("expected a string or string array");
            return j.get<std::vector<std::string>>();
        }

        std::string text(const Json& j)
        {
            return j.is_string() ? j.get<std::string>() : j.is_boolean() ? (j.get<bool>() ? "1" : "0") : j.dump();
        }

        void unique(std::vector<std::string>& list, const std::string& s)
        {
            if (std::find(list.begin(), list.end(), s) == list.end())
                list.push_back(s);
        }

        fs::path outputPath(const fs::path& build, std::string s)
        {
            if (s.starts_with("@build/"))
                s.erase(0, 7);
            auto p = fs::absolute(build / s).lexically_normal();
            if (!FileSystem::isWithin(fs::weakly_canonical(p), fs::weakly_canonical(build)) || p == fs::absolute(build))
                throw std::runtime_error("output escapes build directory: " + s);
            return p;
        }

        std::string xml(std::string s)
        {
            std::string r;
            for (char c : s)
                r += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '\"' ? "&quot;" : std::string(1, c);
            return r;
        }

        std::regex globRegex(std::string s)
        {
            std::string r = "^";
            for (size_t i = 0; i < s.size(); ++i)
            {
                char c = s[i];
                if (c == '*' && i + 1 < s.size() && s[i + 1] == '*')
                {
                    ++i;
                    if (i + 1 < s.size() && s[i + 1] == '/')
                    {
                        ++i;
                        r += "(?:.*/)?";
                    }
                    else
                        r += ".*";
                }
                else if (c == '*')
                    r += "[^/]*";
                else if (c == '?')
                    r += "[^/]";
                else
                {
                    if (std::string(".\\+()[]{}^$|").find(c) != std::string::npos)
                        r += '\\';
                    r += c == '\\' ? '/' : c;
                }
            }
            return std::regex(r + "$");
        }

        std::vector<fs::path> scan(const fs::path& root, const std::vector<std::string>& patterns, const fs::path& build)
        {
            std::vector<fs::path> result;
            if (!fs::is_directory(root))
                throw std::runtime_error("input set root does not exist: " + root.string());
            for (fs::recursive_directory_iterator it(root), end; it != end; ++it)
            {
                if (it->is_directory())
                {
                    auto n = it->path().filename().string();
                    if (n == ".git" || n == ".tpkg" || n == ".rhodium" || n == ".axlum" || FileSystem::isWithin(it->path(), build))
                        it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file())
                    continue;
                auto rel = it->path().lexically_relative(root).generic_string();
                if (std::any_of(
                        patterns.begin(),
                        patterns.end(),
                        [&](const auto& p)
                        {
                            return std::regex_match(rel, globRegex(p));
                        }
                    ))
                    result.push_back(fs::absolute(it->path()).lexically_normal());
            }
            std::sort(result.begin(), result.end());
            return result;
        }

        Json array(const Json& object, const char* key)
        {
            return object.contains(key) ? object.at(key) : Json::array();
        }
    } // namespace

    std::string DslFeatures::luaPrelude()
    {
        return R"lua(
function option(name, spec)
    spec = spec or {}; spec.name = name
    return __axlum_feature("option", spec)
end
function targetfile(name) return "${artifact:" .. name:gsub("::", "__") .. "}" end
function targetoutputdir(name) return "${artifactdir:" .. name:gsub("::", "__") .. "}" end
function generatedfile(name, path) return "${generated:" .. name:gsub("::", "__") .. ":" .. path .. "}" end
function configure_file(input, output, values)
    return __axlum_feature("template", {input=input, output=output, values=values or {}})
end
function configure_header(output, values)
    return __axlum_feature("template", {output=output, values=values or {}, header=true})
end
function test(name, spec) spec=spec or {}; spec.name=name; __axlum_feature("test", spec) end
function gtest_discover_tests(target, spec)
    spec=spec or {}; spec.target=target; spec.discovery="gtest"
    test(spec.name or target, spec)
end
list = {}
function list.concat(...)
    local result={}
    for _,values in ipairs({...}) do
        if type(values)=="string" then values={values} end
        for _,value in ipairs(values) do result[#result+1]=value end
    end
    return result
end
function list.unique(values)
    local result,seen={},{}
    for _,value in ipairs(values) do
        if not seen[value] then seen[value]=true; result[#result+1]=value end
    end
    return result
end
function list.prefix(prefix, values)
    local result={}
    for _,value in ipairs(values) do result[#result+1]=prefix..value end
    return result
end
local settingOrder={"kind","language","project","cppdialect","cdialect","files","removefiles",
    "includedirs","public_includedirs","defines","public_defines","forceincludes","characterset",
    "public_deps","private_deps","interface_deps","packages","public_packages","links",
    "libdirs","cflags","cxxflags","linkoptions","optimize","symbols","tool","args","inputs","outputs","workingdir","environment"}
local settingFunctions={}
for _,key in ipairs(settingOrder) do settingFunctions[key]=true end
function settings(spec)
    assert(type(spec)=="table", "settings requires a table")
    for key,_ in pairs(spec) do assert(settingFunctions[key] and type(_G[key])=="function", "unknown declarative setting: "..tostring(key)) end
    for _,key in ipairs(settingOrder) do if spec[key]~=nil then _G[key](spec[key]) end end
end
local declareTarget=target
function target(name, spec)
    declareTarget(name)
    if spec then settings(spec) end
end
local conditional=when
function when(condition, body)
    if type(body)=="table" then return conditional(condition, function() settings(body) end) end
    return conditional(condition, body)
end
function build_directory(path) __axlum_feature("build_directory",path) end
function hosttool(value) __axlum_feature("hosttool", value) end
function host_program(name, spec)
    spec=spec or {}; spec.name=name
    local result=__axlum_host_program(spec)
    return result
end
function forceincludes(value) __axlum_feature("forceincludes", value) end
function characterset(value) __axlum_feature("charset", value) end
function workingdir(value) __axlum_feature("workingdir", value) end
function environment(value) __axlum_feature("environment", value) end
function byproducts(value) __axlum_feature("byproducts", value) end
function inputset(root, patterns) __axlum_feature("inputset", {root=root, patterns=patterns or {"**"}}) end
function outputmanifest(path) __axlum_feature("outputmanifest", path) end
function generatedsources(producer, spec)
    spec=spec or {}; spec.producer=producer; __axlum_feature("generatedsources",spec)
end
function deployfiles(root, spec)
    spec=spec or {}; spec.root=root; __axlum_feature("deployfiles",spec)
end
function managedsources(spec) __axlum_feature("managedsources",spec) end
function rhodium.system(name, spec)
    spec=spec or {}; spec.name=name; __axlum_feature("system",spec)
end
function rhodium.tool(name, spec)
    local recipe={}
    for k,v in pairs(spec) do if k~="executable" then recipe[k]=v end end
    recipe.build=recipe.build or "prebuilt"
    rhodium.require(name,recipe)
    __axlum_feature("package_tool",{name=name, executable=spec.executable})
    return "${package_tool:" .. name .. "}"
end
)lua";
    }

    Json DslFeatures::readOptions(const fs::path& build)
    {
        return !build.empty() && fs::is_regular_file(build / "axlum.options.json") ? Json::parse(FileSystem::readAll(build / "axlum.options.json"))
                                                                                   : Json::object();
    }

    Json DslFeatures::option(Workspace& ws, const Json& spec, const Json& values)
    {
        auto name = spec.at("name").get<std::string>();
        if (!std::regex_match(name, std::regex("[A-Za-z_][A-Za-z0-9_.-]*")))
            throw std::runtime_error("invalid option name: " + name);
        if (ws.m_features["options"].contains(name))
            throw std::runtime_error("duplicate option: " + name);
        auto type = spec.value("type", std::string("bool"));
        auto value = values.contains(name) ? values.at(name) : spec.value("default", type == "bool" ? Json(false) : Json(""));
        if (type == "bool")
        {
            if (value.is_string())
            {
                auto v = value.get<std::string>();
                if (v == "true" || v == "on" || v == "1")
                    value = true;
                else if (v == "false" || v == "off" || v == "0")
                    value = false;
            }
            if (!value.is_boolean())
                throw std::runtime_error("option " + name + " requires bool");
        }
        else
        {
            if (type != "string" && type != "enum" && type != "path")
                throw std::runtime_error("unknown option type: " + type);
            if (!value.is_string())
                throw std::runtime_error("option " + name + " requires string");
            if (type == "enum")
            {
                auto choices = strings(spec.at("values"));
                if (std::find(choices.begin(), choices.end(), value.get<std::string>()) == choices.end())
                    throw std::runtime_error("invalid value for option: " + name);
            }
        }
        ws.m_features["options"][name] = spec;
        ws.m_features["options"][name]["value"] = value;
        return value;
    }

    bool DslFeatures::systemPackage(const Workspace& ws, const std::string& name)
    {
        for (const auto& s : array(ws.m_features, "system"))
            if (s.at("name") == name)
                return true;
        return false;
    }

    void DslFeatures::finalize(Workspace& ws, const fs::path& build, Configuration config)
    {
        // References add dependencies independently of declaration order.
        for (auto& t : ws.m_targets)
        {
            if (t.m_hostTool && t.m_kind != TargetKind::Executable)
                throw std::runtime_error("hosttool requires a native executable target: " + t.m_name);
            auto refs = [&](const std::string& s)
            {
                std::regex pattern(R"(\$\{(?:artifact|artifactdir|generated):([^:}]+)(?::[^}]+)?\})");
                for (std::sregex_iterator i(s.begin(), s.end(), pattern), end; i != end; ++i)
                {
                    auto name = (*i)[1].str();
                    if (name != t.m_name && std::find(t.m_publicDependencies.begin(), t.m_publicDependencies.end(), name) == t.m_publicDependencies.end() &&
                        std::find(t.m_interfaceDependencies.begin(), t.m_interfaceDependencies.end(), name) == t.m_interfaceDependencies.end())
                        unique(t.m_privateDependencies, name);
                }
            };
            if (t.m_tool.starts_with("@target/"))
                refs("${artifact:" + t.m_tool.substr(8) + "}");
            for (const auto& arg : t.m_args)
                refs(arg);
            for (const auto& arg : t.m_inputs)
                refs(arg);
            for (const auto& spec : array(t.m_features, "generatedsources"))
                unique(t.m_privateDependencies, spec.at("producer"));
            for (const auto& spec : array(t.m_features, "byproducts"))
                for (const auto& f : strings(spec))
                    unique(t.m_outputs, f);
            for (const auto& m : array(t.m_features, "outputmanifest"))
            {
                if (t.m_kind != TargetKind::Codegen)
                    throw std::runtime_error("outputmanifest requires Codegen");
                auto file = outputPath(build, m.get<std::string>());
                unique(t.m_outputs, file.lexically_relative(fs::absolute(build)).generic_string());
                if (fs::is_regular_file(file))
                {
                    auto manifest = Json::parse(FileSystem::readAll(file));
                    for (const auto& f : strings(manifest))
                    {
                        auto out = outputPath(file.parent_path(), f);
                        unique(t.m_outputs, out.lexically_relative(fs::absolute(build)).generic_string());
                        t.m_features["dynamic_outputs"].push_back(out.lexically_relative(fs::absolute(build)).generic_string());
                    }
                }
            }
            for (const auto& spec : array(t.m_features, "inputset"))
            {
                auto root = ws.m_root / spec.at("root").get<std::string>();
                auto matches = scan(root, strings(spec.at("patterns")), build);
                for (const auto& f : matches)
                    unique(t.m_inputs, f.generic_string());
                Json members = Json::array();
                for (auto& f : matches)
                    members.push_back(f.generic_string());
                auto membership = fs::absolute(build / "sets" / (FileSystem::shortHash(t.m_name + root.generic_string() + spec.dump()) + ".json"));
                if (config.m_name != "__inspect")
                    FileSystem::writeIfChanged(membership, members.dump());
                unique(t.m_inputs, membership.generic_string());
            }
            // System interfaces do not pretend a workspace directory is a package source.
            for (const auto& s : array(ws.m_features, "system"))
            {
                const auto name = s.at("name").get<std::string>();
                bool pub = std::find(t.m_publicPackages.begin(), t.m_publicPackages.end(), name) != t.m_publicPackages.end();
                bool priv = std::find(t.m_packages.begin(), t.m_packages.end(), name) != t.m_packages.end();
                if (!pub && !priv)
                    continue;
                std::erase(t.m_packages, name);
                std::erase(t.m_publicPackages, name);
                Target iface;
                iface.m_name = "_system_" + name;
                iface.m_kind = TargetKind::HeaderOnly;
                auto existing = std::find_if(
                    ws.m_targets.begin(),
                    ws.m_targets.end(),
                    [&](auto& v)
                    {
                        return v.m_name == iface.m_name;
                    }
                );
                // Materialized below, after the target iteration.
                unique(pub ? t.m_publicDependencies : t.m_privateDependencies, iface.m_name);
            }
        }
        for (const auto& s : array(ws.m_features, "system"))
        {
            Target t;
            t.m_name = "_system_" + s.at("name").get<std::string>();
            t.m_kind = TargetKind::HeaderOnly;
            t.m_features["system_interface"] = true;
            t.m_compile.m_publicIncludes = strings(s.value("includedirs", Json::array()));
            t.m_compile.m_publicDefines = strings(s.value("defines", Json::array()));
            t.m_link.m_libraries = strings(s.value("links", Json::array()));
            t.m_link.m_frameworks = strings(s.value("frameworks", Json::array()));
            ws.m_targets.push_back(t);
        }
        for (const auto& spec : array(ws.m_features, "template"))
        {
            Target t;
            t.m_name = "_template_" + FileSystem::shortHash(spec.at("output").get<std::string>());
            t.m_kind = TargetKind::Codegen;
            t.m_tool = "@axlum";
            t.m_features["builtin"] = spec;
            const auto data = spec.dump();
            std::regex references(R"(\$\{(?:artifact|artifactdir):([^}]+)\})");
            for (std::sregex_iterator i(data.begin(), data.end(), references), end; i != end; ++i)
                unique(t.m_privateDependencies, (*i)[1]);
            t.m_outputs = {spec.at("output").get<std::string>()};
            if (spec.contains("input"))
                t.m_inputs = {spec.at("input").get<std::string>()};
            ws.m_targets.push_back(t);
        }
        for (auto& t : ws.m_targets)
            for (const auto& spec : array(t.m_features, "generatedsources"))
            {
                auto name = spec.at("producer").get<std::string>();
                auto it = std::find_if(
                    ws.m_targets.begin(),
                    ws.m_targets.end(),
                    [&](auto& v)
                    {
                        return v.m_name == name;
                    }
                );
                if (it == ws.m_targets.end() || !it->m_features.contains("outputmanifest"))
                    throw std::runtime_error("generatedsources requires an outputmanifest producer: " + name);
                for (const auto& f : it->m_outputs)
                {
                    auto ext = fs::path(f).extension().string();
                    auto allowed = strings(spec.value("extensions", Json::array({".cpp", ".c", ".cc", ".cxx", ".cs"})));
                    if (std::find(allowed.begin(), allowed.end(), ext) != allowed.end())
                        unique(t.m_sources, "@build/" + f);
                }
            }
    }

    void DslFeatures::selectClosure(Workspace& ws, const std::vector<std::string>& roots)
    {
        std::set<std::string> keep;
        std::function<void(const std::string&)> visit = [&](const auto& name)
        {
            if (!keep.insert(name).second)
                return;
            auto it = std::find_if(
                ws.m_targets.begin(),
                ws.m_targets.end(),
                [&](auto& t)
                {
                    return t.m_name == name;
                }
            );
            if (it == ws.m_targets.end())
                throw std::runtime_error("unknown tool/generator dependency: " + name);
            for (auto* list : {&it->m_publicDependencies, &it->m_privateDependencies, &it->m_interfaceDependencies})
                for (auto& d : *list)
                    visit(d);
        };
        for (auto& r : roots)
            visit(r);
        std::erase_if(
            ws.m_targets,
            [&](auto& t)
            {
                return !keep.contains(t.m_name);
            }
        );
        std::erase_if(
            ws.m_modules,
            [&](auto& m)
            {
                return !keep.contains(m.m_targetName);
            }
        );
        ws.m_features.erase("test");
        ws.m_targetAliases.clear();
    }

    void DslFeatures::resolveToolPackages(Workspace& ws, const fs::path& build, Configuration config, bool locked)
    {
        if (!ws.m_features.contains("package_tool"))
            return;
        Json active = Json::array();
        std::set<std::string> declared;
        for (const auto& spec : ws.m_features["package_tool"])
        {
            const auto name = spec.at("name").get<std::string>();
            declared.insert(name);
            const auto token = "${package_tool:" + name + "}";
            bool used = false;
            for (const auto& t : ws.m_targets)
            {
                used |= t.m_tool.find(token) != std::string::npos;
                for (const auto& arg : t.m_args)
                    used |= arg.find(token) != std::string::npos;
            }
            if (used)
                active.push_back(spec);
        }
        if (active.empty())
        {
            std::erase_if(
                ws.m_packageManifest->m_model.rootPackage.dependencies,
                [&](const auto& d)
                {
                    return declared.contains(d.name);
                }
            );
            return;
        }
        const auto hostPlatform = ToolchainDiscovery::hostTriple().find("windows") != std::string::npos ? "windows"
                                  : ToolchainDiscovery::hostTriple().find("apple") != std::string::npos ? "macos"
                                                                                                        : "linux";
        const auto hostArch =
            ToolchainDiscovery::hostTriple().find("aarch64") != std::string::npos || ToolchainDiscovery::hostTriple().find("arm64") != std::string::npos
                ? "arm64"
                : "x64";
        auto hostBuild = build / "tools";
        FileSystem::writeIfChanged(hostBuild / "axlum.options.json", readOptions(build).dump(2));
        auto toolWs = ManifestLoader::loadManifest(ws.m_manifest, config, hostBuild, hostArch, hostPlatform);
        toolWs.m_features["package_tool"] = active;
        std::set<std::string> keep, roots;
        auto& recipes = toolWs.m_packageManifest->m_model.rootPackage.dependencies;
        std::function<void(const std::string&)> visit = [&](const auto& name)
        {
            if (!keep.insert(name).second)
                return;
            auto it = std::find_if(
                recipes.begin(),
                recipes.end(),
                [&](const auto& d)
                {
                    return d.name == name;
                }
            );
            if (it == recipes.end())
                throw std::runtime_error("undeclared host tool package: " + name);
            for (const auto& dep : it->dependencies)
                visit(dep);
        };
        for (const auto& spec : active)
        {
            roots.insert(spec.at("name"));
            visit(spec.at("name"));
        }
        std::erase_if(
            recipes,
            [&](const auto& d)
            {
                return !keep.contains(d.name);
            }
        );
        Target dummy;
        dummy.m_name = "HostTools";
        dummy.m_kind = TargetKind::HeaderOnly;
        toolWs.m_targets = {dummy};
        toolWs.m_modules.clear();
        toolWs.m_references.clear();
        auto tc = ToolchainDiscovery::discoverToolchain("auto", ws.m_root, {}, hostArch, hostPlatform);
        auto fingerprint = PackageManager::packageInputsFingerprint(toolWs, tc, config, hostBuild);
        auto state = hostBuild / "packages.state";
        auto restore = !fs::is_regular_file(state) || FileSystem::readAll(state) != fingerprint;
        PackageManager::resolvePackages(toolWs, tc, config, locked, restore, hostBuild / "packages");
        FileSystem::writeIfChanged(state, PackageManager::packageInputsFingerprint(toolWs, tc, config, hostBuild));
        ws.m_features["tool_paths"] = toolWs.m_features["tool_paths"];
        // Host-only recipes are not restored with the target architecture/toolchain.
        std::erase_if(
            ws.m_packageManifest->m_model.rootPackage.dependencies,
            [&](const auto& d)
            {
                return declared.contains(d.name);
            }
        );
    }

    void DslFeatures::prepare(Workspace& ws, const fs::path& build, Configuration, const fs::path& self)
    {
        for (const auto& path : array(ws.m_features, "build_directory"))
            fs::create_directories(outputPath(build, path.get<std::string>()));
        for (auto& t : ws.m_targets)
            if (t.m_features.contains("builtin"))
            {
                // The planner writes the fully resolved description once. Writing
                // unresolved placeholders here would dirty it on every refresh.
                auto file = fs::absolute(build / "features" / (t.m_name + ".json"));
                t.m_args = {"__feature", file.generic_string()};
                unique(t.m_inputs, file.generic_string());
                t.m_tool = self.string();
            }
    }

    void DslFeatures::plan(BuildGraph& graph, const Workspace& ws, const Toolchain&, const fs::path& build, Configuration cfg, const fs::path& self)
    {
        auto expand = [&](std::string value, const std::string& owner)
        {
            std::regex re(R"(\$\{(artifact|artifactdir|generated):([^:}]+)(?::([^}]+))?\})");
            std::smatch m;
            while (std::regex_search(value, m, re))
            {
                auto name = m[2].str();
                if (!graph.m_targetOutputs.contains(name) || graph.m_targetOutputs.at(name).empty())
                    throw std::runtime_error("target has no artifact: " + name);
                auto path = graph.m_targetOutputs.at(name).front();
                if (m[1] == "artifactdir")
                    path = path.parent_path();
                if (m[1] == "generated")
                {
                    path = outputPath(build, m[3]);
                    const auto& outputs = graph.m_targetOutputs.at(name);
                    if (std::find(outputs.begin(), outputs.end(), path) == outputs.end())
                        throw std::runtime_error("undeclared generated file: " + path.string());
                }
                value.replace(m.position(), m.length(), path.generic_string());
            }
            return NativePaths::expandBuildValue(value, ws, build, cfg, owner);
        };
        // Runtime resources preserve source-relative paths and use ordinary owned copy actions.
        for (const auto& t : ws.m_targets)
        {
            if (t.m_features.contains("builtin"))
            {
                auto spec = t.m_features["builtin"];
                spec["workspace"] = ws.m_root.generic_string();
                spec["build"] = fs::absolute(build).generic_string();
                spec["config"] = BuildModel::toString(cfg);
                if (spec.contains("values"))
                    for (auto& [key, value] : spec["values"].items())
                        if (value.is_string())
                            value = expand(value.get<std::string>(), t.m_name);
                FileSystem::writeIfChanged(fs::absolute(build / "features" / (t.m_name + ".json")), spec.dump(2));
            }
            for (auto& a : graph.m_actions)
                if (a.m_owner == t.m_name)
                {
                    if (t.m_features.contains("workingdir"))
                        a.m_workingDirectory = NativePaths::resolveBuildInput(ws, build, expand(t.m_features["workingdir"].back(), t.m_name));
                    for (const auto& env : array(t.m_features, "environment"))
                        for (auto& [k, v] : env.items())
                            a.m_environment[k] = expand(text(v), t.m_name);
                }
            for (const auto& spec : array(t.m_features, "deployfiles"))
            {
                auto root = NativePaths::resolveBuildInput(ws, build, expand(spec.at("root"), t.m_name));
                auto dest = graph.m_targetOutputs.at(t.m_name).front().parent_path() / spec.value("destination", std::string(""));
                for (auto& src : scan(root, strings(spec.value("patterns", Json::array({"**"}))), build))
                {
                    auto out = outputPath(build, (dest / src.lexically_relative(root)).generic_string());
                    if (out == src)
                        continue;
                    Action a;
                    a.m_name = "Deploy " + src.filename().string();
                    a.m_owner = t.m_name;
                    a.m_workingDirectory = ws.m_root;
                    a.m_inputs = {src};
                    a.m_outputs = {out};
                    a.m_restat = true;
                    a.m_argv = {self.string(), "__copy", src.string(), out.string()};
                    graph.m_actions.push_back(a);
                    graph.m_targetOutputs[t.m_name].push_back(out);
                }
            }
            if (t.m_kind == TargetKind::DotNetProject && t.m_features.contains("managedsources"))
            {
                std::string content = "<Project>";
                for (const auto& spec : t.m_features["managedsources"])
                {
                    auto project = FileSystem::dependencyPath(fs::absolute(ws.m_root / spec.value("project", t.m_project)));
                    content += "<ItemGroup Condition=\"'$(MSBuildProjectFullPath)' == '" + xml(project.string()) + "'\">";
                    for (auto& f : strings(spec.value("remove", Json::array())))
                        content += "<Compile Remove=\"" + xml(FileSystem::dependencyPath(ws.m_root / f).generic_string()) + "\" />";
                    for (auto& f : strings(spec.at("include")))
                        content += "<Compile Include=\"" +
                                   xml(FileSystem::dependencyPath(NativePaths::resolveBuildInput(ws, build, expand(f, t.m_name))).generic_string()) + "\" />";
                    content += "</ItemGroup>";
                }
                content += "</Project>";
                auto file = fs::absolute(build / "dotnet" / t.m_name / "GeneratedSources.targets");
                FileSystem::writeIfChanged(file, content);
                for (auto& a : graph.m_actions)
                    if (a.m_owner == t.m_name && a.m_name.starts_with("dotnet build"))
                    {
                        a.m_argv.insert(a.m_argv.end(), {"--imports", file.string()});
                        a.m_inputs.push_back(file);
                    }
            }
        }
        if (ws.m_features.contains("test"))
            fs::create_directories(build / "bin");
        Json tests = Json::array();
        std::set<std::string> names;
        std::string ctest = "# Generated by AxlumBuild\n";
        auto bracket = [](const std::string& s)
        {
            std::string eq = "=";
            while (s.find("]" + eq + "]") != std::string::npos)
                eq += '=';
            return "[" + eq + "[" + s + "]" + eq + "]";
        };
        for (auto spec : array(ws.m_features, "test"))
        {
            auto name = spec.at("name").get<std::string>();
            if (!names.insert(name).second)
                throw std::runtime_error("duplicate test: " + name);
            auto target = spec.value("target", std::string{});
            std::vector<std::string> command;
            if (!target.empty())
            {
                if (!graph.m_targetOutputs.contains(target))
                    throw std::runtime_error("unknown test target: " + target);
                auto t = std::find_if(
                    ws.m_targets.begin(),
                    ws.m_targets.end(),
                    [&](auto& v)
                    {
                        return v.m_name == target;
                    }
                );
                if (t->m_kind == TargetKind::DotNetProject)
                    command = {self.string(), "__managed-test", fs::absolute(build / "dotnet" / target / "artifacts/bin").string()};
                else if (t->m_kind == TargetKind::Executable)
                    command = {graph.m_targetOutputs.at(target).front().string()};
                else
                    throw std::runtime_error("test target must be executable or managed: " + target);
            }
            else
                command = strings(spec.at("command"));
            for (auto& arg : strings(spec.value("args", Json::array())))
                command.push_back(expand(arg, target));
            if (command.empty())
                throw std::runtime_error("test command is empty: " + name);
            auto cwd = spec.value("workingdir", std::string("${build}/bin"));
            cwd = NativePaths::resolveBuildInput(ws, build, expand(cwd, target)).generic_string();
            spec["resolved_command"] = command;
            spec["resolved_workingdir"] = cwd;
            auto resolvedEnvironment = spec.value("environment", Json::object());
            for (auto& [key, value] : resolvedEnvironment.items()) value = expand(text(value), target);
            spec["resolved_environment"] = resolvedEnvironment;
            if (spec.value("discovery", std::string{}) == "gtest")
            {
                if (target.empty() || graph.m_targetKinds.at(target) != TargetKind::Executable)
                    throw std::runtime_error("gtest_discover_tests requires a native executable target: " + name);
                auto emulator = strings(spec.value("emulator", Json::array()));
                for (auto& arg : emulator) arg = expand(arg, target);
                command.insert(command.begin(), emulator.begin(), emulator.end());
                spec["resolved_command"] = command;
                const auto base = fs::absolute(build / "tests" / FileSystem::shortHash(name));
                const auto description = fs::path(base.string() + ".discovery.json");
                const auto cases = fs::path(base.string() + ".json");
                const auto script = fs::path(base.string() + ".cmake");
                spec["discoveryFile"] = cases.string();
                spec["ctestFile"] = script.string();
                FileSystem::writeIfChanged(description, spec.dump(2));
                Action action;
                action.m_name = "Discover tests " + name;
                action.m_owner = target;
                action.m_workingDirectory = ws.m_root;
                action.m_inputs = graph.m_targetOutputs.at(target);
                action.m_inputs.push_back(description);
                action.m_outputs = {cases, script};
                action.m_argv = {self.string(), "__discover-tests", description.string()};
                action.m_restat = true;
                graph.m_actions.push_back(std::move(action));
                graph.m_targetOutputs[target].insert(graph.m_targetOutputs[target].end(), {cases, script});
                ctest += "if(EXISTS " + bracket(script.generic_string()) + ")\ninclude(" + bracket(script.generic_string()) +
                         ")\nelse()\nadd_test(" + bracket(name + "/NOT_BUILT") + " NOT_BUILT)\nendif()\n";
                tests.push_back(spec);
                continue;
            }
            ctest += "add_test(" + bracket(name);
            for (auto& arg : command)
                ctest += " " + bracket(arg);
            ctest += ")\nset_tests_properties(" + bracket(name) + " PROPERTIES WORKING_DIRECTORY " + bracket(cwd) + " TIMEOUT " +
                     std::to_string(spec.value("timeout", 180)) + " LABELS ";
            std::string labels;
            for (auto& l : strings(spec.value("labels", Json::array())))
                labels += (labels.empty() ? "" : ";") + l;
            ctest += bracket(labels);
            auto testEnvironment = spec.value("environment", Json::object());
            std::string env;
            for (auto& [k, v] : testEnvironment.items())
            {
                auto value = expand(text(v), target);
                std::string escaped;
                for (char c : value)
                {
                    if (c == ';')
                        escaped += '\\';
                    escaped += c;
                }
                env += (env.empty() ? "" : ";") + k + "=" + escaped;
            }
            ctest += " ENVIRONMENT " + bracket(env) + ")\n";
            spec["resolved_command"] = command;
            tests.push_back(spec);
        }
        if (!tests.empty() || fs::is_regular_file(build / "axlum.tests.json"))
        {
            FileSystem::writeIfChanged(build / "CTestTestfile.cmake", ctest);
            FileSystem::writeIfChanged(build / "axlum.tests.json", tests.dump(2));
        }
    }

    int DslFeatures::discoverTests(const fs::path& description)
    {
        const auto spec = Json::parse(FileSystem::readAll(description));
        auto command = strings(spec.at("resolved_command"));
        command.push_back("--gtest_list_tests");
        std::string output;
        const auto environment = spec.at("resolved_environment").get<std::map<std::string, std::string>>();
        if (Process::run(command, spec.at("resolved_workingdir").get<std::string>(), environment,
                [&](std::string_view chunk) { output += chunk; }) != 0)
            throw std::runtime_error("GoogleTest discovery failed for " + spec.at("name").get<std::string>() + ":\n" + output);
        auto bracket = [](const std::string& value) {
            std::string eq = "=";
            while (value.find("]" + eq + "]") != std::string::npos) eq += '=';
            return "[" + eq + "[" + value + "]" + eq + "]";
        };
        Json tests = Json::array();
        std::string script = "# Discovered GoogleTest cases\n", suite, line;
        std::set<std::string> names;
        std::istringstream stream(output);
        while (std::getline(stream, line))
        {
            const bool indented = !line.empty() && (line.front() == ' ' || line.front() == '\t');
            line = line.substr(0, line.find('#'));
            const auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos) continue;
            line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
            if (!indented) { suite = line.ends_with('.') ? line : ""; continue; }
            if (suite.empty() || line.find_first_of(" \t") != std::string::npos) continue;
            const auto filter = suite + line;
            const auto name = spec.at("name").get<std::string>() + "/" + filter;
            if (!names.insert(name).second) throw std::runtime_error("duplicate discovered test: " + name);
            const bool disabled = std::regex_search(filter, std::regex("(^|[./])DISABLED_"));
            auto test = spec;
            test.erase("discoveryFile"); test.erase("ctestFile"); test.erase("discovery");
            test["name"] = name;
            test["disabled"] = disabled;
            auto args = strings(spec.at("resolved_command"));
            args.push_back("--gtest_filter=" + filter);
            test["resolved_command"] = args;
            tests.push_back(test);
            script += "add_test(" + bracket(name);
            for (const auto& arg : args) script += " " + bracket(arg);
            script += ")\nset_tests_properties(" + bracket(name) + " PROPERTIES WORKING_DIRECTORY " +
                bracket(spec.at("resolved_workingdir")) + " TIMEOUT " + std::to_string(spec.value("timeout", 180)) +
                " SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION " + bracket("\\[  SKIPPED \\]") + " DISABLED " + (disabled ? "TRUE" : "FALSE");
            std::string labels, env;
            for (const auto& label : strings(spec.value("labels", Json::array()))) labels += (labels.empty() ? "" : ";") + label;
            for (const auto& [key, value] : environment) {
                std::string escaped;
                for (char c : value) { if (c == ';') escaped += '\\'; escaped += c; }
                env += (env.empty() ? "" : ";") + key + "=" + escaped;
            }
            script += " LABELS " + bracket(labels) + " ENVIRONMENT " + bracket(env) + ")\n";
        }
        FileSystem::writeIfChanged(spec.at("ctestFile").get<std::string>(), script);
        FileSystem::writeIfChanged(spec.at("discoveryFile").get<std::string>(), tests.dump(2));
        return 0;
    }

    int DslFeatures::execute(const fs::path& description)
    {
        auto spec = Json::parse(FileSystem::readAll(description));
        auto build = fs::path(spec.at("build").get<std::string>());
        auto root = fs::path(spec.at("workspace").get<std::string>());
        auto out = outputPath(build, spec.at("output"));
        auto values = spec.value("values", Json::object());
        for (auto& [key, value] : values.items())
            if (value.is_string())
            {
                auto s = value.get<std::string>();
                for (auto& [token, replacement] : std::vector<std::pair<std::string, std::string>>{
                         {"${workspace}", root.generic_string()}, {"${build}", build.generic_string()}, {"${config}", spec.at("config").get<std::string>()}
                     })
                {
                    for (auto pos = s.find(token); pos != std::string::npos; pos = s.find(token, pos + replacement.size()))
                        s.replace(pos, token.size(), replacement);
                }
                value = s;
            }
        std::string result;
        if (spec.value("header", false))
        {
            result = "#pragma once\n";
            for (auto& [key, v] : values.items())
            {
                if (!std::regex_match(key, std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                    throw std::runtime_error("invalid configuration macro: " + key);
                result += "#define " + key + " " + (v.is_string() ? v.dump() : text(v)) + "\n";
            }
        }
        else
        {
            auto source = FileSystem::readAll(root / spec.at("input").get<std::string>());
            std::regex re(R"(@([A-Za-z_][A-Za-z0-9_]*)(:cstr)?@)");
            std::smatch m;
            while (std::regex_search(source, m, re))
            {
                auto key = m[1].str();
                if (!values.contains(key))
                    throw std::runtime_error("undefined template variable: " + key);
                result += m.prefix().str();
                result += m[2].matched ? Json(text(values[key])).dump() : text(values[key]);
                source = m.suffix();
            }
            result += source;
        }
        // Common substitutions in configured strings remain explicit and deterministic.
        FileSystem::writeIfChanged(out, result);
        return 0;
    }

    int DslFeatures::runTests(const fs::path& build, const std::string& pattern, const std::string& label)
    {
        auto ctest = Process::findExecutable("ctest");
        if (ctest.empty())
            throw std::runtime_error("ctest is required by the test runner");
        std::vector<std::string> args = {ctest, "--test-dir", build.string(), "--output-on-failure", "--no-tests=error"};
        if (!pattern.empty())
            args.insert(args.end(), {"-R", pattern});
        if (!label.empty())
            args.insert(args.end(), {"-L", label});
        return Process::run(args, build);
    }
} // namespace axlum
