#include "Xor/XorBackend.hpp"
#include "Xor/XorDevice.hpp"
#include "Xor/XorCommandList.hpp"

namespace Xor
{
    namespace backend
    {
        ComPtr<IDXGIFactory4> dxgiFactory()
        {
            ComPtr<IDXGIFactory4> factory;

            XOR_CHECK_HR(CreateDXGIFactory1(
                __uuidof(IDXGIFactory4),
                &factory));

            return factory;
        }

        void setName(ComPtr<ID3D12Object> object, const String &name)
        {
            object->SetName(name.wideStr().c_str());
        }

        void DeviceChild::setParent(Device *device)
        {
            m_parentDevice = device->m_state;
        }

        Device DeviceChild::device()
        {
            return Device(m_parentDevice.lock());
        }
    }
}
