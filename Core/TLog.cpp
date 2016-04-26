#include "TLog.hpp"
#include "Utils.hpp"

#include <unordered_map>

namespace xor
{
    std::vector<BuildInfo> scanBuildInfos(path tlogDirectory, const String &extension)
    {
        static const char writeTlog[] = ".write";
        static const char readTlog[]  = ".read";
        static const char cmdTlog[]   = ".command";

        for (auto &entry : recursive_directory_iterator(tlogDirectory))
        {
        }
    }
}
