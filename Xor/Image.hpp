#pragma once

#include "Core/Core.hpp"

#include <memory>

namespace xor
{
    class Image
    {
        struct State;
        std::shared_ptr<State> m_state;

    public:
        Image() = default;
        Image(const String &filename);

        explicit operator bool() const { return !!m_state; }

        uint2 size() const;
    };
}
