#pragma once

#include "Core/Core.hpp"
#include "Xor/Format.hpp"

#include <memory>

namespace xor
{
    struct Subresource
    {
        uint mip   = 0;
        uint slice = 0;

        Subresource(uint mip = 0, uint slice = 0)
            : mip(mip)
            , slice(slice)
        {}

        uint index(uint mipLevels) const
        {
            return slice * mipLevels + mip;
        }
    };

    struct ImageRect
    {
        uint2 leftTop           = 0;
        uint2 rightBottom       = 0;
        Subresource subresource = 0;

        ImageRect() = default;
        ImageRect(uint2 leftTop)
            : leftTop(leftTop)
        {}
        ImageRect(uint2 leftTop, uint2 rightBottom, Subresource subresource = 0)
            : leftTop(leftTop)
            , rightBottom(rightBottom)
            , subresource(subresource)
        {}
        ImageRect(uint2 leftTop, Subresource subresource)
            : leftTop(leftTop)
            , subresource(subresource)
        {}

        bool empty() const
        {
            auto s = size();
            return s.x == 0 || s.y == 0;
        }

        uint2 size() const
        {
            return max(leftTop, rightBottom) - leftTop;
        }
    };

    struct ImageData
    {
        Span<const uint8_t> data;
        Format format;
        uint2 size;
        uint pitch     = 0;
        uint pixelSize = 0;

        template <typename T>
        const T &pixel(uint2 coords) const
        {
            // TODO: Block compressed formats
            uint offset =
                coords.y * pitch +
                coords.x * pixelSize;

            return reinterpret_cast<const T &>(data[offset]);
        }
    };

    class Image
    {
        struct State;
        std::shared_ptr<State> m_state;

        void load(const String &filename, Format format = Format());
    public:
        Image() = default;
        Image(const String &filename);

        explicit operator bool() const { return !!m_state; }

        uint2 size() const;
        Format format() const;
        ImageData subresource(Subresource sr) const;
    };
}
