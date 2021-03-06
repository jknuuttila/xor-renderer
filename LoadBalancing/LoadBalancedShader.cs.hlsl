#include "LoadBalancedShader.sig.h"

// #define NAIVE_LDS_ATOMICS
// #define PREFIX_LINEAR
// #define ZERO_SKIPPING
// #define SKIP_TO_LAST

// #define PREFIX_LINEAR_STORE4
// #define PREFIX_BINARY
// #define PREFIX_BITSCAN

// #define WORK_STEALING

#define ONE_AT_A_TIME

#ifndef WORK_STEALING_THRESHOLD
#define WORK_STEALING_THRESHOLD 8
#endif

#ifndef LB_SUBGROUP_SIZE
#define LB_SUBGROUP_SIZE        16
#define LB_SUBGROUP_SIZE_LOG2   4
#endif

static const uint ThreadGroupSize     = LB_THREADGROUP_SIZE;
static const uint ThreadGroupSizeLog2 = LB_THREADGROUP_SIZE_LOG2;

static const uint SubgroupSize     = LB_SUBGROUP_SIZE;
static const uint SubgroupSizeLog2 = LB_SUBGROUP_SIZE_LOG2;
static const uint SubgroupCount    = ThreadGroupSize / SubgroupSize;

uint subgroupID(uint groupID)
{
#if LB_SUBGROUP_SIZE == LB_THREADGROUP_SIZE
    return 0;
#else
    return groupID / SubgroupSize;
#endif
}
uint subgroupThreadID(uint groupID)
{
#if LB_SUBGROUP_SIZE == LB_THREADGROUP_SIZE
    return groupID;
#else
    return groupID % SubgroupSize;
#endif
}

#if !defined(NAIVE) \
    && !defined(NAIVE_LDS_ATOMICS) \
    && !defined(PREFIX_LINEAR) \
    && !defined(PREFIX_LINEAR_STORE4) \
    && !defined(PREFIX_BINARY) \
    && !defined(PREFIX_BITSCAN) \
    && !defined(WORK_STEALING) \
    && !defined(ONE_AT_A_TIME)

#define NAIVE
#endif

// Kogge-Stone prefix sum calculation.
groupshared uint prefixSum[SubgroupCount][ThreadGroupSize + 1];
void computePrefixSum(uint sgid, uint stid, uint thisThreadsValue)
{
    int ownIndex = stid + 1;
    uint sum     = thisThreadsValue;

    if (stid == 0) prefixSum[sgid][0] = 0;
    prefixSum[sgid][ownIndex] = thisThreadsValue;

    GroupMemoryBarrierWithGroupSync();

    int accumulateFrom = -1;
    for (uint i = 0; i < SubgroupSizeLog2; ++i)
    {
        int index = ownIndex + accumulateFrom;
        index     = max(0, index);

        uint partialSum = prefixSum[sgid][index];
        sum += partialSum;

        GroupMemoryBarrierWithGroupSync();

        prefixSum[sgid][ownIndex] = sum;
        accumulateFrom <<= 1;

        GroupMemoryBarrierWithGroupSync();
    }
}

uint exclusivePrefixSum(uint sgid, uint stid) { return prefixSum[sgid][stid];     }
uint inclusivePrefixSum(uint sgid, uint stid) { return prefixSum[sgid][stid + 1]; }
// uint exclusivePrefixSum(uint i) { return exclusivePrefixSum(subgroupID(i), subgroupThreadID(i)); }
// uint inclusivePrefixSum(uint i) { return inclusivePrefixSum(subgroupID(i), subgroupThreadID(i)); }
uint totalCount(uint sgid) { return prefixSum[sgid][SubgroupSize]; }

uint binarySearchForFirstGreater(uint sgid, uint needle)
{
    uint min   = 0;
    uint max   = SubgroupSize;
    uint probe = max / 2;

    [unroll]
    for (uint i = 0; i < SubgroupSizeLog2; ++i)
    {
        uint value       = prefixSum[sgid][probe];
        bool lessOrEqual = value <= needle;
        min              = lessOrEqual ? probe : min;
        max              = lessOrEqual ?   max : probe;

        uint diff        = max - min;
        probe            = min + diff / 2;
    }

    return max;
}

groupshared uint groupBaseShared[SubgroupCount];
groupshared uint groupOutputHigh[SubgroupCount][SubgroupSize];
#ifdef ZERO_SKIPPING
groupshared uint groupZeroCount[SubgroupCount][SubgroupSize / 32 + 1];
#endif
#ifdef SKIP_TO_LAST
groupshared uint groupLastActiveWorkItem[SubgroupCount];
#endif
void prefixLinear(uint tid, uint gid, uint index, uint items)
{
    uint sgid = subgroupID(gid);
    uint stid = subgroupThreadID(gid);

    // If zero skipping is enabled, mark all nonzero counts with one bits
#ifdef ZERO_SKIPPING
    {
        if (stid < (SubgroupSize / 32 + 1))
            groupZeroCount[sgid][stid] = 0;

        GroupMemoryBarrierWithGroupSync();

        if (items > 0)
        {
            uint zeroIndex = stid / 32;
            uint zeroBit   = stid % 32;
            uint zeroMask  = 1 << zeroBit;
            InterlockedOr(groupZeroCount[sgid][zeroIndex], zeroMask);
        }
    }
#endif

#ifdef SKIP_TO_LAST
    if (stid == 0)
        groupLastActiveWorkItem[sgid] = 0;
#endif

    computePrefixSum(sgid, stid, items);

    uint total = totalCount(sgid);

    // debugPrint1(uint4(tid, items, inclusivePrefixSum(gid), total));

    uint outputHighBits         = index << WorkItemCountBits;
    groupOutputHigh[sgid][stid] = outputHighBits;

    uint groupBase;
    if (stid == 0)
    {
        outputCounter.InterlockedAdd(0, total, groupBase);
        groupBaseShared[sgid] = groupBase;
    }
    GroupMemoryBarrierWithGroupSync();

    for (sgid = 0; sgid < SubgroupCount; ++sgid)
    {
        groupBase = groupBaseShared[sgid];
        total     = totalCount(sgid);

        uint i = 0;
        for (uint base = 0; base < total; base += ThreadGroupSize)
        {
            uint workItem = base + gid;
            if (workItem < total)
            {
#ifdef SKIP_TO_LAST
                i = groupLastActiveWorkItem[sgid];
#endif
                // debugPrint1(uint4(tid, base, workItem, inclusivePrefixSum(i) <= workItem));
                while (inclusivePrefixSum(sgid, i) <= workItem)
                {
                    // If zero skipping is enabled, use bitscan to directly
                    // advance to a nonzero index.
#ifdef ZERO_SKIPPING
                    uint zeroIndex = i / 32;
                    uint zeroBit = i % 32;
                    uint zeroMask = (0xfffffffe << zeroBit);
                    uint zeroValue = groupZeroCount[sgid][zeroIndex];
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

                uint workItemBase = exclusivePrefixSum(sgid, i);
                uint workItemOffset = workItem - workItemBase;

                outputHighBits = groupOutputHigh[sgid][i];

                uint offset = groupBase + workItem;
                uint outputValue = outputHighBits | workItemOffset;

                // debugPrint2(uint4(tid, workItemBase, workItemOffset, workItem), uint4(i, groupBase, offset, outputValue));

                output.Store(offset * 4, outputValue);
#ifdef SKIP_TO_LAST
                if (stid == SubgroupSize - 1)
                    groupLastActiveWorkItem[sgid] = i;
#endif
            }

#ifdef SKIP_TO_LAST
            GroupMemoryBarrierWithGroupSync();
#endif
        }
    }
}

void prefixLinearStore4(uint tid, uint gid, uint index, uint items)
{
    uint sgid = subgroupID(gid);
    uint stid = subgroupThreadID(gid);

    computePrefixSum(sgid, stid, items);

    uint total = totalCount(sgid);

    // debugPrint1(uint4(tid, items, inclusivePrefixSum(gid), total));

    uint outputHighBits  = index << WorkItemCountBits;
    groupOutputHigh[sgid][stid] = outputHighBits;

    uint groupBase;
    if (stid == 0)
    {
        outputCounter.InterlockedAdd(0, total, groupBase);
        groupBaseShared[sgid] = groupBase;
    }
    GroupMemoryBarrierWithGroupSync();
    groupBase = groupBaseShared[sgid];

    uint i = 0;
    uint store4Limit = total - 4;
    for (uint base = 0; base < total; base += SubgroupSize * 4)
    {
        uint threadBase = base + stid * 4;
        uint4 outputValues;

        uint ips = inclusivePrefixSum(sgid, i);

        [unroll]
        for (uint j = 0; j < 4; ++j)
        {
            uint workItem = threadBase + j;
            uint outputValue = 0;

            if (workItem < total)
            {
                // debugPrint1(uint4(tid, base, workItem, inclusivePrefixSum(i) <= workItem));
                while (ips <= workItem)
                {
                    ++i;
                    ips = inclusivePrefixSum(sgid, i);
                }

                uint workItemBase   = exclusivePrefixSum(sgid, i);
                uint workItemOffset = workItem - workItemBase;

                outputHighBits = groupOutputHigh[sgid][i];

                outputValue = outputHighBits | workItemOffset;

                // debugPrint2(uint4(tid, workItemBase, workItemOffset, workItem), uint4(i, groupBase, groupBase + threadBase + j, outputValue));
            }

            outputValues[j] = outputValue;
        }

        uint offset = groupBase + threadBase;
        offset *= 4;
        if (threadBase <= store4Limit)
        {
            // debugPrint2(offset / 4, outputValues);
            output.Store4(offset, outputValues);
        }
        else if (threadBase < total)
        {
            uint remaining = total - threadBase;

            if      (remaining == 3) output.Store3(offset, outputValues.xyz);
            else if (remaining == 2) output.Store2(offset, outputValues.xy);
            else if (remaining == 1) output.Store (offset, outputValues.x);
        }
    }
}

void prefixBinary(uint tid, uint gid, uint index, uint items)
{
    uint sgid = subgroupID(gid);
    uint stid = subgroupThreadID(gid);

    computePrefixSum(sgid, stid, items);

    uint total = totalCount(sgid);

    // debugPrint1(uint4(tid, items, inclusivePrefixSum(gid), total));

    uint outputHighBits  = index << WorkItemCountBits;
    groupOutputHigh[sgid][stid] = outputHighBits;

    uint groupBase;
    if (stid == 0)
    {
        outputCounter.InterlockedAdd(0, total, groupBase);
        groupBaseShared[sgid] = groupBase;
    }
    GroupMemoryBarrierWithGroupSync();

    for (sgid = 0; sgid < SubgroupCount; ++sgid)
    {
        total     = totalCount(sgid);
        groupBase = groupBaseShared[sgid];

        for (uint base = 0; base < total; base += ThreadGroupSize)
        {
            uint workItem = base + gid;
            if (workItem < total)
            {
                // debugPrint1(uint4(tid, base, workItem, inclusivePrefixSum(i) <= workItem));
                uint i = binarySearchForFirstGreater(sgid, workItem) - 1;

                uint workItemBase   = exclusivePrefixSum(sgid, i);
                uint workItemOffset = workItem - workItemBase;

                outputHighBits      = groupOutputHigh[sgid][i];

                uint offset         = groupBase + workItem;
                uint outputValue    = outputHighBits | workItemOffset;

                // debugPrint2(uint4(tid, workItemBase, workItemOffset, workItem), uint4(i, groupBase, offset, outputValue));

                output.Store(offset * 4, outputValue);
            }
        }
    }
}

#if defined(PREFIX_BITSCAN)
#if LB_SUBGROUP_SIZE > 32
#error Subgroup sizes above 32 are not supported because there are no 64-bit integers
#endif
#endif
groupshared uint groupWorkBits[SubgroupCount];
groupshared uint groupWorkIndices[SubgroupCount][SubgroupSize];
void prefixBitscan(uint tid, uint gid, uint index, uint items)
{
    uint sgid = subgroupID(gid);
    uint stid = subgroupThreadID(gid);

    computePrefixSum(sgid, stid, items);

    uint total = totalCount(sgid);

    // debugPrint2(uint3(tid, sgid, stid), uint3(items, inclusivePrefixSum(sgid, stid), total));

    uint outputHighBits = index << WorkItemCountBits;
    groupOutputHigh[sgid][stid] = outputHighBits;

    uint groupBase;
    if (stid == 0)
    {
        outputCounter.InterlockedAdd(0, total, groupBase);
        groupBaseShared[sgid] = groupBase;
        groupWorkBits[sgid] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    total                = totalCount(sgid);
    groupBase            = groupBaseShared[sgid];

    uint maxTotal = 0;
    for (uint sg = 0; sg < SubgroupCount; ++sg)
        maxTotal = max(maxTotal, totalCount(sg));

    uint sumHere         = inclusivePrefixSum(sgid, stid);
    uint sumPrev         = inclusivePrefixSum(sgid, stid - 1);
    uint maxWorkItemHere = sumHere - 1;

    uint begin = sumPrev;
    uint end   = sumHere;

    end = (items == 0) ? 0 : end;

    // debugPrint2(uint3(tid, stid, maxTotal), uint4(begin, end, maxWorkItemHere, total));

    for (uint base = 0; base < maxTotal; base += SubgroupSize)
    {
        uint maxWorkItemNow = base + (SubgroupSize - 1);
        uint inRange = maxWorkItemNow >= begin;
        inRange     &= base < end;
        
        // debugPrint2(uint2(tid, stid), uint4(begin, base, end, inRange));

        if (inRange)
        {
            uint maxWorkItemHereNow                = min(maxWorkItemHere, maxWorkItemNow);
            uint maxStidHereNow                    = maxWorkItemHereNow - base;
            uint workItemMask                      = 1 << maxStidHereNow;

            // debugPrint2(uint2(tid, stid), uint2(maxWorkItemHereNow, maxStidHereNow));

            groupWorkIndices[sgid][maxStidHereNow] = stid;
            InterlockedOr(groupWorkBits[sgid], workItemMask);
        }
        GroupMemoryBarrierWithGroupSync();

        uint workItem     = base + stid;
        uint workItemMask = 0xffffffff << stid;

        uint workBits     = groupWorkBits[sgid];

        // debugPrint3(uint2(tid, stid),
        //            uint4(workItem, workBits, workItemMask, workBits & workItemMask),
        //            uint2(firstbitlow(workBits & workItemMask), groupWorkIndices[sgid][clamp(firstbitlow(workBits & workItemMask), 0, 31)]));

        workBits         &= workItemMask;

        if (workBits && workItem < total)
        {
            uint workAt         = firstbitlow(workBits);
            uint i              = groupWorkIndices[sgid][workAt];

            uint workItemBase   = exclusivePrefixSum(sgid, i);
            uint workItemOffset = workItem - workItemBase;

            outputHighBits      = groupOutputHigh[sgid][i];

            uint offset         = groupBase + workItem;
            uint outputValue    = outputHighBits | workItemOffset;

            // debugPrint2(uint4(tid, workItemBase, workItemOffset, workItem), uint4(i, groupBase, offset, outputValue));

            output.Store(offset * 4, outputValue);
        }
        
        if (stid == 0)
            groupWorkBits[sgid] = 0;

        GroupMemoryBarrierWithGroupSync();
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

groupshared uint groupTotal;
void naiveLDSAtomics(uint tid, uint gid, uint index, uint items)
{

    if (gid == 0) groupTotal = 0;
    GroupMemoryBarrierWithGroupSync();

    uint base;
    InterlockedAdd(groupTotal, items, base);
    GroupMemoryBarrierWithGroupSync();

    if (gid == 0)
    {
        uint groupBase;
        outputCounter.InterlockedAdd(0, groupTotal, groupBase);
        groupBaseShared[0] = groupBase;
    }
    GroupMemoryBarrierWithGroupSync();

    base += groupBaseShared[0];

    uint outputHighBits = index << WorkItemCountBits;
    for (uint i = 0; i < items; ++i)
    {
        uint outputValue = outputHighBits | i;
        uint offset = base + i;
        offset *= 4;
        output.Store(offset, outputValue);
    }
}

#if defined(WORK_STEALING)
#if LB_SUBGROUP_SIZE > 32
#error Subgroup sizes above 32 are not supported because there are no 64-bit integers
#endif
#endif
groupshared uint4 groupWorkItems[SubgroupCount][SubgroupSize];
groupshared uint  groupWorkInSlot[SubgroupCount];
groupshared uint  groupWorkLeft;
void workStealing(uint tid, uint gid, uint index, uint items)
{
    uint sgid = subgroupID(gid);
    uint stid = subgroupThreadID(gid);

    uint base;
    outputCounter.InterlockedAdd(0, items, base);

    if (stid == 0) groupWorkInSlot[sgid] = 0;
    if (gid  == 0) groupWorkLeft = 0xffffffff >> (32 - SubgroupCount);
    GroupMemoryBarrierWithGroupSync();

    uint outputHighBits = index << WorkItemCountBits;

    uint workBegin = 0;
    uint workSize  = items;
    uint ownMask   = 1 << stid;
    uint rotShift  = (32 - stid) % 32;

    for (;;)
    {
        // debugPrint2(uint3(tid, sgid, stid), uint2(workBegin, workSize));

        // If we have too much work and there is no work in our slot, split
        // our work and put it up for stealing.
        bool tooMuchWork  = workSize > WORK_STEALING_THRESHOLD;
        uint workInSlot = groupWorkInSlot[sgid] & ownMask;
        if (tooMuchWork && !workInSlot)
        {
            uint split     = workSize / 2;
            uint leftForMe = workSize - split;

            uint4 workToSteal = uint4(workBegin + leftForMe, split, base, outputHighBits);
            workSize          = leftForMe;

            // debugPrint3(uint4(tid, sgid, stid, ownMask), uint2(workBegin, workSize), uint2(workToSteal.x, workToSteal.y));

            groupWorkItems[sgid][stid] = workToSteal;
            InterlockedOr(groupWorkInSlot[sgid], ownMask);
        }
        GroupMemoryBarrierWithGroupSync();

        // If we have no work, try to steal from some slot, starting from our
        // own (i.e. try to get back our own work first).
        uint workLeft = workSize;
        if (!workLeft)
        {
            uint workBits = groupWorkInSlot[sgid];
            workLeft      = workBits;
            // Rotate so that we are LSB
            uint rotatedWorkBits = (workBits >> stid) | (workBits << rotShift);

            // If there is work to be stolen, try to steal
            if (rotatedWorkBits)
            {
                uint moreWorkAt = firstbitlow(rotatedWorkBits);
                moreWorkAt      = (moreWorkAt + stid) % SubgroupSize;

                // Attempt the steal with an atomic, check if successful
                uint stealMask  = 1 << moreWorkAt;
                uint prevMask;
                InterlockedAnd(groupWorkInSlot[sgid], ~stealMask, prevMask);

                if (prevMask & stealMask)
                {
                    // Successfully stole work, fetch it
                    uint4 stolenWork = groupWorkItems[sgid][moreWorkAt];
                    workBegin        = stolenWork.x;
                    workSize         = stolenWork.y;
                    base             = stolenWork.z;
                    outputHighBits   = stolenWork.w;

                    // debugPrint3(uint3(tid, sgid, stid), uint3(workBits, rotatedWorkBits, moreWorkAt), uint3(prevMask & stealMask, workBegin, workSize));
                }
            }
        }

        // If at least one thread has no more work left after trying to steal,
        // it will zero the bit of this subgroup.
        // If at least one thread has work left, it will set the bit afterward.
        // After the block, the subgroup bit will be set iff at least one
        // thread in the subgroup has work.
        {
            uint groupWorkMask = 1 << sgid;
            if (!workLeft) InterlockedAnd(groupWorkLeft, ~groupWorkMask);
            GroupMemoryBarrierWithGroupSync();
            if (workLeft) InterlockedOr(groupWorkLeft, groupWorkMask);
        }

        if (workSize > 0)
        {
            uint i           = workBegin;
            uint outputValue = outputHighBits | i;
            uint offset      = base + i;
            offset          *= 4;

            // debugPrint2(uint3(tid, sgid, stid), uint3(i, offset, outputValue));

            output.Store(offset, outputValue);

            ++workBegin;
            --workSize;
        }

        GroupMemoryBarrierWithGroupSync();

        if (!groupWorkLeft) return;
    }
}

groupshared uint groupItemCounts[SubgroupCount][SubgroupSize];
groupshared uint groupItemBases[SubgroupCount][SubgroupSize];
void oneAtATime(uint tid, uint gid, uint index, uint items)
{
    uint sgid = subgroupID(gid);
    uint stid = subgroupThreadID(gid);

    uint base;
    outputCounter.InterlockedAdd(0, items, base);

    // debugPrint2(uint3(tid, sgid, stid), uint3(base, index, items));

    uint outputHighBits = index << WorkItemCountBits;
    groupOutputHigh[sgid][stid] = outputHighBits;
    groupItemBases[sgid][stid]  = base;
    groupItemCounts[sgid][stid] = items;

    for (uint i = 0; i < SubgroupSize; ++i)
    {
        items = groupItemCounts[sgid][i];
        if (items > 0)
        {
            base           = groupItemBases[sgid][i];
            outputHighBits = groupOutputHigh[sgid][i];

            for (uint j = 0; j < items; j += SubgroupSize)
            {
                uint workItem = j + stid;
                if (workItem < items)
                {
                    uint offset = base + workItem;
                    uint outputValue = outputHighBits | workItem;

                    // debugPrint3(uint3(tid, sgid, stid), uint2(i, items), uint4(j, workItem, offset, outputValue));

                    output.Store(offset * 4, outputValue);
                }
            }
        }
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
#elif defined(NAIVE_LDS_ATOMICS)
    naiveLDSAtomics(tid.x, gid.x, index, items);
#elif defined(PREFIX_LINEAR)
    prefixLinear(tid.x, gid.x, index, items);
#elif defined(PREFIX_LINEAR_STORE4)
    prefixLinearStore4(tid.x, gid.x, index, items);
#elif defined(PREFIX_BINARY)
    prefixBinary(tid.x, gid.x, index, items);
#elif defined(PREFIX_BITSCAN)
    prefixBitscan(tid.x, gid.x, index, items);
#elif defined(WORK_STEALING)
    workStealing(tid.x, gid.x, index, items);
#elif defined(ONE_AT_A_TIME)
    oneAtATime(tid.x, gid.x, index, items);
#endif
}
