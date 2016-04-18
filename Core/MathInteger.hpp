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
}
