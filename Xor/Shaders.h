#ifndef XOR_SHADERS_H
#define XOR_SHADERS_H

#ifdef __cplusplus

#include "Core/Math.hpp"

namespace xor
{
    namespace backend
    {
        template <typename T, unsigned Slot> struct ShaderCBuffer {};
        template <unsigned Slot> using ShaderSRV = unsigned;
        template <unsigned Slot> using ShaderUAV = unsigned;
    }
}

#define XOR_BEGIN_SIGNATURE(signatureName) struct signatureName {
#define XOR_END_SIGNATURE };

#define XOR_CBUFFER(cbufferName, cbufferSlot) \
    struct cbufferName : ::xor::backend::ShaderCBuffer<cbufferName, cbufferSlot>

#define XOR_SRV(srvType, srvName, srvSlot) \
    static const ::xor::backend::ShaderSRV<srvSlot> srvName = srvSlot;
#define XOR_UAV(uavType, uavName, uavSlot) \
    static const ::xor::backend::ShaderUAV<uavSlot> uavName = uavSlot;

#define XOR_SAMPLER_BILINEAR(samplerName)
#define XOR_SAMPLER_POINT(samplerName)

#else

#define XOR_BEGIN_SIGNATURE(signatureName)
#define XOR_END_SIGNATURE

#define XOR_CBUFFER(cbufferName, cbufferSlot) \
    cbuffer cbufferName : register(b ## cbufferSlot)

#define XOR_SRV(srvType, srvName, srvSlot) \
    srvType srvName : register(t ## srvSlot);
#define XOR_UAV(uavType, uavName, uavSlot) \
    uavType uavName : register(u ## uavSlot);

#define XOR_SAMPLER_BILINEAR(samplerName) \
    SamplerState samplerName : register(s0);
#define XOR_SAMPLER_POINT(samplerName) \
    SamplerState samplerName : register(s1);

#endif

#define XOR_ROOT_SIGNATURE_BASE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
    "StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT), " \
    "StaticSampler(s1, " \
        "filter = FILTER_MIN_MAG_MIP_POINT), "

#define XOR_ROOT_SIGNATURE_C(numCBVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")) " \

#define XOR_ROOT_SIGNATURE_S(numSRVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(SRV(t0, numDescriptors = " #numSRVs ")) " \

#define XOR_ROOT_SIGNATURE_U(numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(UAV(u0, numDescriptors = " #numUAVs ")) " \

#define XOR_ROOT_SIGNATURE_CS(numCBVs, numSRVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")," \
    "                SRV(t0, numDescriptors = " #numSRVs ")) " \

#define XOR_ROOT_SIGNATURE_CU(numCBVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")," \
    "                UAV(u0, numDescriptors = " #numUAVs ")) " \

#define XOR_ROOT_SIGNATURE_SU(numSRVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(SRV(t0, numDescriptors = " #numSRVs ")," \
    "                UAV(u0, numDescriptors = " #numUAVs ")) " \

#define XOR_ROOT_SIGNATURE_CSU(numCBVs, numSRVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")," \
    "                SRV(t0, numDescriptors = " #numSRVs "), " \
    "                UAV(u0, numDescriptors = " #numUAVs ")) " \

#endif