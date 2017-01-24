#include "LoadBalancedShader.sig.h"
#include "LoadBalancingDefs.h"

[RootSignature(LOADBALANCEDSHADER_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid : SV_DispatchThreadID)
{
    bool D = tid.x == 0;

    if (tid.x >= size)
        return;

    uint inputValue = input.Load(tid.x * 4);

    uint index = inputValue >> WorkItemCountBits;
    uint items = inputValue  & WorkItemCountMask;

    uint base;
    outputCounter.InterlockedAdd(0, items, base);

    uint outputHighBits = index << WorkItemCountBits;
    for (uint i = 0; i < items; ++i)
    {
        uint outputValue = outputHighBits | i;
        uint offset = base + i;
        offset *= 4;
        output.Store(offset, outputValue);
    }
}
