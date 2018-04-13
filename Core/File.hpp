#pragma once

#include "OS.hpp"
#include "String.hpp"
#include "Utils.hpp"

#include <filesystem>

namespace Xor
{
    namespace fs = std::experimental::filesystem;

    class File
    {
        Handle         m_file;
        Handle         m_mapping;
        const uint8_t *m_begin = nullptr;
        const uint8_t *m_end   = nullptr;
        HRESULT        m_hr    = E_NOT_SET;
    public:
        enum class Create
        {
            DontCreate,
            CreateNew,
            Overwrite,
            CreateAlways,
        };
        enum class Mode
        {
            ReadOnly,
            ReadMapped,
            ReadWrite,
        };

        File() = default;
        File(const String &filename, Mode mode = Mode::ReadOnly, Create create = Create::DontCreate);

        File(File &&) = default;
        File &operator=(File &&) = default;
        File(const File &) = delete;
        File &operator=(const File &) = delete;

        HRESULT hr() const { return m_hr; }
        explicit operator bool() const { return SUCCEEDED(m_hr); }

        size_t size() const;

        const uint8_t *data()  const { return m_begin; }
        const uint8_t *begin() const { return m_begin; }
        const uint8_t *end() const { return m_end; }

        void seek(int64_t pos);
        void seekRelative(int64_t pos);

        void close();
        HRESULT read(void *dst, size_t bytes, size_t *bytesRead = nullptr);
        HRESULT write(const void *src, size_t bytes);
        HRESULT read(Span<uint8_t> dst);
        HRESULT write(Span<const uint8_t> src);

        DynamicBuffer<uint8_t> read();
        String readText();
        std::wstring readWideText();

        static bool exists(const String &path);
        static uint64_t lastWritten(const String &path);
        static String canonicalize(const String &path, bool absolute = false);
        static bool ensureDirectoryExists(const String &path);
    };

    int shellCommand(const String &exe, StringView args,
                     String *stdOut      = nullptr,
                     String *stdErr      = nullptr,
                     const String *stdIn = nullptr);

    std::vector<String> listFiles(const String &path, const String &pattern = "*");
    std::vector<String> searchFiles(const String &path, const String &pattern);

    String fileOpenDialog(const String &description, const String &pattern);
    String fileSaveDialog(const String &description, const String &pattern);
    String absolutePath(const String &path);
    std::vector<String> splitPath(const String &path);
}

