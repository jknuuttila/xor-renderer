#pragma once

#include "Core/Core.hpp"
#include "Xor/Format.hpp"

#include <memory>

namespace xor
{
    class Image
    {
        struct State;
        std::shared_ptr<State> m_state;

        void load(const String &filename, Format format = Format());
    public:
        Image() = default;
        Image(const String &filename);

        explicit operator bool() const { return !!m_state; }

        uint2 size() const;
    };
}
