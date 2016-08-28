#include "Xor/Format.hpp"

namespace xor
{
    uint Format::size() const
    {
        if (m_elementSize)
            return m_elementSize;

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
        default:
            XOR_CHECK(false, "Unknown format");
        }

        return 0;
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

    uint Format::rowSizeBytes(unsigned rowLength) const
    {
        // TODO: Block compressed formats
        return rowLength * size();
    }
}
