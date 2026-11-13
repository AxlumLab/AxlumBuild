/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/Resources.hpp"
#include "core/Process.hpp"
#include <functional>
#include <regex>
#include <set>
#include <sstream>

namespace axlum
{
    std::vector<fs::path> ResourceScanner::resourceDependencies(const fs::path& source, const std::vector<fs::path>& includes)
    {
        std::vector<fs::path> result;
        std::set<fs::path> visited;
        // RC has no native dependency-file switch. Track literal includes and file resources,
        // recursively; macro-generated filenames remain expressible through resourceinputs.
        const std::regex include(R"re(^\s*#\s*include\s*"([^"]+)")re");
        const std::regex resource(R"re(\b(?:ICON|BITMAP|CURSOR|RCDATA|HTML|MANIFEST|AVI|FONT|MESSAGETABLE|TYPELIB)\b[^"\r\n]*"([^"]+)")re", std::regex::icase);
        std::function<void(fs::path)> scan = [&](fs::path file)
        {
            file = fs::weakly_canonical(file);
            if (!visited.insert(file).second || !fs::is_regular_file(file))
                return;
            if (file != fs::weakly_canonical(source))
                result.push_back(file);
            const auto ext = file.extension().string();
            if (file != fs::weakly_canonical(source) && ext != ".h" && ext != ".hpp" && ext != ".rc" && ext != ".rc2")
                return;
            std::istringstream text{FileSystem::readAll(file)};
            std::string line;
            while (std::getline(text, line))
            {
                std::smatch match;
                const bool header = std::regex_search(line, match, include);
                if (!header && !std::regex_search(line, match, resource))
                    continue;
                const fs::path name = match[1].str();
                std::vector<fs::path> search = {file.parent_path(), source.parent_path()};
                search.insert(search.end(), includes.begin(), includes.end());
                for (const auto& directory : search)
                    if (fs::is_regular_file(directory / name))
                    {
                        scan(directory / name);
                        break;
                    }
            }
        };
        scan(source);
        return result;
    }
} // namespace axlum
