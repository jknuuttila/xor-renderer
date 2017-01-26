#ifndef LOADBALANCEDSHADER_SIG_H
#define LOADBALANCEDSHADER_SIG_H

#include "Xor/Shaders.h"
#include "LoadBalancingDefs.h"

XOR_BEGIN_SIGNATURE(LoadBalancedShader)

XOR_CBUFFER(Constants, 0)
{
    uint size;
};

XOR_SRV(ByteAddressBuffer,   input,         0)
XOR_UAV(RWByteAddressBuffer, output,        0)
XOR_UAV(RWByteAddressBuffer, outputCounter, 1)

XOR_THREADGROUP_SIZE_2D(LBThreadGroupSize, 1)

XOR_END_SIGNATURE

#define LOADBALANCEDSHADER_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CSU(1, 1, 2)

#endif