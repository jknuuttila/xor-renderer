#pragma once

#include "Core/Utils.hpp"

namespace xor
{
    size_t compressZstd(Span<uint8_t> compressed, Span<const uint8_t> src, int compressionLevel = -1);
    DynamicBuffer<uint8_t> compressZstd(Span<const uint8_t> src, int compressionLevel = -1);

    size_t decompressZstd(Span<uint8_t> decompressed, Span<const uint8_t> compressed);
    DynamicBuffer<uint8_t> decompressZstd(size_t decompressedSize, Span<const uint8_t> compressed);
}

