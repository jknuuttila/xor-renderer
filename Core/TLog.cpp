#include "TLog.hpp"

#include <unordered_map>

namespace xor
{
    std::vector<BuildInfo> scanBuildInfos(path tlogDirectory, const String &extension)
    {
        std::unordered_map<path, BuildInfo> infoForTarget;
        std::unordered_map<path, std::vector<path>> depsForSource;
        return {};
    }
}
