#pragma once

// We are using an older version of DirectX headers which causes 
// "warning C4005: '...' : macro redefinition"
#pragma warning(push)
#pragma warning(disable: 4005)
#include <d3d11.h>
#include <DirectXMath.h>
#pragma warning(pop)

#include <cstdint>

using namespace DirectX;

class DX11Renderer;

// Used by a scene to access necessary renderer internals
class IRenderingContext // TODO - this should be renamed as it is no longer an interface
{
public:

    IRenderingContext(ID3D11Device* d, ID3D11DeviceContext* c, DX11Renderer* r) {
        Init(d, c, r);
    };

    IRenderingContext() = default;

    void Init(ID3D11Device* d, ID3D11DeviceContext* c, DX11Renderer* r) {
        m_device = d;  
        m_context = c;
        m_renderer = r;
    }; 

    virtual ID3D11Device* GetDevice() {
        return m_device;
    }

    virtual ID3D11DeviceContext*    GetImmediateContext() {
        return m_context;
    }

    virtual float                   GetFrameAnimationTime() const { return 0; }; // In seconds

    virtual bool IsValid()
    {
        return GetDevice() && GetImmediateContext();
    };

    DX11Renderer* getDXRenderer() {
        return m_renderer;
    }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    DX11Renderer* m_renderer;
};
