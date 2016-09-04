#pragma once

#include "Core/Core.hpp"
#include "Core/TLog.hpp"

#include "Xor/XorBackend.hpp"
#include "Xor/Image.hpp"

#include <unordered_map>

namespace xor
{
    namespace info
    {
        enum class DepthMode
        {
            Disabled,
            ReadOnly,
            Write,
        };

        class BufferInfo
        {
        protected:
            std::function<void(CommandList &cmd, Buffer &buf)> m_initializer;
            friend class Device;
        public:
            size_t size = 0;
            Format format;

            BufferInfo() = default;
            BufferInfo(size_t size, Format format)
                : size(size)
                , format(format)
            {}

            static BufferInfo fromBytes(Span<const uint8_t> data, Format format);

            template <typename T>
            BufferInfo(Span<const T> data, Format format = Format::structure<T>())
            {
                *this = fromBytes(asBytes(data), format);
            }

            template <typename T>
            BufferInfo(std::initializer_list<T> data, Format format = Format::structure<T>())
                : BufferInfo(asConstSpan(data), format)
            {}

            size_t sizeBytes() const { return size * format.size(); }
        };

        class BufferInfoBuilder : public BufferInfo
        {
        public:
            using Info = BufferInfo;

            BufferInfoBuilder() = default;
            BufferInfoBuilder(const BufferInfo &info) : BufferInfo(info) {}
            BufferInfoBuilder &size(size_t sz)    { BufferInfo::size = sz; return *this; }
            BufferInfoBuilder &format(Format fmt) { BufferInfo::format = fmt; return *this; }
        };

        class BufferViewInfo
        {
        public:
            size_t firstElement = 0;
            uint   numElements  = 0;
            Format format;

            BufferViewInfo defaults(const BufferInfo &bufferInfo) const;
            uint sizeBytes() const;
        };

        class BufferViewInfoBuilder : public BufferViewInfo 
        {
        public:
            using Info = BufferViewInfo;

            BufferViewInfoBuilder() = default;
            BufferViewInfoBuilder(const BufferViewInfo &info) : BufferViewInfo(info) {}

            BufferViewInfoBuilder &firstElement(size_t index) { BufferViewInfo::firstElement = index; return *this; }
            BufferViewInfoBuilder &numElements(uint count) { BufferViewInfo::numElements = count; return *this; }
            BufferViewInfoBuilder &format(Format format) { BufferViewInfo::format = format; return *this; }
        };

        class TextureInfo
        {
        protected:
            std::function<void(CommandList &cmd, Texture &tex)> m_initializer;
            friend class Device;
        public:
            uint2 size;
            Format format;
            uint mipLevels = 1;

            TextureInfo() = default;
            TextureInfo(uint2 size, Format format)
                : size(size)
                , format(format)
            {}
            TextureInfo(const Image &image, Format fmt = Format());
            TextureInfo(const ImageData &image, Format fmt = Format());
            TextureInfo(ID3D12Resource *texture);

            size_t sizeBytes() const;
        };

        class TextureInfoBuilder : public TextureInfo
        {
        public:
            using Info = TextureInfo;

            TextureInfoBuilder() = default;
            TextureInfoBuilder(const TextureInfo &info) : TextureInfo(info) {}
        };

        class TextureViewInfo
        {
        public:
            Format format;

            TextureViewInfo() = default;
            TextureViewInfo(Format format) : format(format) {}

            TextureViewInfo defaults(const TextureInfo &textureInfo) const;
        };

        class TextureViewInfoBuilder : public TextureViewInfo 
        {
        public:
            using Info = TextureViewInfo;

            TextureViewInfoBuilder() = default;
            TextureViewInfoBuilder(const TextureViewInfo &info) : TextureViewInfo(info) {}
        };

        class InputLayoutInfo
        {
        protected:
            std::vector<D3D12_INPUT_ELEMENT_DESC> m_elements;
            friend class Device;
        public:
            D3D12_INPUT_LAYOUT_DESC desc() const;
        };

        class InputLayoutInfoBuilder : public InputLayoutInfo
        {
        public:
            InputLayoutInfoBuilder &element(const char *semantic, uint index,
                                            Format format,
                                            uint inputSlot = 0,
                                            uint offset = D3D12_APPEND_ALIGNED_ELEMENT)
            {
                m_elements.emplace_back();
                auto &e                = m_elements.back();
                e.SemanticName         = semantic;
                e.SemanticIndex        = index;
                e.Format               = format;
                e.InputSlot            = inputSlot;
                e.AlignedByteOffset    = offset;
                e.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                e.InstanceDataStepRate = 0;
                return *this;
            }
        };

        class GraphicsPipelineInfo : private D3D12_GRAPHICS_PIPELINE_STATE_DESC
        {
            friend class Device;
            friend class GraphicsPipeline;
            friend struct backend::PipelineState;
            String                                 m_vs;
            String                                 m_ps;
            std::shared_ptr<info::InputLayoutInfo> m_inputLayout;
        public:
            GraphicsPipelineInfo();

            GraphicsPipelineInfo &vertexShader(const String &vsName);
            GraphicsPipelineInfo &pixelShader(const String &psName);
            GraphicsPipelineInfo &renderTargetFormats(Format format);
            GraphicsPipelineInfo &renderTargetFormats(Span<const Format> formats);
            GraphicsPipelineInfo &renderTargetFormats(Span<const DXGI_FORMAT> formats);
            GraphicsPipelineInfo &depthFormat(Format format);
            GraphicsPipelineInfo &depthMode(info::DepthMode mode);
            GraphicsPipelineInfo &inputLayout(const info::InputLayoutInfo &ilInfo);
            GraphicsPipelineInfo &multisampling(uint samples, uint quality = 0);
            GraphicsPipelineInfo &topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type);
            GraphicsPipelineInfo &fill(D3D12_FILL_MODE fillMode);
            GraphicsPipelineInfo &cull(D3D12_CULL_MODE cullMode);
            GraphicsPipelineInfo &winding(bool counterClockWise);
            GraphicsPipelineInfo &blend(uint renderTarget,
                                        bool enabled = false,
                                        D3D12_BLEND src = D3D12_BLEND_ONE,
                                        D3D12_BLEND dst = D3D12_BLEND_INV_SRC_ALPHA,
                                        D3D12_BLEND_OP op = D3D12_BLEND_OP_ADD);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc() const;
        };
    }

    namespace backend
    {
        struct ShaderLoader
        {
            struct ShaderData
            {
                std::shared_ptr<const BuildInfo> buildInfo;
                std::unordered_map<PipelineState *, std::weak_ptr<PipelineState>> users;
                uint64_t timestamp = 0;

                void rebuildPipelines();
                bool isOutOfDate() const
                {
                    return timestamp < buildInfo->sourceTimestamp();
                }
            };

            std::unordered_map<String, std::shared_ptr<ShaderData>> shaderData;
            std::vector<String> shaderScanQueue;
            size_t shaderScanIndex = 0;

            void scanChangedSources();
            void registerBuildInfo(std::shared_ptr<const BuildInfo> buildInfo);

            void registerShaderTlog(StringView projectName,
                                    StringView shaderTlogPath);
        };

        struct ResourceState : DeviceChild
        {
            ComPtr<ID3D12Resource> resource;
            mutable D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

            ~ResourceState();
        };

        struct DescriptorViewState : DeviceChild
        {
            Descriptor descriptor;

            ~DescriptorViewState();
        };

        struct RootSignature
        {
            ComPtr<ID3D12RootSignature> rs;
            unsigned numCBVs = 0;
            unsigned numSRVs = 0;
            unsigned numUAVs = 0;
        };

        struct ShaderBinary;

        struct PipelineState
            : std::enable_shared_from_this<PipelineState>
            , DeviceChild
        {
            std::shared_ptr<info::GraphicsPipelineInfo> graphicsInfo;
            ComPtr<ID3D12PipelineState> pso;
            RootSignature rootSignature;

            backend::ShaderBinary loadShader(ShaderLoader &loader, StringView name);

            void reload();
            void releasePSO();

            ~PipelineState();
        };

    }

    class GraphicsPipeline : private backend::SharedState<backend::PipelineState>
    {
        friend class Device;
        friend class CommandList;
    public:
        GraphicsPipeline() = default;

        using Info = info::GraphicsPipelineInfo;
    };

    class Buffer : public backend::ResourceWithInfo<info::BufferInfoBuilder>
    {
    public:
        Buffer() = default;
    };

    class Texture : public backend::ResourceWithInfo<info::TextureInfoBuilder>
    {
    public:
        Texture() = default;
    };

    class DescriptorView : private backend::SharedState<backend::DescriptorViewState>
    {
        friend class Device;
        friend class CommandList;
    public:
        DescriptorView() = default;

        explicit operator bool() const { return valid(); }
    };

    class TextureView : public DescriptorView
    {
        friend class Device;
        friend class CommandList;
        Texture m_texture;
    public:
        using Info    = info::TextureViewInfo;
        using Builder = info::TextureViewInfoBuilder;

        Texture texture();
    };

    class TextureRTV : public TextureView
    {
    public:
    };

    class TextureDSV : public TextureView
    {
    public:
    };

    class TextureSRV : public TextureView
    {
    public:
    };

    class BufferVBV
    {
        friend class Device;
        friend class CommandList;
        mutable Buffer           m_buffer;
        D3D12_VERTEX_BUFFER_VIEW m_vbv;
    public:
        BufferVBV() = default;
        Buffer buffer() { return m_buffer; }

        using Info    = info::BufferViewInfo;
        using Builder = info::BufferViewInfoBuilder;
    };

    class BufferIBV
    {
        friend class Device;
        friend class CommandList;
        mutable Buffer          m_buffer;
        D3D12_INDEX_BUFFER_VIEW m_ibv;
    public:
        BufferIBV() = default;
        Buffer buffer() { return m_buffer; }

        using Info    = info::BufferViewInfo;
        using Builder = info::BufferViewInfoBuilder;
    };
}
