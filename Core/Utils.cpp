#include "OS.hpp"
#include "Error.hpp"
#include "Utils.hpp"
#include "String.hpp"
#include "Math.hpp"

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

    SequenceTracker::Bit SequenceTracker::bit(uint64_t seqNum) const
    {
        Bit b;
        
        uint64_t offset       = seqNum - m_uncompletedBase;
        uint64_t offsetQwords = offset / 64;
        uint64_t offsetBits   = offset % 64;

        b.qword = (offsetQwords < m_uncompletedBits.size())
            ? static_cast<int64_t>(offsetQwords)
            : -1;

        b.mask = 1ull << offsetBits;

        return b;
    }

    void SequenceTracker::removeCompletedBits()
    {
        auto begin = m_uncompletedBits.begin();

        auto firstNonZero = std::find_if(
            begin,
            m_uncompletedBits.end(),
            [] (auto v) { return v != 0; });

        auto zeroes = firstNonZero - begin;

        // Always leave at least one qword, because some of 
        // its bits might be completely unused.
        if (zeroes > 1)
        {
            auto remove = zeroes - 1;
            m_uncompletedBits.erase(begin, begin + remove);
            m_uncompletedBase += static_cast<uint64_t>(remove) * 64;
        }
    }

    int64_t SequenceTracker::lowestSetBit() const
    {
        int64_t base = 0;

        for (auto &qword : m_uncompletedBits)
        {
            auto lowest = firstbitlow(qword);

            if (lowest >= 0)
            {
                return lowest + base;
            }

            base += 64;
        }

        return -1;
    }

    uint64_t SequenceTracker::start()
    {
        auto seqNum = m_next;
        ++m_next;

        auto b = bit(seqNum);
        if (b.qword < 0)
        {
            XOR_ASSERT(seqNum == m_uncompletedBits.size() * 64 + m_uncompletedBase,
                       "Sequence number out of sync.");

            m_uncompletedBits.emplace_back(1);
        }
        else
        {
            m_uncompletedBits[b.qword] |= b.mask;
        }

        return seqNum;
    }

    void SequenceTracker::complete(uint64_t seqNum)
    {
        XOR_ASSERT(!hasCompleted(seqNum),
                   "Sequence number %ull was completed twice.",
                   static_cast<ull>(seqNum));

        auto b = bit(seqNum);

        XOR_ASSERT(b.qword >= 0,
                   "Sequence number %ull was never started.",
                   static_cast<ull>(seqNum));

        m_uncompletedBits[b.qword] &= ~b.mask;

        removeCompletedBits();
    }

    uint64_t SequenceTracker::oldestUncompleted() const
    {
        auto lowest = lowestSetBit();

        if (lowest >= 0)
            return m_uncompletedBase + static_cast<uint64_t>(lowest);
        else
            return m_next;
    }

    bool SequenceTracker::hasCompleted(uint64_t seqNum) const
    {
        if (seqNum < m_uncompletedBase)
            return true;

        auto b = bit(seqNum);

        XOR_ASSERT(b.qword >= 0,
                   "Sequence number %ull was never started.",
                   static_cast<ull>(seqNum));

        return (m_uncompletedBits[b.qword] & b.mask) == 0;
    }
}
