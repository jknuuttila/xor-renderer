#ifndef XOR_SHADERDEBUGDEFS_H
#define XOR_SHADERDEBUGDEFS_H

#ifdef __cplusplus
#include "Core/Math.hpp"

namespace xor {
namespace backend {
#endif

    struct XorShaderDebugConstants
    {
        int2 cursorPosition;
        uint commandListNumber;
        uint eventNumber;
    };

    static const uint XorShaderDebugPrintOpCodeMetadata    = 1;
    static const uint XorShaderDebugPrintOpCodePrintValues = 2;
    static const uint XorShaderDebugPrintOpCodeNewLine     = 3;

    static const uint XorShaderDebugTypeId_float = 0x00;
    static const uint XorShaderDebugTypeId_int   = 0x10;
    static const uint XorShaderDebugTypeId_uint  = 0x20;

#define XOR_SHADERDEBUG_TYPE_ID(type, amount) (XorShaderDebugTypeId_ ## type | amount)

#ifdef __cplusplus
}
}
#endif


#endif
