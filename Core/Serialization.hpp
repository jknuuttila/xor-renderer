#pragma once

#include "Core/Utils.hpp"
#include "Core/Exception.hpp"

#include <array>
#include <cstring>

namespace xor
{
    namespace serialization
    {
        static const uint32_t StructSizeBits = 20;
        static const uint32_t StructSizeMask = (1U << StructSizeBits) - 1;
    }

    XOR_EXCEPTION_TYPE(SerializationException)

    class Reader
    {
        Span<const uint8_t> m_bytes;
        const uint8_t *     m_cursor = nullptr;

        void checkBounds(size_t bytes) const
        {
            XOR_THROW(bytesLeft() >= bytes, SerializationException, "Ran out of bytes while trying to read");
        }
    public:
        Reader() = default;
        Reader(Span<const uint8_t> bytes)
            : m_bytes(bytes)
            , m_cursor(bytes.begin())
        {}

        void seek(size_t offset)
        {
            XOR_THROW(offset <= m_bytes.sizeBytes(), SerializationException, "Seek out of bounds");
            m_cursor = m_bytes.begin() + offset;
        }

        void reset() { seek(0); }

        size_t bytesLeft() const
        {
            return static_cast<size_t>(m_bytes.end() - m_cursor);
        }

        template <typename T>
        T read()
        {
            static_assert(IsPod<T>::value, "read() only supports POD types");
            T value;
            checkBounds(sizeof(T));
            memcpy(&value, m_cursor, sizeof(T));
            m_cursor += sizeof(T);
            return value;
        }

        uint readLength();

        template <typename T>
        T readStruct()
        {
            static_assert(IsPod<T>::value, "readStruct() only supports POD types");
            uint32_t structHeader  = read<uint32_t>();

            uint32_t versionNumber = structHeader >> serialization::StructSizeBits;
            uint32_t size          = structHeader & serialization::StructSizeMask;

            XOR_THROW(size == sizeof(T)                , SerializationException, "Serialized struct size differs from expected");
            XOR_THROW(versionNumber == T::VersionNumber, SerializationException, "Serialized struct version number differs from expected");

            return read<T>();
        }

        Span<const uint8_t> readBlob();
        StringView readString();
    };

    template <typename Buffer>
    class Writer
    {
        Buffer  *m_buffer = nullptr;
        size_t   m_cursor = 0;

        void ensureSpace(size_t bytes)
        {
            size_t bytesLeft = static_cast<size_t>(m_buffer->size() - m_cursor);

            if (bytesLeft < bytes)
                m_buffer->resize(m_buffer->size() + bytes - bytesLeft);
        }

        uint8_t *cursor() { return reinterpret_cast<uint8_t *>(m_buffer->data()) + m_cursor; }

    public:
        Writer() = default;
        Writer(Buffer &buffer, size_t sizeEstimate = 0)
            : m_buffer(&buffer)
        {
            if (sizeEstimate)
                m_buffer->reserve(std::max(m_buffer->size(), sizeEstimate));
        }

        size_t bytesWritten() const
        {
            return m_cursor;
        }

        template <typename T>
        void write(const T &value)
        {
            static_assert(IsPod<T>::value, "write() only supports POD types");
            ensureSpace(sizeof(T));
            memcpy(cursor(), &value, sizeof(T));
            m_cursor += sizeof(T);
        }

        void writeLength(uint length)
        {
            if (length < 255)
            {
                uint8_t shortLength = static_cast<uint8_t>(length);
                write(shortLength);
            }
            else
            {
                uint8_t shortLength = 255;
                write(shortLength);
                write(length);
            }
        }

        template <typename T>
        void writeStruct(const T &value)
        {
            static_assert(IsPod<T>::value, "writeStruct() only supports POD types");
            uint32_t size          = sizeof(T);
            uint32_t versionNumber = T::VersionNumber;
            XOR_THROW(size == (size & serialization::StructSizeMask), SerializationException, "Struct is too large");
            uint32_t structHeader  = (versionNumber << serialization::StructSizeBits) | size;
            write(structHeader);
            write(value);
        }

        void writeBlob(Span<const uint8_t> bytes)
        {
            uint length = static_cast<uint>(bytes.sizeBytes());
            writeLength(length);
            ensureSpace(length);
            memcpy(cursor(), bytes.data(), length);
            m_cursor += length;
        }

        void writeString(StringView str)
        {
            uint length = str.length();
            writeLength(length);
            ensureSpace(length);
            memcpy(cursor(), str.data(), length);
            m_cursor += length;
        }
    };

    template <typename T>
    size_t serializedStructSize()
    {
        return sizeof(T) + sizeof(uint32_t);
    }

    template <typename Buffer>
    Writer<Buffer> makeWriter(Buffer &buffer, size_t sizeEstimate = 0)
    {
        return Writer<Buffer>(buffer, sizeEstimate);
    }

    struct FourCC
    {
        std::array<char, 4> fourCC;

        FourCC() { fourCC.fill(0); }
        FourCC(const char *str)
        {
            fourCC.fill(0);
            memcpy(fourCC.data(), str, std::min(strlen(str), fourCC.size()));
        }
        FourCC(uint32_t u)
        {
            memcpy(fourCC.data(), &u, sizeof(u));
        }

        uint32_t asUint() const
        {
            uint32_t u;
            memcpy(&u, fourCC.data(), sizeof(u));
            return u;
        }

        StringView asString() const
        {
            return StringView(fourCC.data(), fourCC.data() + fourCC.size());
        }
    };
}
