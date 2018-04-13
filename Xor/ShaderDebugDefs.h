#ifndef XOR_SHADERDEBUGDEFS_H
#define XOR_SHADERDEBUGDEFS_H

#ifdef __cplusplus
#include "Core/Math.hpp"

namespace Xor {
namespace backend {
#endif

    static const uint XorShaderDebugConstantCount = 3;

    struct XorShaderDebugConstants
    {
        int2 cursorPosition;
        uint eventNumber;
    };

    static const uint XorShaderDebugOpCodeMetadata    = 1;
    static const uint XorShaderDebugOpCodePrintValues = 2;
    static const uint XorShaderDebugOpCodeNewLine     = 3;
    static const uint XorShaderDebugOpCodeFeedback    = 4;

    static const uint XorShaderDebugTypeIdMask    = 0x30;
    static const uint XorShaderDebugTypeCountMask = 0x07;
    static const uint XorShaderDebugTypeId_float  = 0x00;
    static const uint XorShaderDebugTypeId_int    = 0x10;
    static const uint XorShaderDebugTypeId_uint   = 0x20;

    static const uint XorShaderDebugFeedbackOffset     = 0;
    static const uint XorShaderDebugWritePointerOffset = 16;
    static const uint XorShaderDebugWritePointerInit   = XorShaderDebugWritePointerOffset + 4;

#define XOR_SHADERDEBUG_TYPE_ID(type, count) (XorShaderDebugTypeId_ ## type | count)

#ifdef __cplusplus
}
}
#endif


#endif
