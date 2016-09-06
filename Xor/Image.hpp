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
        Rect(int x, int y) : leftTop(x, y) {}
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
        ImageRect(int x, int y)
            : Rect(x, y)
        {}
        ImageRect(int2 leftTop, int2 rightBottom, Subresource subresource = 0)
            : Rect(leftTop, rightBottom)
            , subresource(subresource)
        {}
        ImageRect(int2 leftTop, Subresource subresource)
            : Rect(leftTop)
            , subresource(subresource)
        {}
        ImageRect(Subresource subresource)
            : subresource(subresource)
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

    namespace info
    {
        class ImageInfo
        {
        public:
            static const int NoMipmaps  = 0;
            static const int AllMipmaps = -1;

            String filename;
            int generateMipmaps = NoMipmaps;

            ImageInfo() = default;
            ImageInfo(const char *filename) : filename(filename) {}
            ImageInfo(String filename) : filename(std::move(filename)) {}
        };

        class ImageInfoBuilder : public ImageInfo
        {
        public:
            ImageInfoBuilder &filename(String filename) { ImageInfo::filename = std::move(filename); return *this; }
            ImageInfoBuilder &generateMipmaps(int mipmaps = AllMipmaps) { ImageInfo::generateMipmaps = mipmaps; return *this; }
        };
    }

    class Image
    {
        struct State;
        std::shared_ptr<State> m_state;
    public:
        using Info    = info::ImageInfo;
        using Builder = info::ImageInfoBuilder;

        Image() = default;
        Image(const Info &info);

        explicit operator bool() const { return !!m_state; }

        uint2 size() const;
        Format format() const;
        uint mipLevels() const;
        uint arraySize() const;
        ImageData subresource(Subresource sr) const;
        Image compress(Format dstFormat) const;
    };
}
