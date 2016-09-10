#pragma once

#include "Core/Utils.hpp"
#include "Core/Allocators.hpp"
#include "Core/String.hpp"
#include "Core/File.hpp"

namespace xor
{
    class ChunkFile
    {
        String m_path;
        VirtualBuffer<uint8_t> m_contents;
    public:
    };
}
