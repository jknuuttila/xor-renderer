#ifndef $safeitemname$_SIG_H
#define $safeitemname$_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE($safeitemname$)

XOR_CBUFFER(Constants, 0)
{
    uint dummy;
};

XOR_END_SIGNATURE

#define $safeitemname$_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_C(1)

#endif

