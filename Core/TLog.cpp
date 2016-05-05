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

    std::vector<BuildInfo> scanBuildInfos(path tlogDirectory, const String &extension)
    {
        static const char tlogExtension[] = ".tlog";
        static const char writeTlog[] = ".write";
        static const char readTlog[]  = ".read";
        static const char cmdTlog[]   = ".command";

        std::vector<String> readFiles;
        std::vector<String> writeFiles;
        std::vector<String> cmdFiles;

        for (auto &entry : recursive_directory_iterator(tlogDirectory))
        {
            auto p = entry.path();
            String path = StringView(absolute(p)).lower();
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
                    if (exists(path(src.stdString())))
                        sourceFile = src;
                }
                else if (sourceFile)
                {
                    auto target = l.lower();

                    if (!target.contains(extension))
                        continue;

                    sourceBuildInfos[sourceFile].source = path(sourceFile.cStr());
                    sourceBuildInfos[sourceFile].target = path(target.cStr());
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
                    auto sourceFile = l.from(srcPos + 1).lower();
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
                    auto exe = l.lower();
                    buildInfo->buildExe = path(exe.cStr());
                    log("TLog", "%S was built with executable %s\n", buildInfo->source.c_str(), exe.cStr());
                    firstReadDep = false;
                }
                else if (buildInfo)
                {
                    auto dep = l.lower();
                    buildInfo->dependencies.emplace_back(path(dep.cStr()));
                    log("TLog", "%S depends on %s\n", buildInfo->source.c_str(), dep.cStr());
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
                    auto sourceFile = l.from(srcPos + 1).lower();
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
                    log("TLog", "%S was built with arguments %s\n", buildInfo->source.c_str(), args.cStr());
                }
            }
        }

        std::vector<BuildInfo> buildInfos;
        buildInfos.reserve(sourceBuildInfos.size());
        for (auto &kv : sourceBuildInfos)
            buildInfos.emplace_back(std::move(kv.second));
        return buildInfos;
    }
}
