#pragma once

#include "Core/Core.hpp"

namespace xor
{
    class Device;
    class CommandList;

    namespace info
    {
        class InputLayoutInfo;
    }

    enum class VertexAttribute
    {
        Position,
        Normal,
        Tangent,
        Bitangent,
        Color,
        UV,
    };

    struct VertexStream
    {
        VertexAttribute attrib = VertexAttribute::Position;
        uint index             = 0;

        VertexStream() = default;
        VertexStream(VertexAttribute attrib, uint index = 0)
            : attrib(attrib)
            , index(index)
        {}
    };

    class Mesh
    {
        struct State;
        std::shared_ptr<State> m_state;

    public:
        Mesh() = default;
        Mesh(Device &device, const String &filename);

        static std::vector<Mesh> loadFromFile(Device &device, const String &filename);

        info::InputLayoutInfo inputLayout() const;
        void setForRendering(CommandList &cmd) const;
        uint numIndices() const;
    };
}
