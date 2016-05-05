#pragma once

#include "OS.hpp"
#include "File.hpp"

#include <string>
#include <vector>

namespace xor
{
    struct BuildInfo
    {
        // Given target file.
        path target;
        // Main source file that produced the target file.
        path source;
        // All source files that participated in building the target file.
        std::vector<path> dependencies;
        // Path to the build executable
        path buildExe;
        // The exact build arguments that were used to build the target file.
        String buildArgs;
    };

    std::vector<BuildInfo> scanBuildInfos(path tlogDirectory, const String &extension = String());
}

