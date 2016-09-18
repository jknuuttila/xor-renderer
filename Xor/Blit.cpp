#include "Xor/Blit.hpp"
#include "Xor/Blit.sig.h"

namespace xor
{
    Blit::Blit(Device & device)
    {
        m_blit = device.createGraphicsPipeline(GraphicsPipeline::Info()
                                               .vertexShader("Blit.vs")
                                               .pixelShader("Blit.ps")
                                               .cull(D3D12_CULL_MODE_NONE)
                                               .blend(0, true)
                                               .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
    }

    void Blit::blit(CommandList & cmd,
                    TextureRTV & dst, int2 dstPos,
                    TextureSRV src, ImageRect srcRect,
                    float4 multiplier, float4 bias)
    {
        cmd.bind(m_blit);
        cmd.setRenderTargets(dst);

        float2 pos     = float2(dstPos);
        float2 dstSize = float2(dst.texture()->size);
        float2 srcSize = float2(srcRect.size());

        float2 topLeft = float2(-1, 1);
        float2 pixel = 2.f / dstSize * float2(1, -1);

        BlitShader::Constants constants;
        constants.posBegin   = topLeft + pos * pixel;
        constants.posEnd     = constants.posBegin + srcSize * pixel;
        constants.uvBegin    = float2(srcRect.leftTop) / srcSize;
        constants.uvEnd      = float2(srcRect.rightBottom) / srcSize;
        constants.mip        = static_cast<float>(srcRect.subresource.mip);
        constants.multiplier = multiplier;
        constants.bias       = bias;

        cmd.setShaderView(BlitShader::src, src);
        cmd.setConstants(constants);
        cmd.setTopology();
        cmd.draw(6);
    }
}
