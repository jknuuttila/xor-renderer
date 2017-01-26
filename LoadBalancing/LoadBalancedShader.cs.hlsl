#include "LoadBalancedShader.sig.h"

// Kogge-Stone prefix sum calculation.
groupshared uint prefixSum[LBThreadGroupSize + 1];
uint computePrefixSum(uint threadId, uint thisThreadsValue)
{
    int ownIndex = threadId + 1;
    uint sum     = thisThreadsValue;

    prefixSum[0]        = 0;
    prefixSum[ownIndex] = thisThreadsValue;

    GroupMemoryBarrierWithGroupSync();

    int accumulateFrom = -1;
    for (uint i = 0; i < LBThreadGroupSizeLog2; ++i)
    {
        int index = ownIndex + accumulateFrom;
        index     = max(0, index);

        uint partialSum = prefixSum[index];
        sum += partialSum;

        GroupMemoryBarrierWithGroupSync();

        prefixSum[ownIndex] = sum;
        accumulateFrom <<= 1;

        GroupMemoryBarrierWithGroupSync();
    }
}

uint exclusivePrefixSum(uint i) { return prefixSum[i];     }
uint inclusivePrefixSum(uint i) { return prefixSum[i + 1]; }

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
