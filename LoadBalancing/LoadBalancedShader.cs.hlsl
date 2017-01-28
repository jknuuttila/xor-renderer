#include "LoadBalancedShader.sig.h"

// Kogge-Stone prefix sum calculation.
groupshared uint prefixSum[LBThreadGroupSize + 1];
void computePrefixSum(uint threadId, uint thisThreadsValue)
{
    int ownIndex = threadId + 1;
    uint sum     = thisThreadsValue;

    if (threadId == 0) prefixSum[0] = 0;
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
uint totalCount() { return prefixSum[LBThreadGroupSize]; }

groupshared uint groupBaseShared;
groupshared uint groupOutputHigh[LBThreadGroupSize];
void prefixLinear(uint tid, uint gid, uint index, uint items)
{
    computePrefixSum(gid, items);

    uint total = totalCount();
    // debugPrint1(uint4(tid, items, inclusivePrefixSum(tid), total));

    uint outputHighBits  = index << WorkItemCountBits;
    groupOutputHigh[gid] = outputHighBits;

    uint groupBase;
    if (gid == 0)
    {
        outputCounter.InterlockedAdd(0, total, groupBase);
        groupBaseShared = groupBase;
    }
    GroupMemoryBarrierWithGroupSync();
    groupBase = groupBaseShared;

    uint i = 0;
    for (uint base = 0; base < total; base += LBThreadGroupSize)
    {
        uint workItem = base + tid;
        if (workItem < total)
        {
            while (inclusivePrefixSum(i) <= workItem)
                ++i;

            uint workItemBase   = exclusivePrefixSum(i);
            uint workItemOffset = workItem - workItemBase;

            outputHighBits = groupOutputHigh[i];
            // debugPrint2(uint2(tid, base), uint4(i, workItem, outputHighBits, workItemOffset));

            uint offset         = groupBase + workItem;
            uint outputValue    = outputHighBits | workItemOffset;
            output.Store(offset, outputValue);
        }
    }
}

void naive(uint tid, uint gid, uint index, uint items)
{
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

[RootSignature(LOADBALANCEDSHADER_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupThreadID)
{
    uint inputValue;

    if (tid.x >= size)
        inputValue = 0;
    else
        inputValue = input.Load(tid.x * 4);

    uint index = inputValue >> WorkItemCountBits;
    uint items = inputValue  & WorkItemCountMask;

#if 1
    naive(tid.x, gid.x, index, items);
#else
    prefixLinear(tid.x, gid.x, index, items);
#endif
}
