#include "Xor/Image.hpp"
#include "Xor/Format.hpp"
#include "Core/Core.hpp"

#include "external/FreeImage/FreeImage.h"

namespace xor
{
    struct FiBitmap
    {
        MovingPtr<FIBITMAP *> bmp;

        FiBitmap(FIBITMAP *bmp = nullptr) : bmp(bmp) {}

        FiBitmap(FiBitmap &&) = default;
        FiBitmap &operator=(FiBitmap &&) = default;

        FiBitmap(const FiBitmap &) = delete;
        FiBitmap &operator=(const FiBitmap &) = delete;

        ~FiBitmap()
        {
            reset();
        }

        explicit operator bool() const { return !!bmp; }
        operator FIBITMAP *() { return bmp; }

        void reset(FIBITMAP *b = nullptr)
        {
            if (bmp)
            {
                FreeImage_Unload(bmp);
                bmp = b;
            }
        }

        Format format()
        {
            auto bpp = FreeImage_GetBPP(bmp);
            switch (bpp)
            {
            case 24:
            case 32:
                return Format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
            default:
                XOR_CHECK(false, "Unknown bits-per-pixel value");
                __assume(0);
            }
        }
    };

    struct ImageSubresource
    {
        FiBitmap fiBmp;
        DynamicBuffer<uint8_t> data;
        uint2 size;
        uint pitch = 0;
        Format format;

        Span<uint8_t> scanline(uint y);
        void importFrom(FiBitmap bmp);
    };

    struct Image::State
    {
        std::vector<ImageSubresource> subresources;
        uint mipLevels = 0;
        uint arraySize = 0;
    };

    static int defaultFlagsForFormat(FREE_IMAGE_FORMAT format)
    {
        switch (format)
        {
        case FIF_PNG:
            return PNG_IGNOREGAMMA;
        default:
            return 0;
        }
    }

    static uint computePitch(Format format, uint2 size)
    {
        auto rowLength = format.rowSizeBytes(size.x);
        auto pitch     = roundUpToMultiple<uint>(rowLength, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        return pitch;
    }

    static int computeMipAmount(uint2 size)
    {
        uint maxDim     = std::max(size.x, size.y);
        float logDim    = std::log2(static_cast<float>(maxDim)); 
        float extraMips = std::ceil(logDim);
        return static_cast<int>(extraMips) + 1;
    }

    template <typename T> constexpr T opaqueAlpha()
    {
        return std::is_floating_point<T>::value
            ? static_cast<T>(1)
            : static_cast<T>(-1LL);
    }

    template <
        uint Channel,
        uint DstChannels,
        uint UsedSrcChannels,
        typename Dst,
        typename Src
    >
    static void swizzlePixel(Dst *dst, const Src *src)
    {
        static const int SrcChannelIndexes[] = {
            FI_RGBA_RED,
            FI_RGBA_GREEN,
            FI_RGBA_BLUE,
            FI_RGBA_ALPHA,
        };

        if (DstChannels > Channel)
        {
            if (UsedSrcChannels > Channel)
                dst[Channel] = static_cast<Dst>(src[SrcChannelIndexes[Channel]]);
            else if (Channel == 3)
                dst[Channel] = opaqueAlpha<Dst>();
            else 
                dst[Channel] = 0;
        }
    }

    template <
        uint DstChannels,
        uint SrcChannels,
        uint UsedSrcChannels,
        typename Dst,
        typename Src
    >
    static void copyAndSwizzle(uint8_t *pDst, const uint8_t *pSrc, uint pixels)
    {
        auto dst = reinterpret_cast<Dst *>(pDst);
        auto src = reinterpret_cast<const Src *>(pSrc);

        for (uint i = 0; i < pixels; ++i)
        {
            auto dstPixel = &dst[i * DstChannels];
            auto srcPixel = &src[i * SrcChannels];

            swizzlePixel<0, DstChannels, UsedSrcChannels>(dstPixel, srcPixel);
            swizzlePixel<1, DstChannels, UsedSrcChannels>(dstPixel, srcPixel);
            swizzlePixel<2, DstChannels, UsedSrcChannels>(dstPixel, srcPixel);
            swizzlePixel<3, DstChannels, UsedSrcChannels>(dstPixel, srcPixel);
        }
    }

    Image::Image(const Info &info)
    {
        auto fiFormat = FreeImage_GetFileType(info.filename.cStr());
        auto flags  = defaultFlagsForFormat(fiFormat);

        FiBitmap bmp = FreeImage_Load(fiFormat, info.filename.cStr(), flags);

        XOR_CHECK(!!bmp, "Failed to load \"%s\"", info.filename.cStr());

        m_state = std::make_shared<State>();
        m_state->arraySize = 1;

        m_state->subresources.resize(1);
        m_state->subresources[0].importFrom(std::move(bmp));

        if (info.generateMipmaps != Info::NoMipmaps)
        {
            int mipmaps = info.generateMipmaps == Info::AllMipmaps
                ? computeMipAmount(size())
                : info.generateMipmaps;

            XOR_CHECK(mipmaps > 0, "Invalid mipmap count");

            m_state->mipLevels = static_cast<uint>(mipmaps);
        }
        else
        {
            m_state->mipLevels = 1;
        }

        m_state->subresources.resize(m_state->mipLevels);

        for (uint m = 1; m < m_state->mipLevels; ++m)
        {
            auto &prev = m_state->subresources[m - 1];
            auto &cur  = m_state->subresources[m];

            int2 size = int2(prev.size) / 2;
            size      = max(1, size);

            cur.importFrom(FreeImage_Rescale(prev.fiBmp, size.x, size.y));
        }

        for (auto &s : m_state->subresources)
            s.fiBmp.reset();
    }

    uint2 Image::size() const
    {
        return m_state->subresources[0].size;
    }

    Format Image::format() const
    {
        return m_state->subresources[0].format;
    }

    uint Image::mipLevels() const
    {
        return m_state->mipLevels;
    }

    uint Image::arraySize() const
    {
        return m_state->arraySize;
    }

    ImageData Image::subresource(Subresource sr) const
    {
        auto &s = m_state->subresources[sr.index(m_state->mipLevels)];

        ImageData data;
        data.format    = s.format;
        data.size      = s.size;
        data.pitch     = s.pitch;
        data.pixelSize = data.format.size();
        data.data      = asConstSpan(s.data);
        return data;
    }

    Span<uint8_t> ImageSubresource::scanline(uint y)
    {
        return Span<uint8_t>(data.data() + y * pitch, pitch);
    }

    void ImageSubresource::importFrom(FiBitmap bmp)
    {
        fiBmp = std::move(bmp);

        format = fiBmp.format();
        size.x = FreeImage_GetWidth(fiBmp);
        size.y = FreeImage_GetHeight(fiBmp);
        pitch  = computePitch(format, size);
        data   = DynamicBuffer<uint8_t>(size.y * pitch);

        auto bpp = FreeImage_GetBPP(fiBmp);

        if (bpp == 24)
        {
            for (uint y = 0; y < size.y; ++y)
                copyAndSwizzle<4, 3, 3, uint8_t, uint8_t>(scanline(y).data(), FreeImage_GetScanLine(fiBmp, size.y - y - 1), size.x);
        }
        else if (bpp == 32)
        {
            for (uint y = 0; y < size.y; ++y)
                copyAndSwizzle<4, 4, 4, uint8_t, uint8_t>(scanline(y).data(), FreeImage_GetScanLine(fiBmp, size.y - y - 1), size.x);
        }
        else
        {
            XOR_CHECK(false, "Unknown bits-per-pixel value.");
            __assume(0);
        }
    }
}
