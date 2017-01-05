#pragma once

#include "Core/Core.hpp"

#include "Xor/Format.hpp"

#include "external/imgui/imgui.h"

#include <d3d12.h>
#include <dxgi1_5.h>
#include <pix.h>

#include <vector>
#include <unordered_map>
#include <initializer_list>
#include <queue>
#include <memory>

namespace xor
{
    class Device;
    class CommandList;
    class SwapChain;
    class Adapter;
    class Buffer;
    class BufferIBV;
    class BufferVBV;
    class Texture;
    class TextureSRV;
    class TextureRTV;
    class TextureDSV;

    static const uint DefaultAlignment = 4;

#define XOR_INTERNAL_DEBUG_NAME(variable) setName(variable, #variable)

    namespace backend
    {
        class DeviceChild;
        struct Descriptor;
        class ViewHeap;
        struct ShaderLoader;
        struct DeviceState;
        struct CommandListState;
        struct SwapChainState;
        struct ResourceState;
        struct DescriptorViewState;
        struct PipelineState;
        struct BufferState;
        struct GPUProgressTracking;
        struct RootSignature;

        ComPtr<IDXGIFactory4> dxgiFactory();
        void setName(ComPtr<ID3D12Object> object, const String & name);

        template <typename T>
        class SharedState
        {
        protected:
            using State    = T;
            using StatePtr = std::shared_ptr<T>;

            StatePtr m_state;
        public:

            template <typename... Ts>
            T &makeState(Ts &&... ts)
            {
                m_state = std::make_shared<T>(std::forward<Ts>(ts)...);
                return S();
            }

            const T &S() const { return *m_state; }
            T &S() { return *m_state; }

            bool valid() const { return !!m_state; }
        };

        struct Descriptor
        {
            int64_t offset = 0;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu     = { 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE gpu     = { 0 };
            D3D12_CPU_DESCRIPTOR_HANDLE staging = { 0 };
            D3D12_DESCRIPTOR_HEAP_TYPE  type    = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        };

        class Resource : private backend::SharedState<backend::ResourceState>
        {
            friend class Device;
            friend class CommandList;
        public:
            Resource() = default;

            explicit operator bool() const { return valid(); }
            ID3D12Resource *get() const;
        };

        template <typename ResourceInfoBuilder>
        class ResourceWithInfo : public Resource
        {
            friend class Device;
            friend class CommandList;
        public:
            using Info    = typename ResourceInfoBuilder::Info;
            using Builder = ResourceInfoBuilder;

            const Info &info() const { return *m_info; }
            const Info *operator->() const { return m_info.get(); }

        protected:
            std::shared_ptr<Info> m_info;
            Info &makeInfo()
            {
                m_info = std::make_shared<Info>();
                return *m_info;
            }
        };

        struct HeapBlock
        {
            ID3D12Resource *heap = nullptr;
            Block block;
        };

        class DeviceChild
        {
            std::weak_ptr<DeviceState> m_parentDevice;
        public:
            DeviceChild() = default;
            DeviceChild(std::weak_ptr<DeviceState> device)
                : m_parentDevice(device)
            {}

            void setParent(Device *device);
            Device device();
        };

        struct CompletionCallback
        {
            SeqNum seqNum = InvalidSeqNum;
            std::function<void()> f;

            CompletionCallback(SeqNum seqNum, std::function<void()> f)
                : seqNum(seqNum)
                , f(std::move(f))
            {}

            // Smallest seqNum goes first in a priority queue.
            bool operator<(const CompletionCallback &c) const
            {
                return seqNum > c.seqNum;
            }
        };

    }

}
