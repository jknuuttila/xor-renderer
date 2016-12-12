#pragma once

#include <cstdint>
#include <intrin.h>
#include <nmmintrin.h>

namespace xor
{
    // Function names try to match HLSL, where applicable.
    inline int64_t firstbitlow(uint64_t value)
    {
        unsigned long index;
        if (_BitScanForward64(&index, value))
            return static_cast<int64_t>(index);
        else
            return -1;
    }

    inline uint countTrailingZeros(uint64_t value)
    {
        unsigned long index;
        if (_BitScanForward64(&index, value))
            return index;
        else
            return 64;
    }

    inline uint popCount(uint64_t value)
    {
        return static_cast<uint>(_mm_popcnt_u64(value));
    }

    template <typename T> T divRoundUp(T value, T divider)
    {
        return (value + (divider - 1)) / divider;
    }

    template <typename T> T roundUpToMultiple(T value, T multiplier)
    {
        return divRoundUp(value, multiplier) * multiplier;
    }

    template <typename T> T alignTo(T value, T alignment)
    {
        auto misalignment = value % alignment;
        auto offset       = (alignment - misalignment) % alignment;
        return value + offset;
    }

    template <typename T> T roundUpToPow2(T v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        if (sizeof(T) > 1) v |= v >> 8;
        if (sizeof(T) > 2) v |= v >> 16;
        if (sizeof(T) > 4) v |= v >> 32;
        v++;

        return v;
    }
}
