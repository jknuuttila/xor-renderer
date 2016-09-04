#pragma once

#include "Xor/Xor.hpp"

namespace xor
{
    struct MaterialLayer
    {
        String filename;
        TextureSRV texture;

        size_t load(Device &device, StringView basePath = {});
    };

    class Material
    {
        struct State
        {
            String name;
            MaterialLayer albedo;
        };
        std::shared_ptr<State> m_state;
    public:
        Material() = default;
        Material(String name);

        bool valid() const { return !!m_state; }
        explicit operator bool() const { return valid(); }

        void load(Device &device, StringView basePath = {});

        MaterialLayer &albedo() { return m_state->albedo; }
    };
}

