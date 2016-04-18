#include "OS.hpp"
#include "Error.hpp"
#include "Utils.hpp"
#include "String.hpp"

#include <cstring>
#include <vector>

namespace xor
{
    std::vector<std::string> listFiles(const std::string &path, const std::string &pattern)
    {
        std::vector<std::string> files;
        WIN32_FIND_DATAA findData;
        zero(findData);

        auto findPat = path + "/" + pattern;

        auto hnd = FindFirstFileA(findPat.c_str(), &findData);

        bool haveFiles = hnd != INVALID_HANDLE_VALUE;
        while (haveFiles)
        {
            files.emplace_back(findData.cFileName);
            haveFiles = !!FindNextFileA(hnd, &findData);
        }

        FindClose(hnd);

        return files;
    }

    std::vector<std::string> searchFiles(const std::string &path, const std::string &pattern)
    {
        std::vector<std::string> files;

        auto matches = listFiles(path, pattern);
        auto prefix  = path + "/";

        auto addFiles = [&](const std::vector<std::string> &fs, const std::string &prefix = std::string())
        {
            for (auto &f : fs)
            {
                files.emplace_back(prefix + f);
            }
        };

        addFiles(listFiles(path, pattern), prefix);

        auto allFilesInDir = listFiles(path);
        for (auto &f : allFilesInDir)
        {
            if ((GetFileAttributesA(f.c_str()) & FILE_ATTRIBUTE_DIRECTORY) &&
                (f.find(".", 0) == std::string::npos))
            {
                addFiles(searchFiles(prefix + f, pattern));
            }
        }

        return files;
    }

    std::vector<std::string> splitPath(const std::string & path)
    {
        auto canonical = replaceAll(path, "\\", "/");
        auto pathParts = tokenize(canonical, "/");
        return pathParts;
    }

    std::string fileOpenDialog(const std::string &description, const std::string &pattern)
    {
        auto filter = description + '\0' + pattern + '\0' + '\0';

        char fileName[MAX_PATH + 2];
        zero(fileName);

        OPENFILENAME fn;
        zero(fn);
        fn.lStructSize     = sizeof(fn);
        fn.lpstrFilter     = filter.c_str();
        fn.nFilterIndex    = 1;
        fn.lpstrFile       = fileName;
        fn.nMaxFile        = sizeof(fileName) - 1;
        fn.lpstrInitialDir = ".";
        fn.Flags |= OFN_NOCHANGEDIR;

        if (!GetOpenFileNameA(&fn))
            return std::string();
        else
            return std::string(fn.lpstrFile);
    }

    std::string fileSaveDialog(const std::string & description, const std::string & pattern)
    {
        auto filter = description + '\0' + pattern + '\0' + '\0';

        char fileName[MAX_PATH + 2];
        zero(fileName);

        OPENFILENAME fn;
        zero(fn);
        fn.lStructSize     = sizeof(fn);
        fn.lpstrFilter     = filter.c_str();
        fn.nFilterIndex    = 1;
        fn.lpstrFile       = fileName;
        fn.nMaxFile        = sizeof(fileName) - 1;
        fn.lpstrInitialDir = ".";
        fn.Flags |= OFN_NOCHANGEDIR;

        if (!GetSaveFileNameA(&fn))
            return std::string();
        else
            return std::string(fn.lpstrFile);
    }

    std::string absolutePath(const std::string & path)
    {
        char absPath[MAX_PATH + 2] ={ 0 };
        if (!GetFullPathNameA(path.c_str(), sizeof(absPath), absPath, nullptr))
            return std::string();
        else
            return std::string(absPath);
    }

    Timer::Timer()
    {
        LARGE_INTEGER f;
        LARGE_INTEGER now;

        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&now);

        period = 1.0 / static_cast<double>(f.QuadPart);
        start  = now.QuadPart;
    }

    double Timer::seconds() const
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        uint64_t ticks = now.QuadPart - start;
        return static_cast<double>(ticks) * period;
    }
}
