#pragma once

#include "OS.hpp"
#include "File.hpp"

#include <string>
#include <vector>
#include <memory>

namespace xor
{
    struct BuildInfo
    {
        // Given target file.
        String target;
        // Main source file that produced the target file.
        String source;
        // All source files that participated in building the target file.
        std::vector<String> dependencies;
        // Path to the build executable
        String buildExe;
        // The exact build arguments that were used to build the target file.
        String buildArgs;

        bool isTargetOutOfDate() const;
        uint64_t targetTimestamp() const;
        uint64_t sourceTimestamp() const;
    };

    std::vector<std::shared_ptr<const BuildInfo>> scanBuildInfos(const String &tlogDirectory, const String &extension = String());
}

