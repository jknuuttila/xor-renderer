#include "TLog.hpp"
#include "Utils.hpp"
#include "Log.hpp"
#include "String.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xor
{
    static std::vector<String> tlogLines(StringView path)
    {
        return String(File(path).readWideText()).lines();
    }

    std::vector<std::shared_ptr<const BuildInfo>> scanBuildInfos(const String &tlogDirectory,
                                                                 const String &extension)
    {
        static const char tlogExtension[] = ".tlog";
        static const char writeTlog[] = ".write";
        static const char readTlog[]  = ".read";
        static const char cmdTlog[]   = ".command";

        std::vector<String> readFiles;
        std::vector<String> writeFiles;
        std::vector<String> cmdFiles;

        for (auto &entry : fs::recursive_directory_iterator(tlogDirectory.cStr()))
        {
            auto p = entry.path();
            String path = File::canonicalize(String(p), true);
            String ext  = StringView(p.extension()).lower();

            if (ext != tlogExtension)
                continue;

            if (path.contains(readTlog))
                readFiles.emplace_back(std::move(path));
            else if (path.contains(writeTlog))
                writeFiles.emplace_back(std::move(path));
            else if (path.contains(cmdTlog))
                cmdFiles.emplace_back(std::move(path));
        }

        std::unordered_map<String, BuildInfo> sourceBuildInfos;

        for (auto &writeFile : writeFiles)
        {
            auto lines = tlogLines(writeFile);
            String sourceFile;
            for (auto &l : lines)
            {
                int srcPos = l.find("^");
                if (srcPos >= 0)
                {
                    auto src = l.from(srcPos + 1).lower();
                    if (File::exists(src))
                        sourceFile = File::canonicalize(src);
                }
                else if (sourceFile)
                {
                    auto target = File::canonicalize(l);

                    if (!target.contains(extension))
                        continue;

                    sourceBuildInfos[sourceFile].source = sourceFile;
                    sourceBuildInfos[sourceFile].target = target;
                    log("TLog", "%s -> %s\n", sourceFile.cStr(), target.cStr());
                }
            }
        }

        for (auto &readFile : readFiles)
        {
            auto lines = tlogLines(readFile);
            BuildInfo *buildInfo = nullptr;
            bool firstReadDep = false;
            for (auto &l : lines)
            {
                int srcPos = l.find("^");
                if (srcPos >= 0)
                {
                    auto sourceFile = File::canonicalize(l.from(srcPos + 1));
                    auto it = sourceBuildInfos.find(sourceFile);
                    if (it != sourceBuildInfos.end())
                    {
                        buildInfo = &it->second;
                        firstReadDep = true;
                    }
                    else
                    {
                        buildInfo = nullptr;
                        firstReadDep = false;
                    }
                }
                else if (buildInfo && firstReadDep)
                {
                    auto exe = File::canonicalize(l);
                    buildInfo->buildExe = exe;
                    log("TLog", "%s was built with executable %s\n", buildInfo->source.cStr(), exe.cStr());
                    firstReadDep = false;
                }
                else if (buildInfo)
                {
                    auto dep = File::canonicalize(l);
                    buildInfo->dependencies.emplace_back(dep.cStr());
                    log("TLog", "%s depends on %s\n", buildInfo->source.cStr(), dep.cStr());
                }
            }
        }

        for (auto &cmdFile : cmdFiles)
        {
            auto lines = tlogLines(cmdFile);
            BuildInfo *buildInfo = nullptr;
            for (auto &l : lines)
            {
                int srcPos = l.find("^");
                if (srcPos >= 0)
                {
                    auto sourceFile = File::canonicalize(l.from(srcPos + 1));
                    auto it = sourceBuildInfos.find(sourceFile);
                    if (it != sourceBuildInfos.end())
                        buildInfo = &it->second;
                    else
                        buildInfo = nullptr;
                }
                else if (buildInfo)
                {
                    auto &args = l;
                    buildInfo->buildArgs = args;
                    log("TLog", "%s was built with arguments %s\n", buildInfo->source.cStr(), args.cStr());
                }
            }
        }

        std::vector<std::shared_ptr<const BuildInfo>> buildInfos;
        buildInfos.reserve(sourceBuildInfos.size());
        for (auto &kv : sourceBuildInfos)
        {
            auto info = std::make_shared<BuildInfo>(std::move(kv.second));
            buildInfos.emplace_back(std::move(info));
        }
        return buildInfos;
    }

    bool BuildInfo::isTargetOutOfDate() const
    {
        return targetTimestamp() < sourceTimestamp();
    }

    uint64_t BuildInfo::targetTimestamp() const
    {
        return File::lastWritten(target);
    }

    uint64_t BuildInfo::sourceTimestamp() const
    {
        uint64_t timestamp = File::lastWritten(source);

        for (auto &dep : dependencies)
            timestamp = std::max(timestamp, File::lastWritten(dep));

        return timestamp;
    }
}
