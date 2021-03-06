#ifndef XOR_SHADERS_H
#define XOR_SHADERS_H

#include "Xor/ShaderDebugDefs.h"

#ifdef __cplusplus

#ifdef __EDG__
#define XOR_INTELLISENSE
#endif

#include "Core/Math.hpp"

namespace Xor
{
    namespace backend
    {
        template <typename T, unsigned Slot> struct ShaderCBuffer {};
        template <unsigned Slot> using ShaderSRV = unsigned;
        template <unsigned Slot> using ShaderUAV = unsigned;
        template <unsigned X, unsigned Y, unsigned Z> struct ThreadGroupSize {};
    }
}

#define XOR_BEGIN_SIGNATURE(signatureName) struct signatureName {
#define XOR_END_SIGNATURE };

#define XOR_CBUFFER(cbufferName, cbufferSlot) \
    struct cbufferName : ::Xor::backend::ShaderCBuffer<cbufferName, cbufferSlot>

#define XOR_TEXTURE_SRV(srvType, srvName, srvSlot) \
    static const ::Xor::backend::ShaderSRV<srvSlot> srvName = srvSlot;
#define XOR_TEXTURE_UAV(uavType, uavName, uavSlot) \
    static const ::Xor::backend::ShaderUAV<uavSlot> uavName = uavSlot;
#define XOR_BUFFER_SRV(srvType, srvName, srvSlot) \
    static const ::Xor::backend::ShaderSRV<srvSlot> srvName = srvSlot;
#define XOR_BUFFER_UAV(uavType, uavName, uavSlot) \
    static const ::Xor::backend::ShaderUAV<uavSlot> uavName = uavSlot;

#define XOR_SAMPLER_BILINEAR(samplerName)
#define XOR_SAMPLER_POINT(samplerName)
#define XOR_SAMPLER_BILINEAR_WRAP(samplerName)
#define XOR_SAMPLER_POINT_WRAP(samplerName)
#define XOR_SAMPLER_PCF_GE(samplerName)

#define XOR_THREADGROUP_SIZE_1D(sizeX)               static constexpr ::Xor::backend::ThreadGroupSize<sizeX, 1, 1> threadGroupSize = {};
#define XOR_THREADGROUP_SIZE_2D(sizeX, sizeY)        static constexpr ::Xor::backend::ThreadGroupSize<sizeX, sizeY, 1> threadGroupSize = {};
#define XOR_THREADGROUP_SIZE_3D(sizeX, sizeY, sizeZ) static constexpr ::Xor::backend::ThreadGroupSize<sizeX, sizeY, sizeZ> threadGroupSize = {};

#else

#define XOR_SHADER

#include "Xor/ShaderMath.h.hlsl"
#include "Xor/ShaderDebug.h.hlsl"

#define XOR_BEGIN_SIGNATURE(signatureName)
#define XOR_END_SIGNATURE

#define XOR_CBUFFER(cbufferName, cbufferSlot) \
    cbuffer cbufferName : register(b ## cbufferSlot)

#define XOR_TEXTURE_SRV(srvType, srvName, srvSlot) \
    srvType srvName : register(t ## srvSlot);
#define XOR_TEXTURE_UAV(uavType, uavName, uavSlot) \
    uavType uavName : register(u ## uavSlot);
#define XOR_BUFFER_SRV(srvType, srvName, srvSlot) \
    srvType srvName : register(t ## srvSlot);
#define XOR_BUFFER_UAV(uavType, uavName, uavSlot) \
    uavType uavName : register(u ## uavSlot);

#define XOR_SAMPLER_BILINEAR(samplerName) \
    SamplerState samplerName : register(s0);
#define XOR_SAMPLER_POINT(samplerName) \
    SamplerState samplerName : register(s1);
#define XOR_SAMPLER_BILINEAR_WRAP(samplerName) \
    SamplerState samplerName : register(s2);
#define XOR_SAMPLER_POINT_WRAP(samplerName) \
    SamplerState samplerName : register(s3);
#define XOR_SAMPLER_PCF_GE(samplerName) \
    SamplerComparisonState samplerName : register(s4);

#define XOR_THREADGROUP_SIZE_1D(sizeX)               static const uint TGSizeX = sizeX; static const uint TGSizeY = 1; static const uint TGSizeZ = 1; static const uint3 TGSize = uint3(sizeX, 1, 1);
#define XOR_THREADGROUP_SIZE_2D(sizeX, sizeY)        static const uint TGSizeX = sizeX; static const uint TGSizeY = sizeY; static const uint TGSizeZ = 1; static const uint3 TGSize = uint3(sizeX, sizeY, 1);
#define XOR_THREADGROUP_SIZE_3D(sizeX, sizeY, sizeZ) static const uint TGSizeX = sizeX; static const uint TGSizeY = sizeY; static const uint TGSizeZ = sizeZ; static const uint3 TGSize = uint3(sizeX, sizeY, sizeZ);

#endif

#define XOR_NUMTHREADS TGSizeX, TGSizeY, TGSizeZ

#define XOR_ROOT_SIGNATURE_BASE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
    "StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT), " \
    "StaticSampler(s1, " \
        "filter = FILTER_MIN_MAG_MIP_POINT), " \
    "StaticSampler(s2, " \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, " \
        "addressU = TEXTURE_ADDRESS_WRAP, " \
        "addressV = TEXTURE_ADDRESS_WRAP, " \
        "addressW = TEXTURE_ADDRESS_WRAP), " \
    "StaticSampler(s3, " \
        "filter = FILTER_MIN_MAG_MIP_POINT, " \
        "addressU = TEXTURE_ADDRESS_WRAP, " \
        "addressV = TEXTURE_ADDRESS_WRAP, " \
        "addressW = TEXTURE_ADDRESS_WRAP), " \
    "StaticSampler(s4, " \
        "filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, " \
        "comparisonFunc = COMPARISON_GREATER_EQUAL), " \

#define XOR_DESCRIPTOR_TABLE(type, slot, numDescs, space) \
    #type "(" #slot ", numDescriptors = " #numDescs ", space = " #space ")" //", flags = DESCRIPTORS_VOLATILE)"

// Built-in descriptors for ShaderDebug
#define XOR_ROOT_SIGNATURE_DEBUG \
    "RootConstants(num32BitConstants = 3, b0, space = 1)," \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(UAV, u0, 1, 1) ")"

#define XOR_ROOT_SIGNATURE_C(numCBVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(CBV, b0, numCBVs, 0) ")," \
    XOR_ROOT_SIGNATURE_DEBUG

#define XOR_ROOT_SIGNATURE_S(numSRVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(SRV, t0, numSRVs, 0) ")," \
    XOR_ROOT_SIGNATURE_DEBUG

#define XOR_ROOT_SIGNATURE_U(numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(UAV, u0, numUAVs, 0) ")," \
    XOR_ROOT_SIGNATURE_DEBUG

#define XOR_ROOT_SIGNATURE_CS(numCBVs, numSRVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(CBV, b0, numCBVs, 0) ", " \
                       XOR_DESCRIPTOR_TABLE(SRV, t0, numSRVs, 0) ")," \
    XOR_ROOT_SIGNATURE_DEBUG

#define XOR_ROOT_SIGNATURE_CU(numCBVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(CBV, b0, numCBVs, 0) ", " \
                       XOR_DESCRIPTOR_TABLE(UAV, u0, numUAVs, 0) ")," \
    XOR_ROOT_SIGNATURE_DEBUG

#define XOR_ROOT_SIGNATURE_SU(numSRVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(SRV, t0, numSRVs, 0) ", " \
                       XOR_DESCRIPTOR_TABLE(UAV, u0, numUAVs, 0) ")," \
    XOR_ROOT_SIGNATURE_DEBUG

#define XOR_ROOT_SIGNATURE_CSU(numCBVs, numSRVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(" XOR_DESCRIPTOR_TABLE(CBV, b0, numCBVs, 0) ", " \
                       XOR_DESCRIPTOR_TABLE(SRV, t0, numSRVs, 0) ", " \
                       XOR_DESCRIPTOR_TABLE(UAV, u0, numUAVs, 0) ")," \
    XOR_ROOT_SIGNATURE_DEBUG

#endif