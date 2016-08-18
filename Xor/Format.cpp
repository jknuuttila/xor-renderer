#include "Xor/Format.hpp"

namespace xor
{
    uint Format::size() const
    {
        if (m_elementSize)
            return m_elementSize;

        auto fmt = static_cast<DXGI_FORMAT>(m_dxgiFormat);
        switch (fmt)
        {
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R32_UINT:
            return 4;
        default:
            XOR_CHECK(false, "Unknown format");
        }

        return 0;
    }

    uint Format::rowSizeBytes(unsigned rowLength) const
    {
        // TODO: Block compressed formats
        return rowLength * size();
    }
}
