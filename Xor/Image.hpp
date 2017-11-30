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

    template <typename T>
    struct Rectangle
    {
        T min = 0;
        T max = 0;

        Rectangle() = default;
        Rectangle(typename T::Elem x, typename T::Elem y) : min(x, y) {}
        Rectangle(T min, T max = 0)
            : min(min)
            , max(max)
        {}

        static Rectangle withSize(T min, T size)
        {
            Rectangle rect;
            rect.min = min;
            rect.max = min + size;
            return rect;
        }

        static Rectangle withSize(T size)
        {
            return withSize(0, size);
        }

        bool empty() const
        {
            auto s = size();
            return s.x == 0 || s.y == 0;
        }

        T size() const
        {
            return math::max(min, max) - math::min(min, max);
        }
    };

    using Rect  = Rectangle<int2>;
    using RectF = Rectangle<float2>;

    struct ImageRect : public Rect
    {
        Subresource subresource = 0;

        ImageRect() = default;
        ImageRect(Rect rect)
            : Rect(rect)
        {}
        ImageRect(int2 min)
            : Rect(min)
        {}
        ImageRect(int x, int y)
            : Rect(x, y)
        {}
        ImageRect(int2 min, int2 max, Subresource subresource = 0)
            : Rect(min, max)
            , subresource(subresource)
        {}
        ImageRect(int2 min, Subresource subresource)
            : Rect(min)
            , subresource(subresource)
        {}
        ImageRect(Subresource subresource)
            : subresource(subresource)
        {}
    };

    uint computePitch(Format format, uint2 size);

    struct ImageData
    {
        Span<const uint8_t> data;
        Format format;
        uint2 size;
        uint pitch     = 0;
        uint pixelSize = 0;

        ImageData &setDefaultSizes()
        {
            pitch     = computePitch(format, size);
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

        uint2 areaOfPitch() const { return format.areaOfPitch(size.x, pitch); }
    };

    struct RWImageData : ImageData
    {
        Span<uint8_t> mutableData;
        DynamicBuffer<uint8_t> ownedData;

        RWImageData() = default;
        RWImageData(uint2 size, Format format)
        {
            this->size = size;
            this->format = format;
            setDefaultSizes();
            ownedData   = DynamicBuffer<uint8_t>(sizeBytes());
            mutableData = ownedData;
            data        = mutableData;
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
        Image(const ImageData &sourceData);

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
