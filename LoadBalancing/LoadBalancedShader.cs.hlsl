#include "LoadBalancedShader.sig.h"

#define PREFIX_LINEAR
// #define ZERO_SKIPPING
// #define SKIP_TO_LAST

#if !defined(NAIVE) && !defined(PREFIX_LINEAR)
#define NAIVE
#endif

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
#ifdef ZERO_SKIPPING
groupshared uint groupZeroCount[LBThreadGroupSize / 32 + 1];
#endif
#ifdef SKIP_TO_LAST
groupshared uint groupLastActiveWorkItem;
#endif
void prefixLinear(uint tid, uint gid, uint index, uint items)
{
    // If zero skipping is enabled, mark all nonzero counts with one bits
#ifdef ZERO_SKIPPING
    {
        if (gid < (LBThreadGroupSize / 32))
            groupZeroCount[gid] = 0;

        GroupMemoryBarrierWithGroupSync();

        if (items > 0)
        {
            uint zeroIndex = gid / 32;
            uint zeroBit   = gid % 32;
            uint zeroMask  = 1 << zeroBit;
            InterlockedOr(groupZeroCount[zeroIndex], zeroMask);
        }
    }
#endif

#ifdef SKIP_TO_LAST
    if (gid == 0)
        groupLastActiveWorkItem = 0;
#endif

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
#ifdef SKIP_TO_LAST
            i = groupLastActiveWorkItem;
#endif
            // debugPrint1(uint4(tid, base, workItem, inclusivePrefixSum(i) <= workItem));
            while (inclusivePrefixSum(i) <= workItem)
            {
                // If zero skipping is enabled, use bitscan to directly
                // advance to a nonzero index.
#ifdef ZERO_SKIPPING
                // 1 0 1 0 0 1
                // 3 0 2 0 0 1
                // 3 3 5 5 5 6

                // i == 0
                uint zeroIndex = i / 32;
                uint zeroBit   = i % 32;
                uint zeroMask  = (0xfffffffe << zeroBit);
                uint zeroValue = groupZeroCount[zeroIndex];
                zeroValue &= zeroMask;

                uint increment = (zeroValue != 0)
                    ? firstbitlow(zeroValue)
                    : 32;
                increment -= zeroBit;

                // debugPrint3(uint4(tid, i, inclusivePrefixSum(i), workItem), uint3(zeroIndex, zeroBit, zeroValue), uint2(increment, i + increment));

                i += increment;
#else
                ++i;
#endif
            }

            uint workItemBase   = exclusivePrefixSum(i);
            uint workItemOffset = workItem - workItemBase;

            outputHighBits = groupOutputHigh[i];

            uint offset         = groupBase + workItem;
            uint outputValue    = outputHighBits | workItemOffset;

            // debugPrint2(uint4(tid, workItemBase, workItemOffset, workItem), uint4(i, outputHighBits, offset, outputValue));

            output.Store(offset * 4, outputValue);
#ifdef SKIP_TO_LAST
            if (gid == LBThreadGroupSize - 1)
                groupLastActiveWorkItem = i;
#endif
        }

#ifdef SKIP_TO_LAST
        GroupMemoryBarrierWithGroupSync();
#endif
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

#if defined(NAIVE)
    naive(tid.x, gid.x, index, items);
#elif defined(PREFIX_LINEAR)
    prefixLinear(tid.x, gid.x, index, items);
#endif
}
