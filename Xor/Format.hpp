#pragma once

#include "Core/Core.hpp"

#include <d3d12.h>

namespace xor
{
    class Format
    {
        // Use a trick with anonymous unions and structs to
        // keep the class 32 bits long, with two 16-bit fields,
        // but see the DXGI_FORMAT value in the debugger.
        union
        {
            struct
            {
                uint16_t m_dxgiFormat;
                uint16_t m_elementSize;
            };
            DXGI_FORMAT m_asDxgiFormat; // This is shown by the debugger in symbolic form
        };
    public:
        Format(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN)
            : m_dxgiFormat(static_cast<uint16_t>(format))
            , m_elementSize(0)
        {}

        static Format structure(size_t structSize)
        {
            Format f;
            XOR_ASSERT(structSize <= std::numeric_limits<uint16_t>::max(),
                       "Struct sizes above 64k not supported.");
            f.m_dxgiFormat  = DXGI_FORMAT_UNKNOWN;
            f.m_elementSize = static_cast<uint16_t>(structSize);
            return f;
        }
        template <typename T>
        static Format structure() { return structure(sizeof(T)); }

        DXGI_FORMAT dxgiFormat() const
        {
            return static_cast<DXGI_FORMAT>(m_dxgiFormat);
        }

        explicit operator bool() const
        {
            return (dxgiFormat() != DXGI_FORMAT_UNKNOWN) || m_elementSize;
        }

        operator DXGI_FORMAT() const { return dxgiFormat(); }

        uint size() const;
        uint areaSizeBytes(uint2 area) const;
        uint areaSizeBytes(uint width) const { return areaSizeBytes({ width, 1 }); }
        uint blockSize() const;
        uint blockBytes() const;

        bool isDepthFormat() const;
        bool isCompressed() const;

        Format asStructure() const;
    };

}

