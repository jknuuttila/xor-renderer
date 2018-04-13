#include "Core/Error.hpp"
#include "Core/Compression.hpp"
#include "Core/Utils.hpp"

#include "external/zstd-1.0.0/lib/zstd.h"

#include <algorithm>

namespace Xor
{
    static const int DefaultCompressionLevel = 20;

    size_t compressZstd(Span<uint8_t> compressed, Span<const uint8_t> src, int compressionLevel)
    {
        int maxLevel = ZSTD_maxCLevel();

        if (compressionLevel < 0)
            compressionLevel = DefaultCompressionLevel;

        compressionLevel = std::min(compressionLevel, maxLevel);

        return ZSTD_compress(compressed.data(), compressed.size(),
                             src.data(), src.size(),
                             compressionLevel);
    }

    DynamicBuffer<uint8_t> compressZstd(Span<const uint8_t> src, int compressionLevel)
    {
        DynamicBuffer<uint8_t> compressed;
        compressed.resize(ZSTD_compressBound(src.size()));

        Timer compressionTime;
        auto retval = compressZstd(compressed, src, compressionLevel);

        if (ZSTD_isError(retval))
            XOR_THROW(false, CompressionException, "ZSTD compression failed: %s", ZSTD_getErrorName(retval));

        compressed.resize(retval);

        log("Compression", "    Zstd compression: %.2f ms (%zu -> %zu, compression ratio: %.2f)\n",
            compressionTime.milliseconds(),
            src.sizeBytes(),
            compressed.sizeBytes(),
            static_cast<double>(src.sizeBytes()) / static_cast<double>(compressed.sizeBytes()));

        return compressed;
    }

    size_t decompressZstd(Span<uint8_t> decompressed, Span<const uint8_t> compressed)
    {
        return ZSTD_decompress(decompressed.data(), decompressed.size(),
                               compressed.data(), compressed.size());
    }

    DynamicBuffer<uint8_t> decompressZstd(size_t decompressedSize, Span<const uint8_t> compressed)
    {
        DynamicBuffer<uint8_t> decompressed(decompressedSize);
        auto retval = decompressZstd(decompressed, compressed);

        if (ZSTD_isError(retval))
        {
            XOR_THROW(false, CompressionException, "ZSTD decompression failed: %s", ZSTD_getErrorName(retval));
        }

        return decompressed;
    }
}
