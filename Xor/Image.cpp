#include "Xor/Image.hpp"
#include "Xor/Format.hpp"
#include "Core/Core.hpp"

#include "external/FreeImage/FreeImage.h"

namespace xor
{
    struct Image::State
    {
        DynamicBuffer<uint8_t> data;
        uint2 size;
        uint pitch = 0;
        Format format;

        Span<uint8_t> scanline(uint y);
        void importFrom(FIBITMAP *bmp);
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

    static Format computeFormat(FIBITMAP *bmp)
    {
        auto bpp = FreeImage_GetBPP(bmp);
        switch (bpp)
        {
        case 24:
        case 32:
            return Format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        default:
            XOR_CHECK(false, "Unknown bits-per-pixel value");
            return Format();
        }
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

    void Image::load(const String & filename, Format format)
    {
        auto fiFormat = FreeImage_GetFileType(filename.cStr());
        auto flags  = defaultFlagsForFormat(fiFormat);

        auto bmp = raiiPtr(FreeImage_Load(fiFormat, filename.cStr(), flags),
                           FreeImage_Unload);

        XOR_CHECK(!!bmp, "Failed to load \"%s\"", filename.cStr());

        m_state = std::make_shared<State>();

        if (format)
            m_state->format = format;
        else
            m_state->format = computeFormat(bmp.get());

        m_state->size.x = FreeImage_GetWidth(bmp.get());
        m_state->size.y = FreeImage_GetHeight(bmp.get());

        m_state->pitch  = computePitch(m_state->format, m_state->size);

        m_state->data   = DynamicBuffer<uint8_t>(m_state->size.y * m_state->pitch);
        m_state->importFrom(bmp.get());
    }

    Image::Image(const String & filename)
    {
        load(filename);
    }

    uint2 Image::size() const
    {
        return m_state->size;
    }

    Format Image::format() const
    {
        return m_state->format;
    }

    ImageData Image::subresource(Subresource sr) const
    {
        (void)sr;

        ImageData data;
        data.format    = format();
        data.size      = size();
        data.pitch     = m_state->pitch;
        data.pixelSize = data.format.size();
        data.data      = m_state->data;
        return data;
    }

    Span<uint8_t> Image::State::scanline(uint y)
    {
        return Span<uint8_t>(data.data() + y * pitch, pitch);
    }

    void Image::State::importFrom(FIBITMAP * bmp)
    {
        auto bpp = FreeImage_GetBPP(bmp);

        if (bpp == 24)
        {
            for (uint y = 0; y < size.y; ++y)
                copyAndSwizzle<4, 3, 3, uint8_t, uint8_t>(scanline(y).data(), FreeImage_GetScanLine(bmp, size.y - y - 1), size.x);
        }
        else if (bpp == 32)
        {
            for (uint y = 0; y < size.y; ++y)
                copyAndSwizzle<4, 4, 4, uint8_t, uint8_t>(scanline(y).data(), FreeImage_GetScanLine(bmp, size.y - y - 1), size.x);
        }
        else
        {
            XOR_CHECK(false, "Unknown bits-per-pixel value.");
        }
    }
}
