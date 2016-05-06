#include "Error.hpp"
#include "Utils.hpp"
#include "File.hpp"

namespace xor
{
#if 0
    std::vector<String> listFiles(const String &path, const String &pattern)
    {
        std::vector<String> files;
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

    std::vector<String> searchFiles(const String &path, const String &pattern)
    {
        std::vector<String> files;

        auto matches = listFiles(path, pattern);
        auto prefix  = path + "/";

        auto addFiles = [&](const std::vector<String> &fs, const String &prefix = String())
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
                (f.find(".", 0) == String::npos))
            {
                addFiles(searchFiles(prefix + f, pattern));
            }
        }

        return files;
    }

    std::vector<String> splitPath(const String & path)
    {
        auto canonical = replaceAll(path, "\\", "/");
        auto pathParts = tokenize(canonical, "/");
        return pathParts;
    }

    String fileOpenDialog(const String &description, const String &pattern)
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
            return String();
        else
            return String(fn.lpstrFile);
    }

    String fileSaveDialog(const String & description, const String & pattern)
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
            return String();
        else
            return String(fn.lpstrFile);
    }

    String absolutePath(const String & path)
    {
        char absPath[MAX_PATH + 2] ={ 0 };
        if (!GetFullPathNameA(path.c_str(), sizeof(absPath), absPath, nullptr))
            return String();
        else
            return String(absPath);
    }
#endif

    File::File(const String &filename, Mode mode, Create create)
    {
        if (mode != Mode::ReadWrite)
            create = Create::DontCreate;

        DWORD creation;
        switch (create)
        {
        case Create::DontCreate:
        default:
            creation = OPEN_EXISTING;
            break;
        case Create::CreateNew:
            creation = CREATE_NEW;
            break;
        case Create::Overwrite:
            creation = TRUNCATE_EXISTING;
            break;
        }

        DWORD shareMode = 0;
        if (mode == Mode::ReadOnly)
            shareMode = FILE_SHARE_READ;

        m_file = CreateFileA(filename.cStr(),
                             mode == Mode::ReadWrite
                             ? GENERIC_WRITE
                             : GENERIC_READ,
                             shareMode,
                             nullptr,
                             creation,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);

        if (!m_file)
            m_hr = HRESULT_FROM_WIN32(GetLastError());
        else
            m_hr = S_OK;

        if (m_hr == S_OK && mode == Mode::ReadMapped)
        {
            size_t sz    = size();
            DWORD szHigh = static_cast<DWORD>(sz >> 32);
            DWORD szLow  = static_cast<DWORD>(sz);
            m_mapping    = CreateFileMappingA(m_file.get(),
                                              nullptr,
                                              PAGE_READONLY,
                                              szHigh, szLow,
                                              nullptr);

            if (!m_mapping)
            {
                m_hr = HRESULT_FROM_WIN32(GetLastError());
                m_file.close();
                return;
            }

            m_begin = reinterpret_cast<const uint8_t *>(
                MapViewOfFile(m_mapping.get(),
                              FILE_MAP_READ,
                              0, 0,
                              sz));
            m_end = m_begin + sz;
        }
    }

    size_t File::size() const
    {
        LARGE_INTEGER sz = {};
        if (!GetFileSizeEx(m_file.get(), &sz))
            XOR_CHECK_LAST_ERROR();
        return static_cast<size_t>(sz.QuadPart);
    }

    void File::seek(int64_t pos)
    {
        LARGE_INTEGER distance = {};
        distance.QuadPart = pos;
        DWORD from = (pos >= 0) ? FILE_BEGIN : FILE_END;
        if (!SetFilePointerEx(m_file.get(),
                              distance,
                              nullptr,
                              from))
            XOR_CHECK_LAST_ERROR();
    }

    void File::seekRelative(int64_t pos)
    {
        LARGE_INTEGER distance = {};
        distance.QuadPart = pos;
        if (!SetFilePointerEx(m_file.get(),
                              distance,
                              nullptr,
                              FILE_CURRENT))
            XOR_CHECK_LAST_ERROR();
    }

    void File::close()
    {
        *this = File();
    }

    HRESULT File::read(void *dst, size_t bytes, size_t *bytesRead)
    {
        uint8_t *p = reinterpret_cast<uint8_t *>(dst);

        size_t left = bytes;
        while (left > 0)
        {
            DWORD amount = static_cast<DWORD>(
                std::min<size_t>(std::numeric_limits<DWORD>::max(),
                                 left));
            DWORD got = 0;
            if (!ReadFile(m_file.get(),
                          p,
                          amount,
                          &got,
                          nullptr))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }

            left -= got;
            p    += got;

            if (got == 0)
                break;
        }

        if (bytesRead)
        {
            *bytesRead = bytes - left;
        }

        return S_OK;
    }

    HRESULT File::write(const void *src, size_t bytes)
    {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(src);

        while (bytes > 0)
        {
            DWORD amount = static_cast<DWORD>(
                std::min<size_t>(std::numeric_limits<DWORD>::max(),
                                 bytes));
            DWORD put = 0;
            if (!WriteFile(m_file.get(),
                           p,
                           amount,
                           &put,
                           nullptr))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }

            bytes -= put;
            p     += put;

            if (put == 0)
                break;
        }

        if (bytes > 0)
            return E_FAIL;
        else
            return S_OK;
    }

    HRESULT File::read(span<uint8_t> dst)
    {
        size_t amount;

        auto hr = read(dst.data(), dst.size_bytes(), &amount);
        if (FAILED(hr))
            return hr;

        if (amount != dst.size_bytes())
            return E_FAIL;

        return S_OK;
    }

    HRESULT File::write(span<const uint8_t> src)
    {
        auto hr = write(src.data(), src.size_bytes());
        if (FAILED(hr))
            return hr;
        else
            return S_OK;
    }

    std::vector<uint8_t> File::read()
    {
        std::vector<uint8_t> contents;
        contents.resize(size());
        seek(0);
        XOR_CHECK_HR(read(contents));
        return contents;
    }

    String File::readText()
    {
        std::string contents;
        contents.resize(size());
        seek(0);
        size_t length;
        XOR_CHECK_HR(read(&contents[0], size(), &length));
        contents.resize(length);
        return String(std::move(contents));
    }

    std::wstring File::readWideText()
    {
        std::wstring contents;
        size_t length = size() / sizeof(wchar_t);
        contents.resize(length);
        seek(0);
        size_t bytes;
        XOR_CHECK_HR(read(&contents[0], sizeBytes(contents), &bytes));
        length = bytes / sizeof(wchar_t);
        contents.resize(length);
        return contents;
    }

    bool File::exists(const String & path)
    {
        return fs::exists(fs::path(path.cStr()));
    }

    uint64_t File::lastWritten(const String & path)
    {
        WIN32_FILE_ATTRIBUTE_DATA info = {};

        if (!GetFileAttributesExA(path.cStr(), GetFileExInfoStandard, &info))
            return 0;

        return (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
            | static_cast<uint64_t>(info.ftLastWriteTime.dwLowDateTime);
    }

    String File::canonicalize(const String &path, bool absolute)
    {
        String absPath;
        if (absolute)
            absPath = fs::absolute(path.cStr());

        // TODO: Proper casing
        return absPath ? absPath.lower() : path.lower();
    }

    struct Pipe
    {
        Handle read;
        Handle write;

        Pipe() = default;
        static Pipe create()
        {
            Pipe p;

            SECURITY_ATTRIBUTES security = {};
            security.nLength              = sizeof(security);
            security.lpSecurityDescriptor = nullptr;
            security.bInheritHandle       = true;

            if (!CreatePipe(p.read.outRef(), p.write.outRef(), &security, 0))
                XOR_CHECK_LAST_ERROR();

            return p;
        }

        explicit operator bool() const
        {
            return read && write;
        }

        void writeTo(StringView text)
        {
            if (!WriteFile(write.get(),
                           text.data(), static_cast<DWORD>(text.size()),
                           nullptr,
                           nullptr))
                XOR_CHECK_LAST_ERROR();
        }

        String readFrom()
        {
            static const DWORD ChunkSize = 4096;

            size_t len = 0;
            std::string s;

            for (;;)
            {
                DWORD bytes = 0;
                if (!PeekNamedPipe(read.get(),
                                   nullptr, 0, nullptr,
                                   &bytes, nullptr))
                    XOR_CHECK_LAST_ERROR();

                if (bytes == 0)
                    break;

                auto toRead = std::min(bytes, ChunkSize);
                auto tail = s.size();

                s.resize(s.size() + toRead);

                DWORD got = 0;
                if (!ReadFile(read.get(), &s[tail], toRead, &got, nullptr))
                    XOR_CHECK_LAST_ERROR();

                len += got;
            }
            s.resize(len);
            return String(std::move(s));
        }
    };

    int shellCommand(const String &exe, StringView args,
                     String * stdOut,
                     String * stdErr,
                     const String * stdIn)
    {
        std::string commandLine = String::format(
            "\"%s\" %s", exe.cStr(), args.str().cStr()).stdString();

        STARTUPINFOA startupInfo = {};
        startupInfo.cb      = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;

        Pipe out               = Pipe::create();
        Pipe err               = Pipe::create();
        Pipe in                = Pipe::create();
        startupInfo.hStdOutput = out.write.get();
        startupInfo.hStdError  = err.write.get();
        startupInfo.hStdOutput = in.read.get();

        PROCESS_INFORMATION procInfo = {};
        if (!CreateProcessA(
            exe.cStr(),
            &commandLine[0],
            nullptr,
            nullptr,
            true,
            0,
            nullptr,
            nullptr,
            &startupInfo,
            &procInfo))
            XOR_CHECK_LAST_ERROR();

        if (stdIn)
            in.writeTo(*stdIn);

        WaitForSingleObject(procInfo.hProcess, INFINITE);

        if (stdErr)
            *stdErr = err.readFrom();
        if (stdOut)
            *stdOut = out.readFrom();

        DWORD exitCode = 0;
        if (!GetExitCodeProcess(procInfo.hProcess, &exitCode))
            XOR_CHECK_LAST_ERROR();

        CloseHandle(procInfo.hProcess);
        CloseHandle(procInfo.hThread);

        return static_cast<int>(exitCode);
    }
}
