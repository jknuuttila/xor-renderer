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

    struct Rect
    {
        int2 leftTop           = 0;
        int2 rightBottom       = 0;

        Rect() = default;
        Rect(int2 leftTop, int2 rightBottom = 0)
            : leftTop(leftTop)
            , rightBottom(rightBottom)
        {}

        bool empty() const
        {
            auto s = size();
            return s.x == 0 || s.y == 0;
        }

        uint2 size() const
        {
            return uint2(max(leftTop, rightBottom) - leftTop);
        }
    };

    struct ImageRect : public Rect
    {
        Subresource subresource = 0;

        ImageRect() = default;
        ImageRect(int2 leftTop)
            : Rect(leftTop)
        {}
        ImageRect(int2 leftTop, int2 rightBottom, Subresource subresource = 0)
            : Rect(leftTop, rightBottom)
            , subresource(subresource)
        {}
        ImageRect(int2 leftTop, Subresource subresource)
            : Rect(leftTop)
            , subresource(subresource)
        {}
    };

    struct ImageData
    {
        Span<const uint8_t> data;
        Format format;
        uint2 size;
        uint pitch     = 0;
        uint pixelSize = 0;

        ImageData &setDefaultSizes()
        {
            pitch     = format.rowSizeBytes(size.x);
            pixelSize = format.size();
            return *this;
        }

        template <typename T>
        const T &pixel(uint2 coords) const
        {
            // TODO: Block compressed formats
            uint offset =
                coords.y * pitch +
                coords.x * pixelSize;

            return reinterpret_cast<const T &>(data[offset]);
        }

        size_t sizeBytes() const
        {
            return static_cast<size_t>(size.y) * pitch;
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
