#include "Xor/Image.hpp"
#include "Xor/Format.hpp"
#include "Core/Core.hpp"

#include "external/FreeImage/FreeImage.h"
#include "external/Compressonator/Compressonator.h"

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

        Span<uint8_t> scanline(uint y);
        void importFrom(FiBitmap bmp);
        ImageData imageData(Format format) const;
    };

    struct Image::State
    {
        std::vector<ImageSubresource> subresources;
        uint mipLevels = 0;
        uint arraySize = 0;
        Format format;
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
        auto rowLength = format.areaSizeBytes({size.x, 1});
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
        m_state->format    = bmp.format();

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
        return m_state->format;
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
        return m_state->subresources[sr.index(m_state->mipLevels)].imageData(format());
    }

    struct CompressionTexture
    {
        DynamicBuffer<uint8_t> data;
        CMP_Texture cmpTex;

        CompressionTexture()
        {
            memset(&cmpTex, 0, sizeof(cmpTex));
            cmpTex.dwSize = sizeof(cmpTex);
        }

        CompressionTexture(const ImageData &img)
            : CompressionTexture()
        {
            cmpTex.dwWidth      = img.size.x;
            cmpTex.dwHeight     = img.size.y;
            cmpTex.dwPitch      = img.pitch;
            cmpTex.nBlockWidth  = 1;
            cmpTex.nBlockHeight = 1;
            cmpTex.nBlockDepth  = 1;
            cmpTex.format       = cmpFormat(img.format);

            allocate(img.size.y * img.pitch);

            // Copy the data, then transcode pixels as necessary
            memcpy(data.data(), img.data.data(),
                   std::min(data.sizeBytes(), img.data.sizeBytes()));

            switch (cmpTex.format)
            {
            case CMP_FORMAT_ARGB_8888:
                convertRGBAToBGRA();
                break;
            default:
                // Default is no transcoding
                break;
            }
        }

        CompressionTexture(uint2 size, Format format)
            : CompressionTexture()
        {
            cmpTex.dwWidth      = size.x;
            cmpTex.dwHeight     = size.y;
            cmpTex.format       = cmpFormat(format);

            if (format.isCompressed())
                cmpTex.dwPitch = 0;
            else
                cmpTex.dwPitch = computePitch(format, size);

            cmpTex.nBlockWidth  = format.blockSize();
            cmpTex.nBlockHeight = format.blockSize();
            cmpTex.nBlockDepth  = 1;

            allocate();
        }

        void allocate(uint size = 0)
        {
            if (size == 0)
                size = CMP_CalculateBufferSize(&cmpTex);

            data.resize(size);
            cmpTex.dwDataSize = size;
            cmpTex.pData      = data.data();
        }

        static CMP_FORMAT cmpFormat(Format format)
        {
            switch (format.dxgiFormat())
            {
            case DXGI_FORMAT_R8G8B8A8_SINT:
            case DXGI_FORMAT_R8G8B8A8_SNORM:
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UINT:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                return CMP_FORMAT_ARGB_8888;
            case DXGI_FORMAT_BC1_TYPELESS:
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
                return CMP_FORMAT_BC1;
            case DXGI_FORMAT_BC3_TYPELESS:
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
                return CMP_FORMAT_BC3;
            default:
                XOR_CHECK(false, "Unsupported format");
                __assume(0);
            }
        }

        void convertRGBAToBGRA()
        {
            for (auto &px : reinterpretSpan<uint32_t>(data))
            {
                auto abgr = _byteswap_ulong(px);
                px = (abgr >> 8) | (abgr << 24);
            }
        }
    };

    Image Image::compress(Format dstFormat) const
    {
        Image compressed;

        compressed.m_state            = std::make_shared<Image::State>();
        compressed.m_state->format    = dstFormat;
        compressed.m_state->mipLevels = m_state->mipLevels;
        compressed.m_state->arraySize = m_state->arraySize;

        compressed.m_state->subresources.resize(m_state->subresources.size());

        for (size_t i = 0; i < m_state->subresources.size(); ++i)
        {
            auto &src = m_state->subresources[i];
            auto &dst = compressed.m_state->subresources[i];

            CompressionTexture srcCmp(src.imageData(format()));
            CompressionTexture dstCmp(src.size, dstFormat);

            CMP_CompressOptions options = {};
            options.dwSize              = sizeof(options);
            // Use maximum quality, both for actual quality and because
            // Compressonator crashes on x64 without it.
            options.fquality            = 1;

            auto error = CMP_ConvertTexture(&srcCmp.cmpTex, &dstCmp.cmpTex, &options,
                                            nullptr, 0, 0);
            XOR_CHECK(error == CMP_OK, "Texture compression failed");
        }
    }

    Span<uint8_t> ImageSubresource::scanline(uint y)
    {
        return Span<uint8_t>(data.data() + y * pitch, pitch);
    }

    void ImageSubresource::importFrom(FiBitmap bmp)
    {
        fiBmp = std::move(bmp);

        size.x = FreeImage_GetWidth(fiBmp);
        size.y = FreeImage_GetHeight(fiBmp);
        pitch  = computePitch(fiBmp.format(), size);
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

#if 0
        static bool tryComp = true;
        if (tryComp)
        {
            print("COMPRESSING\n");

            CMP_Texture src = {};
            CMP_Texture dst = {};

            src.dwSize       = sizeof(src);
            src.dwWidth      = size.x;
            src.dwHeight     = size.y;
            src.dwPitch      = pitch;
            src.format       = CMP_FORMAT_ARGB_8888;
            src.nBlockWidth  = 1;
            src.nBlockHeight = 1;
            src.nBlockDepth  = 1;
            src.dwDataSize   = data.sizeBytes();
            src.pData        = data.data();

            auto swiz = reinterpretSpan<uint32_t>(data);
            for (auto &px : swiz)
                px = _byteswap_ulong(px) >> 8;

            dst.dwSize       = sizeof(dst);
            dst.dwWidth      = size.x;
            dst.dwHeight     = size.y;
            dst.dwPitch      = 0;
            dst.format       = CMP_FORMAT_BC1;
            dst.nBlockWidth  = 4;
            dst.nBlockHeight = 4;
            dst.nBlockDepth  = 1;

            CMP_CompressOptions opts = {};
            opts.dwSize = sizeof(opts);
            opts.fquality = 1;

            DynamicBuffer<uint8_t> dstData(CMP_CalculateBufferSize(&dst));
            dst.dwDataSize   = dstData.sizeBytes();
            dst.pData        = dstData.data();

            auto err = CMP_ConvertTexture(&src, &dst, &opts,
                                          nullptr, 0, 0);
            XOR_CHECK(err == CMP_OK, "halp");

            pitch = size.x * 2;
            format = DXGI_FORMAT_BC1_UNORM_SRGB;
            data = std::move(dstData);

            tryComp = false;
        }
#endif
    }

    ImageData ImageSubresource::imageData(Format format) const
    {
        ImageData data;
        data.format    = format;
        data.size      = size;
        data.pitch     = pitch;
        data.pixelSize = data.format.size();
        data.data      = asConstSpan(data);
        return data;
    }
}
