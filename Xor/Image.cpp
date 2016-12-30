#include "Xor/Image.hpp"
#include "Xor/Format.hpp"
#include "Core/Core.hpp"

#include "external/FreeImage/FreeImage.h"
#include "external/Compressonator/Compressonator.h"

namespace xor
{
    struct ImageHeader
    {
        static const uint VersionNumber = 1;

        uint2 size;
        uint mipLevels = 0;
        uint arraySize = 0;
        Format format;
    };

    struct SubresourceHeader
    {
        static const uint VersionNumber = 1;

        uint2 size;
        uint pitch = 0;
    };

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
            case 16:
                return Format(DXGI_FORMAT_R16_UNORM);
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
        ImageData imageData() const;
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

    uint computePitch(Format format, uint2 size)
    {
        auto rowLength = format.areaSizeBytes(size.x);
        auto pitch     = roundUpToMultiple<uint>(rowLength, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        return pitch;
    }

    static bool isValidPitch(uint pitch)
    {
        return pitch >= D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
            && (pitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) == 0;
    }

    static int computeMipAmount(uint2 size)
    {
        uint maxDim     = std::max(size.x, size.y);
        float logDim    = std::log2(static_cast<float>(maxDim)); 
        float extraMips = std::ceil(logDim);
        return static_cast<int>(extraMips) + 1;
    }

    static Format defaultCompressedFormat(Format format)
    {
        switch (format.dxgiFormat())
        {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return DXGI_FORMAT_BC3_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_BC3_UNORM_SRGB;
        default:
            XOR_CHECK(false, "No default compressed format defined");
            __assume(0);
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

    Image::Image(const Info &info)
    {
        if (!info.blob.empty())
            loadFromBlob(info);
        else if (info.filename)
            loadFromFile(info);
        else
            XOR_CHECK(false, "Invalid Image creation parameters");
    }

    Image::Image(const ImageData & sourceData)
    {
        m_state = std::make_shared<State>();
        m_state->arraySize = 1;
        m_state->mipLevels = 1;
        m_state->subresources.resize(1);

        auto &sr = m_state->subresources[0];
        sr.data.resize(sourceData.data.sizeBytes());
        sr.format = sourceData.format;
        sr.size   = sourceData.size;
        sr.pitch  = sourceData.pitch;

        memcpy(sr.data.data(), sourceData.data.data(), sr.data.sizeBytes());
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

    size_t Image::sizeBytes() const
    {
        size_t totalSize = 0;
        for (auto &s : m_state->subresources)
            totalSize += s.imageData().sizeBytes();
        return totalSize;
    }

    ImageData Image::imageData() const
    {
        XOR_ASSERT(mipLevels() == 1 && arraySize() == 1,
                   "Use subresource() for images with many subresources");
        return subresource(0);
    }

    ImageData Image::subresource(Subresource sr) const
    {
        return m_state->subresources[sr.index(m_state->mipLevels)].imageData();
    }

    std::vector<ImageData> Image::allSubresources() const
    {
        std::vector<ImageData> srs;
        srs.reserve(m_state->subresources.size());

        for (auto &s : m_state->subresources)
            srs.emplace_back(s.imageData());

        return srs;
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

        if (!dstFormat)
            dstFormat = defaultCompressedFormat(format());

        compressed.m_state            = std::make_shared<Image::State>();
        compressed.m_state->mipLevels = m_state->mipLevels;
        compressed.m_state->arraySize = m_state->arraySize;

        compressed.m_state->subresources.resize(m_state->subresources.size());

        for (uint s = 0; s < m_state->subresources.size(); ++s)
        {
            auto &src = m_state->subresources[s];

            if (any(src.size < dstFormat.blockSize()))
            {
                // TODO: Texture arrays
                log("Image", "Cutting mip levels at %u because of block size\n", s);
                compressed.m_state->mipLevels = s;
                compressed.m_state->subresources.resize(s);
                break;
            }

            auto &dst = compressed.m_state->subresources[s];

            CompressionTexture srcCmp(src.imageData());
            CompressionTexture dstCmp(src.size, dstFormat);

            CMP_CompressOptions options = {};
            options.dwSize              = sizeof(options);
            // Use maximum quality, both for actual quality and because
            // Compressonator crashes on x64 without it.
            options.fquality            = 1;

            log("Image", "Compressing %u x %u (%u bytes) into %u bytes\n",
                src.size.x, src.size.y,
                static_cast<uint>(srcCmp.data.sizeBytes()),
                static_cast<uint>(dstCmp.data.sizeBytes()));

            auto error = CMP_ConvertTexture(&srcCmp.cmpTex, &dstCmp.cmpTex, &options,
                                            nullptr, 0, 0);
            XOR_CHECK(error == CMP_OK, "Texture compression failed");

            dst.format = dstFormat;
            dst.size   = src.size;

            uint rowSize = dst.format.areaSizeBytes(dst.size.x);

            // If the row size (i.e. one row of compressed blocks) is
            // a valid pitch, we can use the buffer as-is.
            if (isValidPitch(rowSize))
            {
                dst.data  = std::move(dstCmp.data);
                dst.pitch = rowSize;
            }
            // Otherwise, we have to insert padding to cope with D3D12
            // requirements.
            else
            {
                uint rows = divRoundUp(dst.size.y, dst.format.blockSize());

                dst.pitch = computePitch(dst.format, dst.size);
                dst.data.resize(rows * dst.pitch);

                auto dstData = dst.data.data();
                auto srcData = dstCmp.data.data();

                for (uint i = 0; i < rows; ++i)
                {
                    memcpy(dstData, srcData, rowSize);
                    dstData += dst.pitch;
                    srcData += rowSize;
                }
            }
        }

        return compressed;
    }

    DynamicBuffer<uint8_t> Image::serialize() const
    {
        size_t sizeEstimate = sizeof(ImageHeader) + 16;
        for (auto &s : m_state->subresources)
            sizeEstimate += s.data.sizeBytes() + sizeof(SubresourceHeader) + 16;

        DynamicBuffer<uint8_t> blob;
        auto blobWriter = makeWriter(blob, sizeEstimate);

        ImageHeader header;
        header.size      = size();
        header.mipLevels = mipLevels();
        header.arraySize = arraySize();
        header.format    = format();

        blobWriter.writeStruct(header);

        for (uint i = 0; i < header.arraySize; ++i)
        {
            for (uint m = 0; m < header.mipLevels; ++m)
            {
                auto &s = subresource(Subresource(m, i).index(header.mipLevels));

                SubresourceHeader srHeader;
                srHeader.size  = s.size;
                srHeader.pitch = s.pitch;

                blobWriter.writeStruct(srHeader);
                blobWriter.writeBlob(s.data);
            }
        }

        return blob;
    }

    void Image::loadFromFile(const Info & info)
    {
        auto fiFormat = FreeImage_GetFileType(info.filename.cStr());

        Timer loadTime;

        if (fiFormat != FIF_UNKNOWN)
        {
            loadUsingFreeImage(info);
        }
        else
        {
            auto ext = String(info.filename.path().extension().c_str()).lower();

            if (ext == ".flt")
            {
                loadGridFloat(info);
            }
            else
            {
                XOR_CHECK(false, "Unknown file format \"%s\"", ext.cStr());
            }
        }

        log("Image", "Loaded image \"%s\" in %.2f ms (%.2f MB / s)\n",
            info.filename.cStr(),
            loadTime.milliseconds(),
            loadTime.bandwidthMB(sizeBytes()));
    }

    void Image::loadFromBlob(const Info & info)
    {
        Reader blobReader(info.blob);

        auto header = blobReader.readStruct<ImageHeader>();
        m_state = std::make_shared<State>();
        m_state->mipLevels = header.mipLevels;
        m_state->arraySize = header.arraySize;

        m_state->subresources.resize(mipLevels() * arraySize());

        for (uint i = 0; i < header.arraySize; ++i)
        {
            for (uint m = 0; m < header.mipLevels; ++m)
            {
                auto &s = m_state->subresources[Subresource(m, i).index(header.mipLevels)];

                auto srHeader = blobReader.readStruct<SubresourceHeader>();
                s.size        = srHeader.size;
                s.pitch       = srHeader.pitch;
                s.format      = header.format;

                auto data = blobReader.readBlob();
                s.data.resize(data.sizeBytes());
                memcpy(s.data.data(), data.data(), data.sizeBytes());
            }
        }
    }

    void Image::loadUsingFreeImage(const Info & info)
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

        if (info.compress)
        {
            Image uncompressed = std::move(*this);
            Image compressed   = uncompressed.compress(info.compressFormat);
            *this              = std::move(compressed);
        }
    }

    void Image::loadGridFloat(const Info & info)
    {
        auto hdrFilename = String(info.filename.path().replace_extension(".hdr"));

        uint2 imageSize;
        {
            auto header = File(hdrFilename).readText().lower();

            for (auto &&l : header.lines())
            {
                auto fields = l.splitNonEmpty();
                if (fields[0] == "ncols")
                    imageSize.x = std::stoul(fields[1].stdString());
                else if (fields[0] == "nrows")
                    imageSize.y = std::stoul(fields[1].stdString());
            }
        }

        XOR_CHECK(all(imageSize > 0), "Could not determine GridFloat file dimensions");
        m_state = std::make_shared<State>();
        m_state->arraySize = 1;
        m_state->mipLevels = 1;
        m_state->subresources.resize(1);

        auto &s  = m_state->subresources.front();
        s.format = DXGI_FORMAT_R32_FLOAT;
        s.size   = imageSize;
        s.pitch  = computePitch(s.format, s.size);
        s.data.resize(s.pitch * s.size.y);

        // Since the GridFloat file is tightly packed and we need to respect
        // pitch requirements, read it in line by line.
        auto rowBytes = s.format.areaSizeBytes(s.size.x);

        File dataFile(info.filename);
        XOR_CHECK(dataFile.size() >= s.format.areaSizeBytes(imageSize),
                  "Data file unexpectedly small");

        for (uint y = 0; y < s.size.y; ++y)
            dataFile.read(s.scanline(y)(0, rowBytes));
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

        auto fiScanline = [&] (uint y)
        {
            return FreeImage_GetScanLine(fiBmp, size.y - y - 1);
        };

        if (bpp == 24)
        {
            for (uint y = 0; y < size.y; ++y)
                copyAndSwizzle<4, 3, 3, uint8_t, uint8_t>(scanline(y).data(), fiScanline(y), size.x);
        }
        else if (bpp == 32)
        {
            for (uint y = 0; y < size.y; ++y)
                copyAndSwizzle<4, 4, 4, uint8_t, uint8_t>(scanline(y).data(), fiScanline(y), size.x);
        }
        else if (bpp == 16)
        {
            for (uint y = 0; y < size.y; ++y)
                memcpy(scanline(y).data(), fiScanline(y), size.x * sizeof(uint16_t));
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

    ImageData ImageSubresource::imageData() const
    {
        ImageData data;
        data.format    = format;
        data.size      = size;
        data.pitch     = pitch;
        data.pixelSize = data.format.size();
        data.data      = asConstSpan(this->data);
        return data;
    }
}
