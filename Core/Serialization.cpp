#include "Core/Serialization.hpp"

namespace Xor
{
    uint Reader::readLength()
    {
        auto shortLength = read<uint8_t>();
        uint length;

        if (shortLength == 255)
        {
            length = read<uint>();
        }
        else
        {
            length = shortLength;
        }

        return length;
    }

    Span<const uint8_t> Reader::readBlob()
    {
        uint length = readLength();
        checkBounds(length);
        Span<const uint8_t> blob(m_cursor, m_cursor + length);
        m_cursor += length;
        return blob;
    }

    StringView Reader::readString()
    {
        uint length = readLength();
        checkBounds(length);
        StringView str(reinterpret_cast<const char *>(m_cursor),
                       reinterpret_cast<const char *>(m_cursor + length));
        m_cursor += length;
        return str;
    }
}
