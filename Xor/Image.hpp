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

        static Subresource fromIndex(uint subresourceIndex, uint mipLevels)
        {
            Subresource sr;
            sr.mip   = subresourceIndex % mipLevels;
            sr.slice = subresourceIndex / mipLevels;
            return sr;
        }

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

        static Rect withSize(int2 leftTop, int2 size)
        {
            Rect rect;
            rect.leftTop     = leftTop;
            rect.rightBottom = leftTop + size;
            return rect;
        }

        static Rect withSize(int2 size)
        {
            return withSize(0, size);
        }

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
        ImageRect(Rect rect)
            : Rect(rect)
        {}
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
            pitch     = format.areaSizeBytes(size.x);
            pixelSize = format.size();
            return *this;
        }

        float2 normalized(int2 coords) const
        {
            return float2(coords) / float2(size);
        }

        float2 normalized(uint2 coords) const
        {
            return float2(coords) / float2(size);
        }

        uint2 unnormalized(float2 uv) const
        {
            return uint2(uv * float2(size));
        }

        template <typename T>
        const T &pixel(int2 coords) const
        {
            return pixel<T>(uint2(max(int2(0), coords)));
        }

        template <typename T>
        const T &pixel(uint2 coords) const
        {
			coords = min(coords, size - 1);

            // TODO: Block compressed formats
            uint offset =
                coords.y * pitch +
                coords.x * pixelSize;

            return reinterpret_cast<const T &>(data[offset]);
        }

        template <typename T>
        const T &pixel(float2 uv) const
        {
			uint2 coords = uint2(uv * float2(size));
			return pixel<T>(coords);
        }

        template <typename T>
        Span<const T> scanline(uint y) const
        {
            uint offset = y * pitch;
            uint length = format.areaSizeBytes(size.x);
            return reinterpretSpan<const T>(data(offset, offset + length));
        }

        template <typename T>
        Span<const T> scanline(int y) const { return scanline<T>(uint(y)); }

        size_t sizeBytes() const
        {
            return static_cast<size_t>(size.y) * pitch;
        }
    };

    struct RWImageData : ImageData
    {
        Span<uint8_t> mutableData;

        DynamicBuffer<uint8_t> createNewImage(uint2 size, Format format)
        {
            this->size = size;
            this->format = format;
            setDefaultSizes();
            DynamicBuffer<uint8_t> bytes(sizeBytes());
            mutableData = bytes;
            data = bytes;
            return bytes;
        }

        template <typename T>
        T &pixel(int2 coords)
        {
            return pixel<T>(uint2(max(int2(0), coords)));
        }

        template <typename T>
        T &pixel(uint2 coords)
        {
			coords = min(coords, size - 1);

            // TODO: Block compressed formats
            uint offset =
                coords.y * pitch +
                coords.x * pixelSize;

            return reinterpret_cast<T &>(mutableData[offset]);
        }

        template <typename T>
        T &pixel(float2 uv)
        {
			uint2 coords = uint2(uv * float2(size));
			return pixel<T>(coords);
        }

        template <typename T>
        Span<T> scanline(uint y)
        {
            uint offset = y * pitch;
            uint length = format.areaSizeBytes(size.x);
            return reinterpretSpan<T>(mutableData(offset, offset + length));
        }

        template <typename T>
        Span<T> scanline(int y) { return scanline<T>(uint(y)); }
    };

    namespace info
    {
        class ImageInfo
        {
        public:
            static const int NoMipmaps  = 0;
            static const int AllMipmaps = -1;

            String filename;
            Span<const uint8_t> blob;
            int generateMipmaps = NoMipmaps;
            bool compress = false;
            Format compressFormat;

            ImageInfo() = default;
            ImageInfo(const char *filename) : filename(filename) {}
            ImageInfo(String filename) : filename(std::move(filename)) {}
            ImageInfo(Span<const uint8_t> blob) : blob(blob) {}
        };

        class ImageInfoBuilder : public ImageInfo
        {
        public:
            ImageInfoBuilder &filename(String filename) { ImageInfo::filename = std::move(filename); return *this; }
            ImageInfoBuilder &blob(Span<const uint8_t> blob) { ImageInfo::blob = blob; return *this; }
            ImageInfoBuilder &generateMipmaps(int mipmaps = AllMipmaps) { ImageInfo::generateMipmaps = mipmaps; return *this; }
            ImageInfoBuilder &compress(Format compressFormat = Format()) { ImageInfo::compress = true; ImageInfo::compressFormat = compressFormat; return *this; }
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
        size_t sizeBytes() const;
        ImageData imageData() const;
        ImageData subresource(Subresource sr) const;
        std::vector<ImageData> allSubresources() const;
        Image compress(Format dstFormat = Format()) const;
        DynamicBuffer<uint8_t> serialize() const;

    private:
        void loadFromFile(const Info &info);
        void loadFromBlob(const Info &info);

        void loadUsingFreeImage(const Info &info);
        void loadGridFloat(const Info &info);
    };
}
