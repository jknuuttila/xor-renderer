#pragma once

#include "Xor/Xor.hpp"

namespace xor
{
    class Blit
    {
        GraphicsPipeline m_blit;
    public:
        Blit() = default;
        Blit(Device &device);

        void blit(CommandList &cmd,
                  TextureRTV &dst, Rect dstRect,
                  TextureSRV src, ImageRect srcRect = {},
                  float4 multiplier = 1, float4 bias = float4(0, 0, 0, 1));
    };
}
