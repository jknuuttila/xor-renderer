#pragma once

#include <cstdint>
#include <intrin.h>

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
}
