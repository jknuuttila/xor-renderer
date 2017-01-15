#ifndef XOR_SHADERDEBUG_H_HLSL
#define XOR_SHADERDEBUG_H_HLSL

#include "Xor/ShaderDebugDefs.h"

cbuffer XorShaderDebugConstantBuffer : register(b0, space1)
{
    XorShaderDebugConstants debugConstants;
};

RWByteAddressBuffer debugPrintData : register(u0, space1);

bool debugIsCursorPosition(int2 coords)
{
    return all(coords == debugConstants.cursorPosition);
}

bool debugIsCursorPosition(uint2 coords)
{
    return debugIsCursorPosition(int2(coords));
}

bool debugIsCursorPosition(float2 coords)
{
    return debugIsCursorPosition(int2(coords));
}

static const uint ShaderDebugPrintMetadataSize = 8;
static const uint ShaderDebugPrintNewlineSize = 4;
static const uint ShaderDebugPrintValueHeaderSize = 8;
static const uint ShaderDebugPrintFixedPayloadSize = ShaderDebugPrintMetadataSize + ShaderDebugPrintNewlineSize;

uint debugAllocatePrintSpace(uint variablePayload)
{
    uint offset;
    debugPrintData.InterlockedAdd(0, ShaderDebugPrintFixedPayloadSize + variablePayload, offset);
    return offset;
}

void debugWriteEventMetadata(inout uint offset)
{
    uint2 values;
    values.x = XorShaderDebugPrintOpCodeMetadata;
    values.y = debugConstants.eventNumber;
    debugPrintData.Store2(offset, values);
    offset += 8;
}

void debugWriteValues(uint typeId, uint data, inout uint offset)
{
    uint3 values;
    values.x = XorShaderDebugPrintOpCodePrintValues;
    values.y = typeId;
    values.z = data;
    debugPrintData.Store3(offset, values);
    offset += 12;
}

void debugWriteValues(uint typeId, uint2 data, inout uint offset)
{
    uint4 values;
    values.x  = XorShaderDebugPrintOpCodePrintValues;
    values.y  = typeId;
    values.zw = data;
    debugPrintData.Store4(offset, values);
    offset += 16;
}

void debugWriteValues(uint typeId, uint3 data, inout uint offset)
{
    uint2 values;
    values.x  = XorShaderDebugPrintOpCodePrintValues;
    values.y  = typeId;
    debugPrintData.Store2(offset, values);
    offset += 8;
    debugPrintData.Store3(offset, data);
    offset += 12;
}

void debugWriteValues(uint typeId, uint4 data, inout uint offset)
{
    uint2 values;
    values.x  = XorShaderDebugPrintOpCodePrintValues;
    values.y  = typeId;
    debugPrintData.Store2(offset, values);
    offset += 8;
    debugPrintData.Store4(offset, data);
    offset += 16;
}

void debugWriteNewline(inout uint offset)
{
    debugPrintData.Store(offset, XorShaderDebugPrintOpCodeNewLine);
    offset += 4;
}

uint debugTypeId(uint1 values) { return XOR_SHADERDEBUG_TYPE_ID(uint, 1); }
uint debugTypeId(uint2 values) { return XOR_SHADERDEBUG_TYPE_ID(uint, 2); }
uint debugTypeId(uint3 values) { return XOR_SHADERDEBUG_TYPE_ID(uint, 3); }
uint debugTypeId(uint4 values) { return XOR_SHADERDEBUG_TYPE_ID(uint, 4); }
int debugTypeId(int1 values) { return XOR_SHADERDEBUG_TYPE_ID(int, 1); }
int debugTypeId(int2 values) { return XOR_SHADERDEBUG_TYPE_ID(int, 2); }
int debugTypeId(int3 values) { return XOR_SHADERDEBUG_TYPE_ID(int, 3); }
int debugTypeId(int4 values) { return XOR_SHADERDEBUG_TYPE_ID(int, 4); }
float debugTypeId(float1 values) { return XOR_SHADERDEBUG_TYPE_ID(float, 1); }
float debugTypeId(float2 values) { return XOR_SHADERDEBUG_TYPE_ID(float, 2); }
float debugTypeId(float3 values) { return XOR_SHADERDEBUG_TYPE_ID(float, 3); }
float debugTypeId(float4 values) { return XOR_SHADERDEBUG_TYPE_ID(float, 4); }
uint debugPayloadSize(uint1 values) { return ShaderDebugPrintValueHeaderSize + 4; }
uint debugPayloadSize(uint2 values) { return ShaderDebugPrintValueHeaderSize + 8; }
uint debugPayloadSize(uint3 values) { return ShaderDebugPrintValueHeaderSize + 12; }
uint debugPayloadSize(uint4 values) { return ShaderDebugPrintValueHeaderSize + 16; }

// Define the actual printing operations as macros instead of functions to avoid a combinatorial
// overload explosion with {uint,int,float} x {1,2,3,4} x argument count
#define debugPrint1(values1) { \
    uint offset_ = debugAllocatePrintSpace(debugPayloadSize(asuint(values1))); \
    debugWriteEventMetadata(offset_); \
    debugWriteValues(debugTypeId(values1), asuint(values1), offset_); \
    debugWriteNewline(offset_); \
}

#define debugPrint2(values1, values2) { \
    uint offset_ = debugAllocatePrintSpace(debugPayloadSize(asuint(values1)) + debugPayloadSize(asuint(values2))); \
    debugWriteEventMetadata(offset_); \
    debugWriteValues(debugTypeId(values1), asuint(values1), offset_); \
    debugWriteValues(debugTypeId(values2), asuint(values2), offset_); \
    debugWriteNewline(offset_); \
}

#define debugPrint3(values1, values2, values3) { \
    uint offset_ = debugAllocatePrintSpace(debugPayloadSize(asuint(values1)) + debugPayloadSize(asuint(values2)) + debugPayloadSize(asuint(values3))); \
    debugWriteEventMetadata(offset_); \
    debugWriteValues(debugTypeId(values1), asuint(values1), offset_); \
    debugWriteValues(debugTypeId(values2), asuint(values2), offset_); \
    debugWriteValues(debugTypeId(values3), asuint(values3), offset_); \
    debugWriteNewline(offset_); \
}

#define debugPrint4(values1, values2, values3, values4) { \
    uint offset_ = debugAllocatePrintSpace(debugPayloadSize(asuint(values1)) + debugPayloadSize(asuint(values2)) + debugPayloadSize(asuint(values3)) + debugPayloadSize(asuint(values4))); \
    debugWriteEventMetadata(offset_); \
    debugWriteValues(debugTypeId(values1), asuint(values1), offset_); \
    debugWriteValues(debugTypeId(values2), asuint(values2), offset_); \
    debugWriteValues(debugTypeId(values3), asuint(values3), offset_); \
    debugWriteValues(debugTypeId(values4), asuint(values4), offset_); \
    debugWriteNewline(offset_); \
}

#endif
