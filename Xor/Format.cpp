#include "Xor/Format.hpp"

namespace xor
{
    uint Format::size() const
    {
        if (m_elementSize)
            return m_elementSize;

        if (isCompressed())
            return 0;

        switch (dxgiFormat())
        {
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_UNORM:
            return 1;
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_D32_FLOAT:
            return 4;
        case DXGI_FORMAT_R32G32_FLOAT:
            return 8;
        case DXGI_FORMAT_R32G32B32_FLOAT:
            return 12;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return 16;
        default:
            XOR_CHECK(false, "Unknown format");
        }

        return 0;
    }

    uint Format::blockSize() const
    {
        return isCompressed() ? 4 : 1;
    }

    uint Format::blockBytes() const
    {
        switch (dxgiFormat())
        {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC4_UNORM:
            return 8;
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 16;
        default:
            return 0;
        }
    }

    bool Format::isDepthFormat() const
    {
        switch (dxgiFormat())
        {
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return true;
        default:
            return false;
        }
    }

    bool Format::isCompressed() const
    {
        switch (dxgiFormat())
        {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    }

    Format Format::asStructure() const
    {
        return Format::structure(size());
    }

    uint Format::areaSizeBytes(uint2 area) const
    {
        uint b = blockSize();
        if (b > 1)
        {
            area = divRoundUp(area, uint2(b));
            uint blocks = area.x * area.y;
            return blocks * blockBytes();
        }
        else
        {
            return area.x * area.y * size();
        }
    }
}
