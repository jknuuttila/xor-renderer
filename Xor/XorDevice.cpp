#include "Xor/XorBackend.hpp"
#include "Xor/XorDevice.hpp"
#include "Xor/XorDeviceState.hpp"
#include "Xor/XorResources.hpp"
#include "Xor/XorCommandList.hpp"

namespace xor
{
    using namespace xor::backend;

    namespace backend
    {
        struct SwapChainState : DeviceChild
        {
            ComPtr<IDXGISwapChain3> swapChain;

            struct Backbuffer
            {
                SeqNum seqNum = InvalidSeqNum;
                TextureRTV rtvSRGB;
                TextureRTV rtvGamma;
            };
            std::vector<Backbuffer> backbuffers;

            ~SwapChainState()
            {
                device().waitUntilDrained();
            }
        };

    }

    Xor::Xor(DebugLayer debugLayer)
    {
        if (debugLayer == DebugLayer::Default)
        {
#if defined(_DEBUG)
            debugLayer = DebugLayer::Enabled;
#else
            debugLayer = DebugLayer::Disabled;
#endif
        }

        if (debugLayer == DebugLayer::Enabled)
        {
            ComPtr<ID3D12Debug> debug;
            XOR_CHECK_HR(D3D12GetDebugInterface(
                __uuidof(ID3D12Debug),
                &debug));
            debug->EnableDebugLayer();
        }

        auto factory = dxgiFactory();

        m_shaderLoader = std::make_shared<ShaderLoader>();

        {
            uint i = 0;
            bool foundAdapters = true;
            while (foundAdapters)
            {
                ComPtr<IDXGIAdapter1> adapter;
                auto hr = factory->EnumAdapters1(i, &adapter);

                switch (hr)
                {
                case S_OK:
                    m_adapters.emplace_back();
                    {
                        auto &a = m_adapters.back();
                        XOR_CHECK_HR(adapter.As(&a.m_adapter));
                        DXGI_ADAPTER_DESC2 desc = {};
                        XOR_CHECK_HR(a.m_adapter->GetDesc2(&desc));
                        a.m_description  = String(desc.Description);
                        a.m_debug        = debugLayer == DebugLayer::Enabled;
                        a.m_shaderLoader = m_shaderLoader;
                    }
                    break;
                case DXGI_ERROR_NOT_FOUND:
                    foundAdapters = false;
                    break;
                default:
                    // This fails.
                    XOR_CHECK_HR(hr);
                    break;
                }

                ++i;
            }
        }
    }

    Xor::~Xor()
    {
    }

    Span<Adapter> Xor::adapters()
    {
        return asSpan(m_adapters);
    }

    Adapter & Xor::defaultAdapter()
    {
        XOR_CHECK(!m_adapters.empty(), "No adapters detected!");
        return m_adapters.front();
    }

    Device Xor::defaultDevice()
    {
        for (auto &adapter : m_adapters)
        {
            if (Device device = adapter.createDevice())
                return device;
        }

        XOR_CHECK(false, "Failed to find a Direct3D 12 device.");

        return Device();
    }

    void Xor::registerShaderTlog(StringView projectName, StringView shaderTlogPath)
    {
        m_shaderLoader->registerShaderTlog(projectName, shaderTlogPath);
    }

    Device Adapter::createDevice()
    {
        ComPtr<ID3D12Device> device;

        auto hr = D3D12CreateDevice(
            m_adapter.Get(),
            D3D_FEATURE_LEVEL_12_0,
            __uuidof(ID3D12Device),
            &device);

        if (FAILED(hr))
        {
            log("Adapter", "Failed to create device: %s\n", errorMessage(hr).cStr());
            return Device();
        }

        XOR_INTERNAL_DEBUG_NAME(device);

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (m_debug && device.As(&infoQueue) == S_OK)
        {
            XOR_CHECK_HR(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
            XOR_CHECK_HR(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      TRUE));
            XOR_CHECK_HR(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    TRUE));

            // The graphics debugger generates these, so disable.
            D3D12_MESSAGE_SEVERITY disabledSeverities[] = {
                D3D12_MESSAGE_SEVERITY_INFO,
            };
            D3D12_MESSAGE_ID disabledMessages[] = {
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
            };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumSeverities  = static_cast<uint>(size(disabledSeverities));
            filter.DenyList.NumIDs         = static_cast<uint>(size(disabledMessages));
            filter.DenyList.pSeverityList  = disabledSeverities;
            filter.DenyList.pIDList        = disabledMessages;
            XOR_CHECK_HR(infoQueue->PushStorageFilter(&filter));
        }

        return Device(*this,
                      std::move(device),
                      m_shaderLoader);
    }

    Device::Device(Adapter adapter,
                   ComPtr<ID3D12Device> device,
                   std::shared_ptr<backend::ShaderLoader> shaderLoader)
    {
        makeState(std::move(adapter), std::move(device), std::move(shaderLoader));

        S().shaderLoader->registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        {
            uint8_t *pixels = nullptr;
            int2 size;

            ImGuiIO &io = ImGui::GetIO();
            io.DeltaTime = 1.f/60.f;
            io.Fonts->AddFontDefault();
            io.Fonts->GetTexDataAsAlpha8(&pixels, &size.x, &size.y);

            ImGuiStyle &style = ImGui::GetStyle();
            style.Colors[ImGuiCol_FrameBg]        = float4(70, 70, 70, 77) / 255;
            style.Colors[ImGuiCol_FrameBgHovered] = float4(110, 110, 110, 102) / 255;
            style.Colors[ImGuiCol_FrameBgActive]  = float4(200, 70, 70, 102) / 255;

            ImageData data;
            data.size   = uint2(size);
            data.format = DXGI_FORMAT_R8_UNORM;
            data.setDefaultSizes();
            data.data = Span<const uint8_t>(pixels, data.sizeBytes());

            S().imgui.fontAtlas = createTextureSRV(Texture::Info(data));

            io.KeyMap[ImGuiKey_Tab]        = VK_TAB;
            io.KeyMap[ImGuiKey_LeftArrow]  = VK_LEFT;
            io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
            io.KeyMap[ImGuiKey_UpArrow]    = VK_UP;
            io.KeyMap[ImGuiKey_DownArrow]  = VK_DOWN;
            io.KeyMap[ImGuiKey_PageUp]     = VK_PRIOR;
            io.KeyMap[ImGuiKey_PageDown]   = VK_NEXT;
            io.KeyMap[ImGuiKey_Home]       = VK_HOME;
            io.KeyMap[ImGuiKey_End]        = VK_END;
            io.KeyMap[ImGuiKey_Delete]     = VK_DELETE;
            io.KeyMap[ImGuiKey_Backspace]  = VK_BACK;
            io.KeyMap[ImGuiKey_Enter]      = VK_RETURN;
            io.KeyMap[ImGuiKey_Escape]     = VK_ESCAPE;
            io.KeyMap[ImGuiKey_A]          = 'A';
            io.KeyMap[ImGuiKey_C]          = 'C';
            io.KeyMap[ImGuiKey_V]          = 'V';
            io.KeyMap[ImGuiKey_X]          = 'X';
            io.KeyMap[ImGuiKey_Y]          = 'Y';
            io.KeyMap[ImGuiKey_Z]          = 'Z';
        }

        S().imgui.imguiRenderer = createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("ImguiRenderer.vs")
            .pixelShader("ImguiRenderer.ps")
            .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM)
            .winding(false)
            .blend(0, true)
            .inputLayout(info::InputLayoutInfoBuilder()
                         .element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM)));
    }

    Device::Device(StatePtr state)
    {
        m_state = std::move(state);
    }

    SwapChain Device::createSwapChain(Window &window)
    {
        static const uint BufferCount = 2;

        auto factory = dxgiFactory();

        SwapChain swapChain;
        swapChain.makeState().setParent(this);

        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width              = window.size().x;
            desc.Height             = window.size().y;
            desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Stereo             = false;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount        = BufferCount;
            desc.Scaling            = DXGI_SCALING_NONE;
            desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags              = 0;

            ComPtr<IDXGISwapChain1> swapChain1;
            XOR_CHECK_HR(factory->CreateSwapChainForHwnd(
                S().graphicsQueue.Get(),
                window.hWnd(),
                &desc,
                nullptr,
                nullptr,
                &swapChain1));

            XOR_CHECK_HR(swapChain1.As(&swapChain.S().swapChain));
        }

        for (uint i = 0; i < BufferCount; ++i)
        {
            SwapChainState::Backbuffer bb;

            auto &tex = bb.rtvSRGB.m_texture;
            tex.makeState().setParent(this);
            XOR_CHECK_HR(swapChain.S().swapChain->GetBuffer(
                i,
                __uuidof(ID3D12Resource),
                &tex.S().resource));

            tex.makeInfo() = Texture::Info(tex.S().resource.Get());

            bb.rtvGamma.m_texture = bb.rtvSRGB.m_texture;

            bb.rtvSRGB.makeState().setParent(this);
            bb.rtvGamma.makeState().setParent(this);

            bb.rtvSRGB.S().descriptor  = S().rtvs.allocateFromHeap();
            bb.rtvGamma.S().descriptor = S().rtvs.allocateFromHeap();

            {
                D3D12_RENDER_TARGET_VIEW_DESC desc = {};
                desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                desc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice   = 0;
                desc.Texture2D.PlaneSlice = 0;

                device()->CreateRenderTargetView(
                    tex.S().resource.Get(),
                    &desc,
                    bb.rtvSRGB.S().descriptor.cpu);

                desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
                device()->CreateRenderTargetView(
                    tex.S().resource.Get(),
                    &desc,
                    bb.rtvGamma.S().descriptor.cpu);
            }

            swapChain.S().backbuffers.emplace_back(std::move(bb));
        }

        return swapChain;
    }

    GraphicsPipeline Device::createGraphicsPipeline(const GraphicsPipeline::Info &info)
    {

        GraphicsPipeline pipeline;
        pipeline.makeState().setParent(this);
        pipeline.S().graphicsInfo = std::make_shared<GraphicsPipeline::Info>(info);
        pipeline.S().reload();
        return pipeline;
    }

    RootSignature Device::collectRootSignature(const D3D12_SHADER_BYTECODE &shader)
    { 
        RootSignature rs;

        XOR_CHECK_HR(device()->CreateRootSignature(
            0,
            shader.pShaderBytecode,
            shader.BytecodeLength,
            __uuidof(ID3D12RootSignature),
            &rs.rs));

        ComPtr<ID3D12RootSignatureDeserializer> deserializer;
        XOR_CHECK_HR(D3D12CreateRootSignatureDeserializer(
            shader.pShaderBytecode,
            shader.BytecodeLength,
            __uuidof(ID3D12RootSignatureDeserializer),
            &deserializer));
        auto desc = deserializer->GetRootSignatureDesc();

        for (uint i = 0; i < desc->NumParameters; ++i)
        {
            auto &p = desc->pParameters[i];

            if (p.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                continue;

            for (uint j = 0; j < p.DescriptorTable.NumDescriptorRanges; ++j)
            {
                auto &dr = p.DescriptorTable.pDescriptorRanges[j];
                switch (dr.RangeType)
                {
                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                    rs.numCBVs = dr.NumDescriptors;
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                    rs.numSRVs = dr.NumDescriptors;
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                    rs.numUAVs = dr.NumDescriptors;
                    break;
                default:
                    break;
                }
            }
        }

        return rs;
    }

    HeapBlock Device::uploadBytes(Span<const uint8_t> bytes, SeqNum cmdListNumber, uint alignment)
    {
        HeapBlock block;
        block.heap = S().uploadHeap->heap.Get();
        block.block = S().uploadHeap->uploadBytes(
            S().progress,
            bytes,
            cmdListNumber,
            alignment);
        return block;
    }

    ID3D12Device * Device::device()
    {
        return S().device.Get();
    }

    CommandList Device::initializerCommandList()
    {
        return graphicsCommandList();
    }

    Buffer Device::createBuffer(const Buffer::Info & info)
    {
        Buffer buffer;
        buffer.makeInfo() = info;
        buffer.makeState().setParent(this);

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type                 = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask     = 0;
        heap.VisibleNodeMask      = 0;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width              = info.sizeBytes();
        desc.Height             = 1;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        XOR_CHECK_HR(device()->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            buffer.S().state,
            nullptr,
            __uuidof(ID3D12Resource),
            &buffer.S().resource));

        if (info.m_initializer)
        {
            auto initCmd = initializerCommandList();
            info.m_initializer(initCmd, buffer);
            execute(initCmd);
        }

        return buffer;
    }

    BufferVBV Device::createBufferVBV(Buffer buffer, const BufferVBV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(buffer.info());

        BufferVBV vbv;

        vbv.m_buffer              = buffer;
        vbv.m_vbv.BufferLocation  = buffer.S().resource->GetGPUVirtualAddress();
        vbv.m_vbv.BufferLocation += info.firstElement * info.format.size();
        vbv.m_vbv.SizeInBytes     = info.sizeBytes();
        vbv.m_vbv.StrideInBytes   = info.format.size();

        return vbv;
    }

    BufferVBV Device::createBufferVBV(const Buffer::Info & bufferInfo, const BufferVBV::Info & viewInfo)
    {
        return createBufferVBV(createBuffer(bufferInfo), viewInfo);
    }

    BufferIBV Device::createBufferIBV(Buffer buffer, const BufferIBV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(buffer.info());

        BufferIBV ibv;

        ibv.m_buffer              = buffer;
        ibv.m_ibv.BufferLocation  = buffer.S().resource->GetGPUVirtualAddress();
        ibv.m_ibv.BufferLocation += info.firstElement * info.format.size();
        ibv.m_ibv.SizeInBytes     = info.sizeBytes();
        ibv.m_ibv.Format          = info.format;

        return ibv;
    }

    BufferIBV Device::createBufferIBV(const Buffer::Info & bufferInfo, const BufferIBV::Info & viewInfo)
    {
        return createBufferIBV(createBuffer(bufferInfo), viewInfo);
    }

    static D3D12_RESOURCE_FLAGS textureFlags(const Texture::Info &info)
    {
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

        if (info.format.isDepthFormat())
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        return flags;
    }

    Texture Device::createTexture(const Texture::Info & info)
    {
        Texture texture;
        texture.makeInfo() = info;
        texture.makeState().setParent(this);

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type                  = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask      = 0;
        heap.VisibleNodeMask       = 0;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment           = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width               = info.size.x;
        desc.Height              = info.size.y;
        desc.DepthOrArraySize    = 1;
        desc.MipLevels           = info.mipLevels;
        desc.Format              = info.format;
        desc.SampleDesc.Count    = 1;
        desc.SampleDesc.Quality  = 0;
        desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags               = textureFlags(info);

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = info.format;
        if (info.format.isDepthFormat())
        {
            clearValue.DepthStencil.Depth   = 0;
            clearValue.DepthStencil.Stencil = 0;
        }
        else
        {
            clearValue.Color[0] = 0;
            clearValue.Color[1] = 0;
            clearValue.Color[2] = 0;
            clearValue.Color[3] = 0;
        }

        bool hasClearValue =
            !!(desc.Flags &
                ( D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET));

        XOR_CHECK_HR(device()->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            texture.S().state,
            hasClearValue ? &clearValue : nullptr,
            __uuidof(ID3D12Resource),
            &texture.S().resource));

        if (info.m_initializer)
        {
            auto initCmd = initializerCommandList();
            info.m_initializer(initCmd, texture);
            execute(initCmd);
        }

        return texture;
    }

    TextureSRV Device::createTextureSRV(Texture texture, const TextureSRV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(texture.info());

        TextureSRV srv;
        srv.m_texture = texture;
        srv.makeState().setParent(this);
        srv.S().descriptor = S().shaderViews.allocateFromHeap();

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format                          = info.format;
        desc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MostDetailedMip       = 0;
        desc.Texture2D.MipLevels             = -1;
        desc.Texture2D.PlaneSlice            = 0;
        desc.Texture2D.ResourceMinLODClamp   = 0;

        device()->CreateShaderResourceView(
            texture.get(),
            &desc,
            srv.S().descriptor.staging);

        return srv;
    }

    TextureSRV Device::createTextureSRV(const Texture::Info & textureInfo, const TextureSRV::Info & viewInfo)
    {
        return createTextureSRV(createTexture(textureInfo), viewInfo);
    }

    TextureDSV Device::createTextureDSV(Texture texture, const TextureDSV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(texture.info());

        TextureDSV dsv;
        dsv.m_texture = texture;
        dsv.makeState().setParent(this);
        dsv.S().descriptor = S().dsvs.allocateFromHeap();

        D3D12_DEPTH_STENCIL_VIEW_DESC desc   = {};
        desc.Format                          = info.format;
        desc.ViewDimension                   = D3D12_DSV_DIMENSION_TEXTURE2D;
        desc.Flags                           = D3D12_DSV_FLAG_NONE;
        desc.Texture2D.MipSlice              = 0;

        device()->CreateDepthStencilView(
            texture.get(),
            &desc,
            dsv.S().descriptor.cpu);

        return dsv;
    }

    TextureDSV Device::createTextureDSV(const Texture::Info & textureInfo, const TextureDSV::Info & viewInfo)
    {
        return createTextureDSV(createTexture(textureInfo), viewInfo);
    }

    CommandList Device::graphicsCommandList()
    {
        CommandList cmd = S().freeGraphicsCommandLists.allocate([this]
        {
            return std::make_shared<CommandListState>(*this);
        });

        cmd.reset();
        ID3D12DescriptorHeap *heaps[] = { S().shaderViews.get() };
        cmd.cmd()->SetDescriptorHeaps(1, heaps);

        ++cmd.S().timesStarted;
        cmd.S().seqNum = S().progress.startNewCommandList();

        return cmd;
    }

    void Device::execute(CommandList &cmd)
    {
        cmd.close();

        ID3D12CommandList *cmds[] = { cmd.S().cmd.Get() };
        S().graphicsQueue->ExecuteCommandLists(1, cmds);
        S().graphicsQueue->Signal(cmd.S().timesCompleted.Get(), cmd.S().timesStarted);

        S().progress.executeCommandList(std::move(cmd));
    }

    void Device::present(SwapChain & swapChain, bool vsync)
    {
        auto &backbuffer = swapChain.S().backbuffers[swapChain.currentIndex()];

        {
            auto toPresent = graphicsCommandList();
            toPresent.transition(backbuffer.rtvSRGB.m_texture, D3D12_RESOURCE_STATE_PRESENT);
            execute(toPresent);
        }

        // The backbuffer is assumed to depend on all command lists
        // that have been executed, but not on those which have
        // been started but not executed. Otherwise, deadlock could result.
        backbuffer.seqNum = S().progress.newestExecuted;
        swapChain.S().swapChain->Present(vsync ? 1 : 0, 0);
        S().shaderLoader->scanChangedSources();
        S().progress.retireCommandLists();
    }

    Device::ImguiInput Device::imguiInput(const Input & input)
    {
        ImGuiIO &io = ImGui::GetIO();

        if (!input.mouseMovements.empty())
            io.MousePos = float2(input.mouseMovements.back().position);

        for (auto &w : input.mouseWheel)
        {
            if (w.delta > 0)
                io.MouseWheel += 1;
            else if (w.delta < 0)
                io.MouseWheel -= 1;
        }

        for (auto &k : input.keyEvents)
        {
            switch (k.code)
            {
            case VK_LBUTTON:
                io.MouseDown[0] = k.pressed; break;
            case VK_RBUTTON:
                io.MouseDown[1] = k.pressed; break;
            case VK_MBUTTON:
                io.MouseDown[2] = k.pressed; break;
            default:
                if (k.code < 512)
                    io.KeysDown[k.code] = k.pressed;
                break;
            }
        }

        for (auto ch : input.characterInput)
            io.AddInputCharacter(static_cast<ImWchar>(ch));

        io.KeyCtrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        io.KeyShift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        io.KeyAlt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
        io.KeySuper = false;

        ImguiInput retval;
        retval.wantsKeyboard = io.WantCaptureKeyboard;
        retval.wantsMouse    = io.WantCaptureMouse;
        retval.wantsText     = io.WantTextInput;
        return retval;
    }

    SeqNum Device::now()
    {
        return S().progress.now();
    }

    void Device::whenCompleted(std::function<void()> f)
    {
        whenCompleted(std::move(f), now());
    }

    void Device::whenCompleted(std::function<void()> f, SeqNum seqNum)
    {
        S().progress.whenCompleted(std::move(f), seqNum);
    }

    bool Device::hasCompleted(SeqNum seqNum)
    {
        return S().progress.hasCompleted(seqNum);
    }

    void Device::waitUntilCompleted(SeqNum seqNum)
    {
        S().progress.waitUntilCompleted(seqNum);
    }

    void Device::waitUntilDrained()
    {
        S().progress.waitUntilDrained();
    }

    backend::ShaderLoader & Device::shaderLoader()
    {
        return *S().shaderLoader;
    }

    void Device::releaseDescriptor(Descriptor descriptor)
    {
        S().viewHeap(descriptor.type).release(descriptor);
    }

    void Device::releaseCommandList(std::shared_ptr<backend::CommandListState> cmdList)
    {
        S().freeGraphicsCommandLists.release(std::move(cmdList));
    }

    uint SwapChain::currentIndex()
    {
        // Block until the current backbuffer has finished rendering.
        for (;;)
        {
            uint index = S().swapChain->GetCurrentBackBufferIndex();

            auto device = S().device();

            auto &cur = S().backbuffers[index];
            if (cur.seqNum < 0 || device.hasCompleted(cur.seqNum))
                return index;

            // If we got here, the backbuffer was presented, but hasn't finished yet.
            device.waitUntilCompleted(cur.seqNum);
        }
    }

    TextureRTV SwapChain::backbuffer(bool sRGB)
    {
        auto &bb = S().backbuffers[currentIndex()];
        return sRGB ? bb.rtvSRGB : bb.rtvGamma;
    }

    Texture TextureView::texture()
    {
        return m_texture;
    }

}