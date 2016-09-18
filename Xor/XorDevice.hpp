#pragma once

#include "Core/Core.hpp"

#include "Xor/Shaders.h"

#include "Xor/XorResources.hpp"
#include "Xor/XorBackend.hpp"

namespace xor
{
    class Adapter
    {
        friend class Xor;

        ComPtr<IDXGIAdapter3>                  m_adapter;
        std::shared_ptr<backend::ShaderLoader> m_shaderLoader;
        String                                 m_description;
        bool                                   m_debug = false;
    public:
        Device createDevice();
    };

    class Device : private backend::SharedState<backend::DeviceState>
    {
        friend class Adapter;
        friend class CommandList;
        friend class DescriptorView;
        friend class SwapChain;
        friend class backend::DeviceChild;
        friend struct backend::DeviceState;
        friend struct backend::CommandListState;
        friend struct backend::SwapChainState;
        friend struct backend::ResourceState;
        friend struct backend::DescriptorViewState;
        friend struct backend::PipelineState;
        friend class info::BufferInfo;
        friend class info::TextureInfo;

        ID3D12Device *device();

        backend::ShaderLoader &shaderLoader();
        void releaseDescriptor(backend::Descriptor descriptor);
        void releaseCommandList(std::shared_ptr<backend::CommandListState> cmdList);

        CommandList initializerCommandList();
        void initializeBufferWith(Buffer &buffer, Span<const uint8_t> bytes);
        void initializeTextureWith(Texture &texture, Span<const ImageData> subresources);

        backend::RootSignature collectRootSignature(const D3D12_SHADER_BYTECODE &shader);

        backend::HeapBlock uploadBytes(Span<const uint8_t> bytes, SeqNum cmdListNumber, uint alignment);

        Device(Adapter adapter,
               ComPtr<ID3D12Device> device,
               std::shared_ptr<backend::ShaderLoader> shaderLoader);
        Device(StatePtr state);
    public:
        Device() = default;

        explicit operator bool() const { return valid(); }

        SwapChain createSwapChain(Window &window);
        GraphicsPipeline createGraphicsPipeline(const GraphicsPipeline::Info &info);

        Buffer    createBuffer(const Buffer::Info &info);
        BufferVBV createBufferVBV(Buffer buffer                 , const BufferVBV::Info &viewInfo = BufferVBV::Info());
        BufferVBV createBufferVBV(const Buffer::Info &bufferInfo, const BufferVBV::Info &viewInfo = BufferVBV::Info());
        BufferIBV createBufferIBV(Buffer buffer                 , const BufferIBV::Info &viewInfo = BufferIBV::Info());
        BufferIBV createBufferIBV(const Buffer::Info &bufferInfo, const BufferIBV::Info &viewInfo = BufferIBV::Info());

        Texture    createTexture(const Texture::Info &info);
        TextureSRV createTextureSRV(Texture texture                 , const TextureSRV::Info &viewInfo = TextureSRV::Info());
        TextureSRV createTextureSRV(const Texture::Info &textureInfo, const TextureSRV::Info &viewInfo = TextureSRV::Info());
        TextureDSV createTextureDSV(Texture texture                 , const TextureDSV::Info &viewInfo = TextureDSV::Info());
        TextureDSV createTextureDSV(const Texture::Info &textureInfo, const TextureDSV::Info &viewInfo = TextureDSV::Info());

        CommandList graphicsCommandList();

        void execute(CommandList &cmd);
        void present(SwapChain &swapChain, bool vsync = true);

        struct ImguiInput
        {
            bool wantsMouse    = false;
            bool wantsKeyboard = false;
            bool wantsText     = false;
        };
        ImguiInput imguiInput(const Input &input);

        SeqNum now();
        void whenCompleted(std::function<void()> f);
        void whenCompleted(std::function<void()> f, SeqNum seqNum);
        bool hasCompleted(SeqNum seqNum);
        void waitUntilCompleted(SeqNum seqNum);
        void waitUntilDrained();
    };

    class SwapChain : private backend::SharedState<backend::SwapChainState>
    {
        friend class Device;
        uint currentIndex();

    public:
        SwapChain() = default;

        TextureRTV backbuffer(bool sRGB = true);
    };

    // Global initialization and deinitialization of the Xor renderer.
    class Xor
    {
    public:
        enum class DebugLayer
        {
            Default,
            Enabled,
            Disabled,
        };

    private:
        std::vector<Adapter>                   m_adapters;
        std::shared_ptr<backend::ShaderLoader> m_shaderLoader;

    public:

        Xor(DebugLayer debugLayer = DebugLayer::Default);
        ~Xor();

        Span<Adapter> adapters();
        Adapter &defaultAdapter();
        Device defaultDevice();

        void registerShaderTlog(StringView projectName,
                                StringView shaderTlogPath);
    };
}
