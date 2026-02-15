#include "framework.h"
#include "Render.h"

#include <filesystem>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment (lib, "d3dcompiler.lib")
#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "dxgi.lib")

#define DX_CHECK(hr, msg) \
    if (FAILED(hr)) { \
        std::wstring fullMsg = L"[FATAL ERROR] "; \
        fullMsg += msg; \
        fullMsg += L"\nError Code: " + std::to_wstring(hr); \
        OutputDebugString(fullMsg.c_str()); \
        OutputDebugString(L"\n"); \
        MessageBox(nullptr, fullMsg.c_str(), L"DirectX Critical Error", MB_OK | MB_ICONERROR); \
        return hr; \
    }

struct Vertex
{
    float x, y, z;
    COLORREF color;
};

struct MatrixBuffer
{
    XMMATRIX m;
};

void Render::SetDObjName(ID3D11DeviceChild* resource, const std::string& name) {
    if (resource) {
        resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
    }
}


Render::Render(HWND hWnd) : m_hWnd(hWnd), camera(nullptr), m_currentModel(nullptr),
    m_pDevice(nullptr),
    m_pDeviceContext(nullptr),
    m_pSwapChain(nullptr),
    m_pRenderTargetView(nullptr),
    m_pPixelShader(nullptr),
    m_pVertexShader(nullptr),
    m_pInputLayout(nullptr),
    m_szTitle(nullptr),
    m_szWindowClass(nullptr){}

HRESULT Render::Init(WCHAR szTitle[], WCHAR szWindowClass[])
{
    m_szTitle = szTitle;
    m_szWindowClass = szWindowClass;

    HRESULT hr;
    ModelFactory::ModelCode init_code = ModelFactory::ModelCode::cube;

    IDXGIFactory* pFactory = nullptr;
    hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    DX_CHECK(hr, L"Не удалось создать DXGIFactory");
    IDXGIAdapter* pAdapter = nullptr;
    hr = pFactory->EnumAdapters(0, &pAdapter);
    
    if (FAILED(hr)) {
        pFactory->Release();
        DX_CHECK(hr, L"Не удалось получить графический адаптер");
        return hr;
    }

    // Создание устройства Direct3D 11
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        pAdapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
#ifdef _DEBUG
        D3D11_CREATE_DEVICE_DEBUG,
#else
        0,
#endif
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &m_pDevice,
        &featureLevel,
        &m_pDeviceContext
    );

    if (FAILED(hr)) {
        
        pAdapter->Release();
        pFactory->Release();
        DX_CHECK(hr, L"Не удалось создать D3D11 устройство");
        return hr;
    }

    hr = m_pDeviceContext->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void**)&m_pAnnotation);
    if (FAILED(hr)) {
        // Это не смертельно, просто не будет меток в отладчике
        OutputDebugString(L"[WARNING] ID3DUserDefinedAnnotation not found\n");
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = m_hWnd;
    swapChainDesc.SampleDesc.Count = 4;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = true;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapChainDesc.Flags = 0;

    hr = pFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
    pAdapter->Release();
    pFactory->Release();

    if (FAILED(hr)) {
        DX_CHECK(hr, L"Не удалось создать цепочку подкачки (SwapChain)");
        return hr;
    }

    if (SUCCEEDED(hr)) hr = ConfigureBackBuffer();
    if (SUCCEEDED(hr)) hr = InitGeometry(init_code);
    if (SUCCEEDED(hr)) hr = InitBufferShader();

    if (FAILED(hr))
    {
        Terminate();
    }

    return hr;
}

HRESULT Render::InitBufferShader()
{
    ID3DBlob* vsBlob = nullptr;
    HRESULT hr = CompileShader(L"VertexColor.vs", &vsBlob);
    if (FAILED(hr)) {
        OutputDebugString(L"[ERROR] Ошибка компиляции вершинного шейдера\n");
        return hr;
    }

    ID3DBlob* psBlob = nullptr;
    hr = CompileShader(L"PixelColor.ps", &psBlob);
    if (FAILED(hr)) {
        vsBlob->Release();
        OutputDebugString(L"[ERROR] Ошибка компиляции пиксельного шейдера\n");
        return hr;
    }

    // Создание шейдеров
    hr = m_pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_pVertexShader);

    if (FAILED(hr)) {
        vsBlob->Release();
        psBlob->Release();
        DX_CHECK(hr, L"Не удалось создать VertexShader (CreateVertexShader)");
        return hr;
    }
    SetDObjName(m_pVertexShader, "My_Vertex_Shader");

    hr = m_pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pPixelShader);
    if (FAILED(hr)) {
        
        vsBlob->Release();
        psBlob->Release();
        DX_CHECK(hr, L"Не удалось создать PixelShader (CreatePixelShader)");
        return hr;
    }
    SetDObjName(m_pPixelShader, "My_Pixel_Shader");

    // Создание входного лэйаута
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    hr = m_pDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_pInputLayout);
    vsBlob->Release();
    psBlob->Release();

    if (FAILED(hr)) {
        DX_CHECK(hr, L"Ошибка создания входного лэйаута (CreateInputLayout)");
        return hr;
    }
    SetDObjName(m_pInputLayout, "My_Input_Layout");
    return hr;
}

HRESULT Render::InitGeometry(ModelFactory::ModelCode code) {
    HRESULT hr = InitModel(code);
    if (FAILED(hr)) {
        Terminate();
        DX_CHECK(hr, L"Ошибка инициализации модели");
        return hr;
    }

    hr = InitCamera();
    if (FAILED(hr)) {
        Terminate();
        DX_CHECK(hr, L"Ошибка инициализации камеры");
        return hr;
    }

    return hr;
}

HRESULT Render::InitCamera() {
   camera = new Camera(m_pDeviceContext);
   return camera->InitVPBuffer(m_pDevice);
}

HRESULT Render::InitModel(ModelFactory::ModelCode code) {
    m_currentModel = ModelFactory::CreateModel(code, m_pDeviceContext);
    return m_currentModel->InitModel(m_pDevice);
}

void Render::Terminate()
{
    if (m_currentModel) {
        ModelFactory::ReleaseModel(m_currentModel);
        m_currentModel = nullptr;
    }
    if (camera) {
        delete camera;
        camera = nullptr;
    }

    if (m_pDeviceContext) {
        m_pDeviceContext->ClearState();
        m_pDeviceContext->Flush();
    }

    if (m_pSwapChain) {
        m_pSwapChain->SetFullscreenState(FALSE, nullptr);
        m_pSwapChain->Release();
        m_pSwapChain = nullptr;
    }

    if (m_pInputLayout) { m_pInputLayout->Release(); m_pInputLayout = nullptr; }
    if (m_pVertexShader) { m_pVertexShader->Release(); m_pVertexShader = nullptr; }
    if (m_pPixelShader) { m_pPixelShader->Release(); m_pPixelShader = nullptr; }
    if (m_pRenderTargetView) { m_pRenderTargetView->Release(); m_pRenderTargetView = nullptr; }

    if (m_pDeviceContext) {
        m_pDeviceContext->Flush();
        m_pDeviceContext->Release();
        m_pDeviceContext = nullptr;
    }

    if (m_pAnnotation) {
        m_pAnnotation->Release();
        m_pAnnotation = nullptr;
    }

    ID3D11Debug* d3dDebug = nullptr;
#ifdef _DEBUG
    if (m_pDevice) {
        m_pDevice->QueryInterface(__uuidof(ID3D11Debug), (reinterpret_cast<void**>(&d3dDebug)));
    }
#endif

    if (m_pDevice) {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }


#ifdef _DEBUG
    if (d3dDebug)
    {
        OutputDebugString(L"------------------------------------------------\n");
        OutputDebugString(L"REPORT LIVE DEVICE OBJECTS:\n");
        // D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL покажет всё максимально подробно
        d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
        OutputDebugString(L"------------------------------------------------\n");

        // Освобождаем последний интерфейс - теперь Девайс умрет окончательно
        d3dDebug->Release();
        d3dDebug = nullptr;
    }
#endif
}

void Render::SetModel(ModelFactory::ModelCode code) {
    if (m_currentModel) ModelFactory::ReleaseModel(m_currentModel);
    m_currentModel = ModelFactory::CreateModel(code, m_pDeviceContext);
}


HRESULT Render::CompileShader(const std::wstring& path, ID3DBlob** pBlob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* errorBlob = nullptr;
    const D3D_SHADER_MACRO defines[] = { nullptr, nullptr };

    const std::wstring ext = path.substr(path.find_last_of(L".") + 1);
    const std::string target = (ext == L"vs") ? "vs_5_0" : "ps_5_0";

    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        defines,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        target.c_str(),
        flags,
        0,
        pBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::string errorMsg = static_cast<const char*>(errorBlob->GetBufferPointer());
            errorBlob->Release();
            // Ошибка компиляции шейдера (синтаксис)
            MessageBoxA(nullptr, errorMsg.c_str(), "Shader Compilation Error", MB_OK | MB_ICONERROR);
        }
        else {
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
                std::wstring msg = L"Файл шейдера не найден: " + path;
                MessageBox(nullptr, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
            }
            else {
                std::wstring msg = L"Ошибка загрузки шейдера: " + path;
                MessageBox(nullptr, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
            }
        }
    }
    return hr;
}

void Render::UpdateCamera(WPARAM wParam) {
    switch(wParam) {
        case 'W':
            // Upward rotation
            camera->Rotate({ 0.0f, 0.01f });
            break;
        case 'S': // Rotating downwards
            camera->Rotate({ 0.0f, -0.01f });
            break;
        case 'A': // Left rotation
            camera->Rotate({ -0.01f, 0.0f });
            break;
        case 'D': // Right rotation
            camera->Rotate({0.01f, 0.0f});
            break;
        case VK_UP:
            camera->Move({ 0.0f, 1.0f, 0.0f });
            break;
        case VK_DOWN:
            camera->Move({ 0.0f, -1.0f, 0.0f });
            break;
        case VK_LEFT:
            camera->Move({ -1.0f, 0.0f, 0.0f });
            break;
        case VK_RIGHT:
            camera->Move({ 1.0f, 0.0f, 0.0f });
            break;
        case VK_ADD:
        case 0xBB:
            camera->Move({ 0.0f, 0.0f, 1.0f });
            break;

    }

}


void Render::RenderStart()
{
    if (m_pAnnotation) m_pAnnotation->BeginEvent(L"Render Frame");

    // Очистка
    if (m_pAnnotation) m_pAnnotation->BeginEvent(L"Clear");
    m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);
    float BackColor[4] = { 0.48f, 0.57f, 0.48f, 1.0f };
    m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView, BackColor);
    if (m_pAnnotation) m_pAnnotation->EndEvent();
    m_currentModel->Update(0.0);
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;
    HRESULT hr = camera->CameraUpdate((float)width / (float)height);
    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);

    if (m_pAnnotation) m_pAnnotation->BeginEvent(L"Draw Model");
    m_currentModel->Render();
    if (m_pAnnotation) m_pAnnotation->EndEvent();

    m_pSwapChain->Present(1, 0);
    if (m_pAnnotation) m_pAnnotation->EndEvent();
}

void Render::SetMVPBuffer()
{
    m_currentModel->Update(0.0);
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;
    HRESULT hr = camera->CameraUpdate((float)width / (float)height);
    
}

HRESULT Render::ConfigureBackBuffer()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    

    if (FAILED(hr)) {
        DX_CHECK(hr, L"Ошибка получения BackBuffer из SwapChain");
        return hr;
    }
        

    SetDObjName(pBackBuffer, "Main_BackBuffer_Texture");

    hr = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        DX_CHECK(hr, L"Ошибка создания RenderTargetView");
        return hr;
    }
        

    SetDObjName(m_pRenderTargetView, "Main_RTV");

    return hr;
}

void Render::Resize()
{
    if (m_pRenderTargetView)
    {
        m_pRenderTargetView->Release();
        m_pRenderTargetView = nullptr;
    }

    if (m_pSwapChain)
    {
        HRESULT hr;

        RECT rc;
        GetClientRect(m_hWnd, &rc);
        UINT width = rc.right - rc.left;
        UINT height = rc.bottom - rc.top;

        hr = m_pSwapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

        if (FAILED(hr)) {
            OutputDebugString(L"[FATAL] ResizeBuffers failed\n");
            MessageBox(nullptr, L"ResizeBuffers failed", L"DirectX Error", MB_OK);
            PostQuitMessage(0);
            return;
        }

        HRESULT resultBack = ConfigureBackBuffer();

        if (FAILED(resultBack)) {
            OutputDebugString(L"[FATAL] ConfigureBackBuffer failed\n");
            MessageBox(nullptr, L"ConfigureBackBuffer failed", L"DirectX Error", MB_OK);
            PostQuitMessage(0);
            return;
        }

        m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);

        D3D11_VIEWPORT vp;
        vp.Width = (FLOAT)width;
        vp.Height = (FLOAT)height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        m_pDeviceContext->RSSetViewports(1, &vp);
    }
}