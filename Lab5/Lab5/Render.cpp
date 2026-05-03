#include "framework.h"
#include "Render.h"
#include "DDSTextureLoader11.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cwctype>
#include <cstring>

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

struct SceneCB
{
    PointLightGPU Lights[3];
    DirectX::XMFLOAT3 CameraPos; float AO;
    DirectX::XMFLOAT3 Albedo; float Roughness;
    float Metalness;
    int DebugMode;
    float UseTexture;
    float ExtendViews;
    float UseNormalMap;
    float NormalStrength;
    float UseRoughnessMap;
    float RoughnessMapStrength;
    float UseDiffuseIBL;
    float IBLIntensity;
    float UseSpecularIBL;
    float SpecularIBLIntensity;
};

struct LightMarkerColorCB
{
    DirectX::XMFLOAT4 Color;
};

struct SpecularPrefilterCB
{
    DirectX::XMFLOAT4 Params; // x = roughness, y = sourceMipCount
};

static void DebugLogA(const char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

static const char* DebugModeToString(int mode)
{
    switch (mode)
    {
    case 0: return "NDF";
    case 1: return "Geometry";
    case 2: return "Fresnel";
    case 3: return "Final PBR";
    case 4: return "Albedo only";
    case 5: return "Light mask";
    case 6: return "Normal WS";
    case 7: return "TBN sign";
    default: return "Unknown";
    }
}

struct MaterialPreset
{
    const char* Name;
    const wchar_t* AlbedoPath;
    const wchar_t* NormalPath;
    const wchar_t* RoughnessPath;
    const wchar_t* HeightPath;
};

static const MaterialPreset kMaterialPresets[] =
{
    { "Marble",    L"Marble\\marble_cliff_01_diff_1k.dds", L"Marble\\marble_cliff_01_nor_dx_1k.dds", nullptr,                      L"Marble\\marble_cliff_01_disp_1k.dds" },
    { "Roof",      L"Roof\\roof_09_diff_1k.dds",           L"Roof\\roof_09_nor_dx_1k.dds",      L"Roof\\roof_09_rough_1k.dds",  L"Roof\\roof_09_disp_1k.dds" },
    { "Legacy cat",L"cat.dds",                             nullptr,                               nullptr,                        nullptr }
};

static const wchar_t* kResourcePathPrefixes[] =
{
    L"",
    L"..\\Lab5\\",
    L"..\\..\\Lab5\\",
    L"Lab5\\",
    L"..\\Lab3\\",
    L"..\\..\\Lab3\\",
    L"Lab3\\"
};

static std::wstring ToLowerWide(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

static void CollectFileNamesByExtensionFromDirectory(
    const std::wstring& directoryPath,
    const wchar_t* extensionMask,
    std::vector<std::wstring>& outNames)
{
    std::wstring mask = directoryPath;
    if (!mask.empty() && mask.back() != L'\\')
        mask += L'\\';
    mask += extensionMask;

    WIN32_FIND_DATAW findData{};
    HANDLE hFind = FindFirstFileW(mask.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;

        std::wstring fileName = findData.cFileName;
        if (!fileName.empty())
            outNames.push_back(fileName);
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}

static std::wstring FileNameWithoutExtension(const std::wstring& fileName)
{
    const size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return fileName;
    return fileName.substr(0, dot);
}

static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return std::string();

    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &utf8[0], bytes, nullptr, nullptr);
    return utf8;
}

struct CubeCaptureFace
{
    DirectX::XMVECTOR Target;
    DirectX::XMVECTOR Up;
};

static void GetCubeCaptureFaces(CubeCaptureFace outFaces[6])
{
    outFaces[0] = { XMVectorSet(1, 0, 0, 0),  XMVectorSet(0, 1, 0, 0) };
    outFaces[1] = { XMVectorSet(-1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) };
    outFaces[2] = { XMVectorSet(0, 1, 0, 0),  XMVectorSet(0, 0, -1, 0) };
    outFaces[3] = { XMVectorSet(0, -1, 0, 0), XMVectorSet(0, 0, 1, 0) };
    outFaces[4] = { XMVectorSet(0, 0, 1, 0),  XMVectorSet(0, 1, 0, 0) };
    outFaces[5] = { XMVectorSet(0, 0, -1, 0), XMVectorSet(0, 1, 0, 0) };
}

static DirectX::XMMATRIX BuildCubemapCaptureProjection()
{
    return XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, 0.1f, 10.0f);
}

static UINT CountMipLevelsFromSize(UINT size)
{
    UINT mipLevels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++mipLevels;
    }
    return mipLevels;
}

static UINT ResolveCubemapMipCountFromSRV(ID3D11ShaderResourceView* srv)
{
    if (srv == nullptr)
        return 1;

    ID3D11Resource* resource = nullptr;
    srv->GetResource(&resource);
    if (resource == nullptr)
        return 1;

    ID3D11Texture2D* tex2D = nullptr;
    UINT mipCount = 1;
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex2D)) && tex2D != nullptr)
    {
        D3D11_TEXTURE2D_DESC desc{};
        tex2D->GetDesc(&desc);
        if (desc.MipLevels > 0)
            mipCount = desc.MipLevels;
        tex2D->Release();
    }
    resource->Release();
    return mipCount;
}

static bool TryLoadDDSWithCandidates(
    ID3D11Device* device,
    const wchar_t* relativePath,
    bool forceSrgb,
    ID3D11ShaderResourceView** outSrv)
{
    if (outSrv == nullptr || device == nullptr || relativePath == nullptr)
        return false;

    for (const wchar_t* prefix : kResourcePathPrefixes)
    {
        std::wstring fullPath = std::wstring(prefix) + relativePath;
        HRESULT hr = E_FAIL;
        if (forceSrgb)
        {
            hr = DirectX::CreateDDSTextureFromFileEx(
                device,
                fullPath.c_str(),
                0,
                D3D11_USAGE_DEFAULT,
                D3D11_BIND_SHADER_RESOURCE,
                0,
                0,
                DirectX::DDS_LOADER_FORCE_SRGB,
                nullptr,
                outSrv
            );
            if (FAILED(hr))
            {
                hr = DirectX::CreateDDSTextureFromFile(device, fullPath.c_str(), nullptr, outSrv);
            }
        }
        else
        {
            hr = DirectX::CreateDDSTextureFromFile(device, fullPath.c_str(), nullptr, outSrv);
        }

        if (SUCCEEDED(hr) && *outSrv != nullptr)
            return true;
    }

    return false;
}

void Render::SetDObjName(ID3D11DeviceChild* resource, const std::string& name) {
    if (resource) {
        resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
    }
}

void Render::InitLights()
{
    m_pointLights.clear();
    m_pointLights.reserve(3);

    {
        auto L = std::make_unique<PointLight>();
        L->SetName(L"Light 1");
        L->SetPosition(m_lightPositions[0]);
        L->SetColor(m_lightColors[0]);
        L->SetRange(m_lightRanges[0]);
        L->SetIntensity(m_lightBaseIntensity[0] * m_lightBrightness[0]);
        m_pointLights.push_back(std::move(L));
    }
    {
        auto L = std::make_unique<PointLight>();
        L->SetName(L"Light 2");
        L->SetPosition(m_lightPositions[1]);
        L->SetColor(m_lightColors[1]);
        L->SetRange(m_lightRanges[1]);
        L->SetIntensity(m_lightBaseIntensity[1] * m_lightBrightness[1]);
        m_pointLights.push_back(std::move(L));
    }
    {
        auto L = std::make_unique<PointLight>();
        L->SetName(L"Light 3");
        L->SetPosition(m_lightPositions[2]);
        L->SetColor(m_lightColors[2]);
        L->SetRange(m_lightRanges[2]);
        L->SetIntensity(m_lightBaseIntensity[2] * m_lightBrightness[2]);
        m_pointLights.push_back(std::move(L));
    }

    OutputDebugString(L"[INFO] 3 point lights created\n");
}

HRESULT Render::InitScenBuffer()
{
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(SceneCB);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_pDevice->CreateBuffer(&bd, nullptr, &m_pSceneCB);
    DX_CHECK(hr, L"Create SceneCB failed");
    SetDObjName(m_pSceneCB, "Scene_CB");
    return hr;
}

int Render::GetDebugMode() const
{
    return m_debugViewMode;
}

bool Render::IsTonemapEnabled() const
{
    return GetDebugMode() == DebugView_Final;
}

void Render::DumpPipelineState(const char* reason)
{
    const int debugMode = GetDebugMode();
    const int useTexture = (m_enableTextures && m_pAlbedoTextureSRV != nullptr) ? 1 : 0;
    const int useNormalMap = (m_enableNormalMap && m_pNormalTextureSRV != nullptr) ? 1 : 0;
    const int useRoughnessMap = (m_enableRoughnessMap && m_pRoughnessTextureSRV != nullptr) ? 1 : 0;
    const bool forceCopyEffective = m_labUiMode && m_forceCopyPostprocess;
    const int tonemapByMode = IsTonemapEnabled() ? 1 : 0;
    const int tonemapEffective = (tonemapByMode && !forceCopyEffective) ? 1 : 0;
    const int materialIndex = (m_materialPresetIndex >= 0 && m_materialPresetIndex < (int)IM_ARRAYSIZE(kMaterialPresets))
        ? m_materialPresetIndex
        : 0;

    DebugLogA("\n[LAB3][DIAG] --- %s ---\n", (reason != nullptr) ? reason : "no-reason");
    DebugLogA("[LAB3][DIAG] mode=%d (%s), grid=%d, sky=%d, texToggle=%d, useTexture=%d\n",
        debugMode, DebugModeToString(debugMode), m_gridMode ? 1 : 0, m_enableSkybox ? 1 : 0, m_enableTextures ? 1 : 0, useTexture);
    DebugLogA("[LAB3][DIAG] materialPreset=%d (%s), normalToggle=%d, useNormalMap=%d, normalStrength=%.2f, roughToggle=%d, useRough=%d, roughStrength=%.2f\n",
        materialIndex, kMaterialPresets[materialIndex].Name,
        m_enableNormalMap ? 1 : 0, useNormalMap, m_normalStrength,
        m_enableRoughnessMap ? 1 : 0, useRoughnessMap, m_roughnessMapStrength);
    DebugLogA("[LAB3][DIAG] labUiMode=%d\n", m_labUiMode ? 1 : 0);
    DebugLogA("[LAB3][DIAG] extendViews=%d\n", m_extendViews ? 1 : 0);
    DebugLogA("[LAB3][DIAG] postprocess: tonemapByMode=%d forceCopyRaw=%d forceCopyEffective=%d tonemapEffective=%d\n",
        tonemapByMode, m_forceCopyPostprocess ? 1 : 0, forceCopyEffective ? 1 : 0, tonemapEffective);
    DebugLogA("[LAB3][DIAG] resources: albedoSRV=%p normalSRV=%p roughSRV=%p envSRV=%p irradianceSRV=%p prefilterSRV=%p brdfLutSRV=%p sampler=%p depthDSV=%p depthTex=%p\n",
        m_pAlbedoTextureSRV, m_pNormalTextureSRV, m_pRoughnessTextureSRV, m_pEnvironmentSRV, m_pIrradianceSRV, m_pSpecularPrefilterSRV, m_pBRDFLutSRV, m_pSamplerState, m_pSceneDepthView, m_pSceneDepthTexture);
    DebugLogA("[LAB3][DIAG] skyboxSelection=%ls\n", GetSelectedSkyboxRelativePath().c_str());
    DebugLogA("[LAB3][DIAG] material: color=(%.3f %.3f %.3f) rough=%.3f metal=%.3f\n",
        m_materialColor.x, m_materialColor.y, m_materialColor.z, m_materialRoughness, m_materialMetalness);
    DebugLogA("[LAB3][DIAG] diffuseIBL: enabled=%d intensity=%.3f\n",
        (m_enableDiffuseIBL && m_pIrradianceSRV != nullptr) ? 1 : 0,
        m_iblIntensity);
    DebugLogA("[LAB3][DIAG] diffuseIBL: sourceMode=%d irradianceSize=%d\n",
        m_labAmbientSourceMode,
        m_irradianceMapSize);
    DebugLogA("[LAB3][DIAG] specularIBL: enabled=%d intensity=%.3f prefilterSize=%u prefilterMips=%u\n",
        (m_enableSpecularIBL && m_pSpecularPrefilterSRV != nullptr && m_pBRDFLutSRV != nullptr) ? 1 : 0,
        m_specularIBLIntensity,
        m_specularPrefilterSize,
        m_specularPrefilterMipLevels);

    for (int i = 0; i < 3; ++i)
    {
        const float intensity = m_lightBaseIntensity[i] * m_lightBrightness[i];
        DebugLogA("[LAB3][DIAG] light[%d]: pos=(%.3f %.3f %.3f) color=(%.3f %.3f %.3f) range=%.3f intensity=%.3f\n",
            i,
            m_lightPositions[i].x, m_lightPositions[i].y, m_lightPositions[i].z,
            m_lightColors[i].x, m_lightColors[i].y, m_lightColors[i].z,
            m_lightRanges[i], intensity);
    }
}

void Render::RenderScene(float roughness, float metalness, const XMFLOAT3& albedo)
{
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(m_pDeviceContext->Map(m_pSceneCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
    {
        SceneCB* cb = (SceneCB*)ms.pData;

        for (int i = 0; i < 3; ++i)
        {
            m_pointLights[i]->SetPosition(m_lightPositions[i]);
            m_pointLights[i]->SetRange(m_lightRanges[i]);
            m_pointLights[i]->SetColor(m_lightColors[i]);
            m_pointLights[i]->SetIntensity(m_lightBaseIntensity[i] * m_lightBrightness[i]);

            PointLightGPU gpu{};
            m_pointLights[i]->Fill(gpu);
            cb->Lights[i] = gpu;
        }

        cb->CameraPos = camera->position;
        cb->AO = 1.0f;
        cb->Albedo = albedo;
        cb->Roughness = (roughness < 0.04f) ? 0.04f : ((roughness > 1.0f) ? 1.0f : roughness);
        cb->Metalness = (metalness < 0.0f) ? 0.0f : ((metalness > 1.0f) ? 1.0f : metalness);
        cb->DebugMode = GetDebugMode();
        cb->UseTexture = (m_enableTextures && m_pAlbedoTextureSRV != nullptr) ? 1.0f : 0.0f;
        cb->ExtendViews = m_extendViews ? 1.0f : 0.0f;
        cb->UseNormalMap = (m_enableNormalMap && m_pNormalTextureSRV != nullptr) ? 1.0f : 0.0f;
        cb->NormalStrength = (m_normalStrength < 0.0f) ? 0.0f : m_normalStrength;
        cb->UseRoughnessMap = (m_enableRoughnessMap && m_pRoughnessTextureSRV != nullptr) ? 1.0f : 0.0f;
        cb->RoughnessMapStrength = (m_roughnessMapStrength < 0.0f) ? 0.0f : ((m_roughnessMapStrength > 1.0f) ? 1.0f : m_roughnessMapStrength);
        const bool useIrradianceAmbient = (!m_labUiMode || m_labAmbientSourceMode == 0);
        cb->UseDiffuseIBL = (m_enableDiffuseIBL && m_pIrradianceSRV != nullptr && useIrradianceAmbient) ? 1.0f : 0.0f;
        cb->IBLIntensity = (m_iblIntensity < 0.0f) ? 0.0f : ((m_iblIntensity > 5.0f) ? 5.0f : m_iblIntensity);
        cb->UseSpecularIBL =
            (m_enableSpecularIBL && m_pSpecularPrefilterSRV != nullptr && m_pBRDFLutSRV != nullptr && useIrradianceAmbient)
            ? 1.0f : 0.0f;
        cb->SpecularIBLIntensity =
            (m_specularIBLIntensity < 0.0f) ? 0.0f : ((m_specularIBLIntensity > 5.0f) ? 5.0f : m_specularIBLIntensity);

        m_pDeviceContext->Unmap(m_pSceneCB, 0);
    }

    m_pDeviceContext->PSSetConstantBuffers(2, 1, &m_pSceneCB);
}


Render::Render(HWND hWnd) : m_hWnd(hWnd), camera(nullptr), m_currentModel(nullptr),
    m_pDevice(nullptr),
    m_pDeviceContext(nullptr),
    m_pSwapChain(nullptr),
    m_pRenderedSceneTexture(nullptr),
    m_pPixelShader(nullptr),
    m_pVertexShader(nullptr),
    m_pInputLayout(nullptr),
    m_szTitle(nullptr),
    m_PostProcessingPass(nullptr),
    m_szWindowClass(nullptr),
    m_pPostProcessedTexture(nullptr){}

HRESULT Render::Init(WCHAR szTitle[], WCHAR szWindowClass[])
{
    m_szTitle = szTitle;
    m_szWindowClass = szWindowClass;

    HRESULT hr;
    ModelFactory::ModelCode init_code = ModelFactory::ModelCode::sphere;

    IDXGIFactory* pFactory = nullptr;
    hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    DX_CHECK(hr, L"пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ DXGIFactory");
    IDXGIAdapter* pAdapter = nullptr;
    hr = pFactory->EnumAdapters(0, &pAdapter);
    
    if (FAILED(hr)) {
        pFactory->Release();
        DX_CHECK(hr, L"пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ");
        return hr;
    }

    // пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ Direct3D 11
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
        DX_CHECK(hr, L"пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ D3D11 пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ");
        return hr;
    }

    hr = m_pDeviceContext->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void**)&m_pAnnotation);
    if (FAILED(hr)) {
        // пїЅпїЅпїЅ пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ, пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅ пїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅ пїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ
        OutputDebugString(L"[WARNING] ID3DUserDefinedAnnotation not found\n");
    }

    RECT rc = { 0, 0, 800, 600 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = rc.right - rc.left;
    swapChainDesc.BufferDesc.Height = rc.bottom - rc.top;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = m_hWnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.Windowed = TRUE;

    hr = pFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
    pAdapter->Release();
    pFactory->Release();

    if (FAILED(hr)) {
        DX_CHECK(hr, L"пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ (SwapChain)");
        return hr;
    }

    InitLights();
    if (SUCCEEDED(hr)) hr = ConfigureBackBuffer();
    if (SUCCEEDED(hr)) hr = InitGeometry(init_code);
    if (SUCCEEDED(hr)) hr = InitBufferShader();
    if (SUCCEEDED(hr)) hr = InitScenBuffer();
    if (SUCCEEDED(hr)) hr = InitTextureResources();
    if (SUCCEEDED(hr)) hr = InitSkyResources();
    m_PostProcessingPass = new Postprocessing;
    if (SUCCEEDED(hr)) hr = m_PostProcessingPass->Init(m_pDevice, m_pDeviceContext);
    if (SUCCEEDED(hr)) InitImGui();

    if (FAILED(hr))
    {
        Terminate();
    }
    else
    {
        // Dump once after startup so pipeline/resource status is visible immediately.
        m_requestDiagnosticsDump = true;
    }

    return hr;
}

HRESULT Render::InitBufferShader()
{
    ID3DBlob* vsBlob = nullptr;
    HRESULT hr = CompileShader(L"VertexColor.vs", &vsBlob);
    if (FAILED(hr)) {
        OutputDebugString(L"[ERROR] пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ\n");
        return hr;
    }

    ID3DBlob* psBlob = nullptr;
    hr = CompileShader(L"PixelColor.ps", &psBlob);
    if (FAILED(hr)) {
        vsBlob->Release();
        OutputDebugString(L"[ERROR] пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ\n");
        return hr;
    }

    // пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ
    hr = m_pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_pVertexShader);

    if (FAILED(hr)) {
        vsBlob->Release();
        psBlob->Release();
        DX_CHECK(hr, L"пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ VertexShader (CreateVertexShader)");
        return hr;
    }
    SetDObjName(m_pVertexShader, "My_Vertex_Shader");

    hr = m_pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pPixelShader);
    if (FAILED(hr)) {
        
        vsBlob->Release();
        psBlob->Release();
        DX_CHECK(hr, L"пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ PixelShader (CreatePixelShader)");
        return hr;
    }
    SetDObjName(m_pPixelShader, "My_Pixel_Shader");

    ID3DBlob* lightMarkerPsBlob = nullptr;
    hr = CompileShader(L"LightMarker.ps", &lightMarkerPsBlob);
    if (FAILED(hr)) {
        vsBlob->Release();
        psBlob->Release();
        OutputDebugString(L"[ERROR] Failed to compile light marker pixel shader\n");
        return hr;
    }
    hr = m_pDevice->CreatePixelShader(
        lightMarkerPsBlob->GetBufferPointer(),
        lightMarkerPsBlob->GetBufferSize(),
        nullptr,
        &m_pLightMarkerPixelShader
    );
    lightMarkerPsBlob->Release();
    if (FAILED(hr)) {
        vsBlob->Release();
        psBlob->Release();
        return hr;
    }

    D3D11_BUFFER_DESC markerCbDesc{};
    markerCbDesc.Usage = D3D11_USAGE_DEFAULT;
    markerCbDesc.ByteWidth = sizeof(LightMarkerColorCB);
    markerCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_pDevice->CreateBuffer(&markerCbDesc, nullptr, &m_pLightMarkerColorCB);
    if (FAILED(hr)) {
        vsBlob->Release();
        psBlob->Release();
        return hr;
    }
    SetDObjName(m_pLightMarkerColorCB, "LightMarker_Color_CB");

    // пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelManagerAbstract::Vertex, xyz),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelManagerAbstract::Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(ModelManagerAbstract::Vertex, uv),     D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelManagerAbstract::Vertex, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelManagerAbstract::Vertex, bitangent), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = m_pDevice->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_pInputLayout
    );
    vsBlob->Release();
    psBlob->Release();

    if (FAILED(hr)) {
        DX_CHECK(hr, L"пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ (CreateInputLayout)");
        return hr;
    }
    SetDObjName(m_pInputLayout, "My_Input_Layout");
    return hr;
}

HRESULT Render::InitGeometry(ModelFactory::ModelCode code) {
    HRESULT hr = InitModel(code);
    if (FAILED(hr)) {
        Terminate();
        DX_CHECK(hr, L"пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ");
        return hr;
    }

    hr = InitCamera();
    if (FAILED(hr)) {
        Terminate();
        DX_CHECK(hr, L"пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ");
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
    if (!m_currentModel)
        return E_FAIL;
    return m_currentModel->InitModel(m_pDevice);
}

void Render::Terminate()
{
    ShutdownImGui();

    ReleaseSkyResources();
    ReleaseTextureResources();
    
    if (m_PostProcessingPass) {
        delete m_PostProcessingPass;
        m_PostProcessingPass = nullptr;
    }

    if (m_pAnnotation) {
        m_pAnnotation->Release();
        m_pAnnotation = nullptr;
    }

    if (m_currentModel) {
        ModelFactory::ReleaseModel(m_currentModel);
        m_currentModel = nullptr;
    }

    if (camera) {
        delete camera;
        camera = nullptr;
    }

    if (m_pInputLayout) { m_pInputLayout->Release(); m_pInputLayout = nullptr; }
    if (m_pVertexShader) { m_pVertexShader->Release(); m_pVertexShader = nullptr; }
    if (m_pLightMarkerPixelShader) { m_pLightMarkerPixelShader->Release(); m_pLightMarkerPixelShader = nullptr; }
    if (m_pPixelShader) { m_pPixelShader->Release(); m_pPixelShader = nullptr; }
    if (m_pLightMarkerColorCB) { m_pLightMarkerColorCB->Release(); m_pLightMarkerColorCB = nullptr; }

    if (m_pSceneCB) {
        m_pSceneCB->Release();
        m_pSceneCB = nullptr;
    }
    if (m_pSceneDepthView) {
        m_pSceneDepthView->Release();
        m_pSceneDepthView = nullptr;
    }
    if (m_pSceneDepthTexture) {
        m_pSceneDepthTexture->Release();
        m_pSceneDepthTexture = nullptr;
    }

    if (m_pPostProcessedTexture) { delete m_pPostProcessedTexture; m_pPostProcessedTexture = nullptr; }
    if (m_pRenderedSceneTexture) { delete m_pRenderedSceneTexture; m_pRenderedSceneTexture = nullptr; }

    if (m_pSwapChain) {
        m_pSwapChain->SetFullscreenState(FALSE, nullptr);
        m_pSwapChain->Release();
        m_pSwapChain = nullptr;
    }

    if (m_pDeviceContext) {
        m_pDeviceContext->ClearState();
        m_pDeviceContext->Flush();
        m_pDeviceContext->Release();
        m_pDeviceContext = nullptr;
    }
    
    if (m_pDevice) {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
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
            // пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ (пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ)
            MessageBoxA(nullptr, errorMsg.c_str(), "Shader Compilation Error", MB_OK | MB_ICONERROR);
        }
        else {
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
                std::wstring msg = L"пїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ: " + path;
                MessageBox(nullptr, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
            }
            else {
                std::wstring msg = L"пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ: " + path;
                MessageBox(nullptr, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
            }
        }
    }
    return hr;
}

HRESULT Render::InitTextureResources()
{
    if (!m_pSamplerState)
    {
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = m_pDevice->CreateSamplerState(&samplerDesc, &m_pSamplerState);
        if (FAILED(hr))
            return hr;
    }

    if (m_pAlbedoTextureSRV)
    {
        m_pAlbedoTextureSRV->Release();
        m_pAlbedoTextureSRV = nullptr;
    }
    if (m_pNormalTextureSRV)
    {
        m_pNormalTextureSRV->Release();
        m_pNormalTextureSRV = nullptr;
    }
    if (m_pRoughnessTextureSRV)
    {
        m_pRoughnessTextureSRV->Release();
        m_pRoughnessTextureSRV = nullptr;
    }

    const int materialIndex = (m_materialPresetIndex >= 0 && m_materialPresetIndex < (int)IM_ARRAYSIZE(kMaterialPresets))
        ? m_materialPresetIndex
        : 0;
    const MaterialPreset& preset = kMaterialPresets[materialIndex];

    const bool albedoLoaded = TryLoadDDSWithCandidates(
        m_pDevice,
        preset.AlbedoPath,
        true,
        &m_pAlbedoTextureSRV
    );
    if (!albedoLoaded)
    {
        OutputDebugString(L"[WARNING] Albedo texture not found or failed to load, texture mode disabled\n");
        DebugLogA("[LAB3][DIAG] albedo load failed for material '%s'\n", preset.Name);
        m_pAlbedoTextureSRV = nullptr;
    }
    else
    {
        DebugLogA("[LAB3][DIAG] albedo loaded for material '%s', SRV=%p\n", preset.Name, m_pAlbedoTextureSRV);
    }

    if (preset.NormalPath != nullptr)
    {
        const bool normalLoaded = TryLoadDDSWithCandidates(
            m_pDevice,
            preset.NormalPath,
            false,
            &m_pNormalTextureSRV
        );

        if (!normalLoaded)
        {
            DebugLogA("[LAB3][DIAG] normal map load failed for material '%s'\n", preset.Name);
            m_pNormalTextureSRV = nullptr;
        }
        else
        {
            DebugLogA("[LAB3][DIAG] normal map loaded for material '%s', SRV=%p\n", preset.Name, m_pNormalTextureSRV);
        }
    }
    else
    {
        m_pNormalTextureSRV = nullptr;
    }

    const wchar_t* roughnessPath = preset.RoughnessPath;
    const bool usingHeightAsRoughness = (roughnessPath == nullptr && preset.HeightPath != nullptr);
    if (roughnessPath == nullptr)
        roughnessPath = preset.HeightPath;

    if (roughnessPath != nullptr)
    {
        const bool roughLoaded = TryLoadDDSWithCandidates(
            m_pDevice,
            roughnessPath,
            false,
            &m_pRoughnessTextureSRV
        );

        if (!roughLoaded)
        {
            DebugLogA("[LAB3][DIAG] roughness-like map load failed for material '%s'\n", preset.Name);
            m_pRoughnessTextureSRV = nullptr;
        }
        else
        {
            DebugLogA(
                "[LAB3][DIAG] %s map loaded for material '%s', SRV=%p\n",
                usingHeightAsRoughness ? "height->roughness" : "roughness",
                preset.Name,
                m_pRoughnessTextureSRV
            );
        }
    }
    else
    {
        m_pRoughnessTextureSRV = nullptr;
    }

    return S_OK;
}

void Render::RefreshSkyboxList()
{
    std::wstring previousKey;
    if (!m_environmentEntries.empty() && m_skyboxFileIndex >= 0 && m_skyboxFileIndex < static_cast<int>(m_environmentEntries.size()))
        previousKey = m_environmentEntries[m_skyboxFileIndex].KeyLower;

    std::vector<std::wstring> discoveredSkyboxDDS;
    std::vector<std::wstring> discoveredHDRI;
    for (const wchar_t* prefix : kResourcePathPrefixes)
    {
        CollectFileNamesByExtensionFromDirectory(std::wstring(prefix) + L"SkyBox", L"*.dds", discoveredSkyboxDDS);
        CollectFileNamesByExtensionFromDirectory(std::wstring(prefix) + L"HDRI", L"*.hdr", discoveredHDRI);
    }

    auto uniqueSortedLower = [](const std::vector<std::wstring>& names) -> std::vector<std::wstring>
    {
        std::set<std::wstring> seen;
        std::vector<std::wstring> out;
        out.reserve(names.size());
        for (const std::wstring& name : names)
        {
            const std::wstring key = ToLowerWide(name);
            if (seen.insert(key).second)
                out.push_back(name);
        }
        std::sort(out.begin(), out.end(),
            [](const std::wstring& a, const std::wstring& b)
            {
                return ToLowerWide(a) < ToLowerWide(b);
            });
        return out;
    };

    const std::vector<std::wstring> skyboxDDS = uniqueSortedLower(discoveredSkyboxDDS);
    const std::vector<std::wstring> hdrFiles = uniqueSortedLower(discoveredHDRI);

    std::vector<EnvironmentEntry> entries;
    entries.reserve(skyboxDDS.size() + hdrFiles.size() + 1);

    for (const std::wstring& ddsName : skyboxDDS)
    {
        EnvironmentEntry entry{};
        entry.SourceKind = EnvironmentSourceKind::SkyboxDDS;
        entry.SourceFileName = ddsName;
        entry.SkyboxRelativePath = std::wstring(L"SkyBox\\") + ddsName;
        entry.KeyLower = std::wstring(L"dds:") + ToLowerWide(ddsName);
        entry.HasConvertedDDS = true;
        entry.RuntimeConverted = true;
        entry.DisplayName = std::wstring(L"[v] SkyBox: ") + ddsName;
        entries.push_back(entry);
    }

    for (const std::wstring& hdrName : hdrFiles)
    {
        EnvironmentEntry entry{};
        entry.SourceKind = EnvironmentSourceKind::Hdri;
        entry.SourceFileName = hdrName;
        entry.HdriRelativePath = std::wstring(L"HDRI\\") + hdrName;
        entry.KeyLower = std::wstring(L"hdr:") + ToLowerWide(hdrName);

        const std::wstring stem = FileNameWithoutExtension(hdrName);
        const std::wstring candidateBox = BuildConvertedSkyboxRelativePath(hdrName);
        const std::wstring candidateDirect = std::wstring(L"SkyBox\\") + stem + L".dds";

        if (DoesRelativeFileExist(candidateBox))
        {
            entry.HasConvertedDDS = true;
            entry.SkyboxRelativePath = candidateBox;
        }
        else if (DoesRelativeFileExist(candidateDirect))
        {
            entry.HasConvertedDDS = true;
            entry.SkyboxRelativePath = candidateDirect;
        }
        else
        {
            entry.HasConvertedDDS = false;
            entry.SkyboxRelativePath = candidateBox;
        }

        ID3D11ShaderResourceView* cachedSRV = nullptr;
        const bool hasRuntimeCache = TryGetCachedHdriCubemap(entry.KeyLower, &cachedSRV);
        if (cachedSRV != nullptr)
            cachedSRV->Release();

        entry.RuntimeConverted = entry.HasConvertedDDS || hasRuntimeCache;
        entry.DisplayName = std::wstring(entry.RuntimeConverted ? L"[v] HDRI: " : L"[x] HDRI: ") + hdrName;
        entries.push_back(entry);
    }

    if (entries.empty())
    {
        EnvironmentEntry fallback{};
        fallback.SourceKind = EnvironmentSourceKind::SkyboxDDS;
        fallback.SourceFileName = L"sunset_1024_box.dds";
        fallback.SkyboxRelativePath = L"SkyBox\\sunset_1024_box.dds";
        fallback.KeyLower = L"dds:sunset_1024_box.dds";
        fallback.HasConvertedDDS = DoesRelativeFileExist(fallback.SkyboxRelativePath);
        fallback.RuntimeConverted = fallback.HasConvertedDDS;
        fallback.DisplayName = std::wstring(fallback.HasConvertedDDS ? L"[v] SkyBox: " : L"[x] SkyBox: ") + fallback.SourceFileName;
        entries.push_back(fallback);
    }

    m_environmentEntries = std::move(entries);
    m_skyboxFileIndex = 0;
    if (!previousKey.empty())
    {
        for (size_t i = 0; i < m_environmentEntries.size(); ++i)
        {
            if (m_environmentEntries[i].KeyLower == previousKey)
            {
                m_skyboxFileIndex = static_cast<int>(i);
                break;
            }
        }
    }
}

std::wstring Render::GetSelectedSkyboxRelativePath() const
{
    if (m_environmentEntries.empty())
        return L"SkyBox\\sunset_1024_box.dds";

    int idx = m_skyboxFileIndex;
    if (idx < 0 || idx >= static_cast<int>(m_environmentEntries.size()))
        idx = 0;

    const EnvironmentEntry& entry = m_environmentEntries[idx];
    if (entry.SourceKind == EnvironmentSourceKind::SkyboxDDS || entry.HasConvertedDDS)
        return entry.SkyboxRelativePath;
    return entry.HdriRelativePath;
}

std::wstring Render::GetSelectedSkyboxDisplayName() const
{
    if (m_environmentEntries.empty())
        return L"<none>";

    int idx = m_skyboxFileIndex;
    if (idx < 0 || idx >= static_cast<int>(m_environmentEntries.size()))
        idx = 0;

    return m_environmentEntries[idx].DisplayName;
}

HRESULT Render::InitSkyResources()
{
    if (m_pSkyVertexShader) { m_pSkyVertexShader->Release(); m_pSkyVertexShader = nullptr; }
    if (m_pSkyPixelShader) { m_pSkyPixelShader->Release(); m_pSkyPixelShader = nullptr; }
    if (m_pEnvironmentSRV) { m_pEnvironmentSRV->Release(); m_pEnvironmentSRV = nullptr; }
    if (m_pIrradianceSRV) { m_pIrradianceSRV->Release(); m_pIrradianceSRV = nullptr; }
    if (m_pSpecularPrefilterSRV) { m_pSpecularPrefilterSRV->Release(); m_pSpecularPrefilterSRV = nullptr; }
    if (m_pSkyRasterState) { m_pSkyRasterState->Release(); m_pSkyRasterState = nullptr; }
    if (m_pSkyDepthState) { m_pSkyDepthState->Release(); m_pSkyDepthState = nullptr; }

    if (m_environmentEntries.empty())
        RefreshSkyboxList();

    HRESULT hr = S_OK;
    ID3DBlob* vsBlob = nullptr;
    hr = CompileShader(L"SkyVertex.vs", &vsBlob);
    if (SUCCEEDED(hr))
    {
        hr = m_pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_pSkyVertexShader);
    }
    if (vsBlob) vsBlob->Release();
    if (FAILED(hr))
        return hr;

    ID3DBlob* psBlob = nullptr;
    hr = CompileShader(L"SkyPixel.ps", &psBlob);
    if (SUCCEEDED(hr))
    {
        hr = m_pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pSkyPixelShader);
    }
    if (psBlob) psBlob->Release();
    if (FAILED(hr))
        return hr;

    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_FRONT;
    rsDesc.DepthClipEnable = TRUE;
    hr = m_pDevice->CreateRasterizerState(&rsDesc, &m_pSkyRasterState);
    if (FAILED(hr))
        return hr;

    D3D11_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = m_pDevice->CreateDepthStencilState(&dsDesc, &m_pSkyDepthState);
    if (FAILED(hr))
        return hr;

    if (m_environmentEntries.empty())
        return E_FAIL;

    int idx = m_skyboxFileIndex;
    if (idx < 0 || idx >= static_cast<int>(m_environmentEntries.size()))
        idx = 0;

    EnvironmentEntry& selected = m_environmentEntries[idx];
    bool skyLoaded = false;
    HRESULT loadHr = E_FAIL;

    if (selected.SourceKind == EnvironmentSourceKind::SkyboxDDS || selected.HasConvertedDDS)
    {
        const std::wstring skyPath = selected.SkyboxRelativePath.empty()
            ? GetSelectedSkyboxRelativePath()
            : selected.SkyboxRelativePath;
        skyLoaded = TryLoadDDSWithCandidates(
            m_pDevice,
            skyPath.c_str(),
            true,
            &m_pEnvironmentSRV
        );
        loadHr = skyLoaded ? S_OK : E_FAIL;
    }
    else
    {
        if (TryGetCachedHdriCubemap(selected.KeyLower, &m_pEnvironmentSRV))
        {
            skyLoaded = (m_pEnvironmentSRV != nullptr);
            loadHr = skyLoaded ? S_OK : E_FAIL;
        }
        else
        {
            const std::wstring hdriFullPath = ResolveFirstExistingPath(selected.HdriRelativePath);
            if (!hdriFullPath.empty())
            {
                ID3D11ShaderResourceView* hdr2DSRV = nullptr;
                loadHr = LoadHDRTexture2D(hdriFullPath, &hdr2DSRV);
                if (SUCCEEDED(loadHr) && hdr2DSRV != nullptr)
                {
                    ID3D11ShaderResourceView* convertedCubeSRV = nullptr;
                    loadHr = ConvertHDRIToCubemap(hdr2DSRV, 1024, &convertedCubeSRV);
                    hdr2DSRV->Release();

                    if (SUCCEEDED(loadHr) && convertedCubeSRV != nullptr)
                    {
                        PutCachedHdriCubemap(selected.KeyLower, convertedCubeSRV);
                        m_pEnvironmentSRV = convertedCubeSRV;
                        selected.RuntimeConverted = true;
                        selected.DisplayName = std::wstring(L"[v] HDRI: ") + selected.SourceFileName;
                        skyLoaded = true;
                    }
                }
            }
        }
    }

    if (!skyLoaded)
    {
        OutputDebugString(L"[WARNING] Skybox/HDRI environment not found or failed to load, skybox disabled\n");
        DebugLogA("[LAB3][DIAG] skybox/hdri load failed for '%ls' (hr=0x%08X)\n", GetSelectedSkyboxRelativePath().c_str(), static_cast<unsigned int>(loadHr));
        m_enableSkybox = false;
        m_pEnvironmentSRV = nullptr;
        hr = FAILED(loadHr) ? loadHr : E_FAIL;
    }
    else
    {
        DebugLogA("[LAB3][DIAG] environment loaded: '%ls', SRV=%p\n", GetSelectedSkyboxRelativePath().c_str(), m_pEnvironmentSRV);
        const HRESULT irradianceHr = RebuildIrradianceFromEnvironment();
        if (FAILED(irradianceHr))
        {
            DebugLogA("[LAB3][DIAG] irradiance convolution failed for '%ls' (hr=0x%08X)\n",
                GetSelectedSkyboxRelativePath().c_str(),
                static_cast<unsigned int>(irradianceHr));
        }

        const HRESULT specularHr = RebuildSpecularIBLFromEnvironment();
        if (FAILED(specularHr))
        {
            DebugLogA("[LAB3][DIAG] specular IBL rebuild failed for '%ls' (hr=0x%08X)\n",
                GetSelectedSkyboxRelativePath().c_str(),
                static_cast<unsigned int>(specularHr));
        }
    }

    return hr;
}

bool Render::TryGetCachedHdriCubemap(const std::wstring& keyLower, ID3D11ShaderResourceView** outSRV) const
{
    if (outSRV == nullptr)
        return false;
    *outSRV = nullptr;

    for (const HdriCubemapCacheEntry& item : m_hdriCubemapCache)
    {
        if (item.KeyLower == keyLower && item.CubemapSRV != nullptr)
        {
            item.CubemapSRV->AddRef();
            *outSRV = item.CubemapSRV;
            return true;
        }
    }
    return false;
}

void Render::PutCachedHdriCubemap(const std::wstring& keyLower, ID3D11ShaderResourceView* cubeSRV)
{
    if (cubeSRV == nullptr)
        return;

    for (HdriCubemapCacheEntry& item : m_hdriCubemapCache)
    {
        if (item.KeyLower == keyLower)
        {
            if (item.CubemapSRV != nullptr)
                item.CubemapSRV->Release();
            item.CubemapSRV = cubeSRV;
            item.CubemapSRV->AddRef();
            return;
        }
    }

    HdriCubemapCacheEntry newEntry{};
    newEntry.KeyLower = keyLower;
    newEntry.CubemapSRV = cubeSRV;
    newEntry.CubemapSRV->AddRef();
    m_hdriCubemapCache.push_back(newEntry);
}

std::wstring Render::ResolveFirstExistingPath(const std::wstring& relativePath) const
{
    for (const wchar_t* prefix : kResourcePathPrefixes)
    {
        std::wstring fullPath = std::wstring(prefix) + relativePath;
        const DWORD attr = GetFileAttributesW(fullPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return fullPath;
    }
    return std::wstring();
}

bool Render::DoesRelativeFileExist(const std::wstring& relativePath) const
{
    return !ResolveFirstExistingPath(relativePath).empty();
}

std::wstring Render::BuildConvertedSkyboxRelativePath(const std::wstring& hdriFileName) const
{
    const std::wstring stem = FileNameWithoutExtension(hdriFileName);
    return std::wstring(L"SkyBox\\") + stem + L"_box.dds";
}

HRESULT Render::LoadHDRTexture2D(const std::wstring& fullPath, ID3D11ShaderResourceView** outSRV)
{
    if (outSRV == nullptr)
        return E_INVALIDARG;
    *outSRV = nullptr;

    std::string narrowPath = WideToUtf8(fullPath);
    if (narrowPath.empty())
        return E_FAIL;

    int width = 0;
    int height = 0;
    int channels = 0;
    float* data = stbi_loadf(narrowPath.c_str(), &width, &height, &channels, 3);
    if (data == nullptr || width <= 0 || height <= 0)
        return E_FAIL;

    std::vector<float> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    for (int i = 0; i < width * height; ++i)
    {
        rgba[static_cast<size_t>(i) * 4u + 0u] = data[i * 3 + 0];
        rgba[static_cast<size_t>(i) * 4u + 1u] = data[i * 3 + 1];
        rgba[static_cast<size_t>(i) * 4u + 2u] = data[i * 3 + 2];
        rgba[static_cast<size_t>(i) * 4u + 3u] = 1.0f;
    }
    stbi_image_free(data);

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = static_cast<UINT>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = static_cast<UINT>(width * 4 * sizeof(float));

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = m_pDevice->CreateTexture2D(&texDesc, &initData, &texture);
    if (FAILED(hr))
        return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_pDevice->CreateShaderResourceView(texture, &srvDesc, outSRV);
    texture->Release();
    return hr;
}

HRESULT Render::ConvertHDRIToCubemap(
    ID3D11ShaderResourceView* equirectSRV,
    UINT cubeSize,
    ID3D11ShaderResourceView** outCubeSRV)
{
    if (equirectSRV == nullptr || outCubeSRV == nullptr || camera == nullptr)
        return E_INVALIDARG;

    *outCubeSRV = nullptr;

    ModelManagerAbstract* captureModel = ModelFactory::CreateModel(ModelFactory::ModelCode::cube, m_pDeviceContext);
    if (captureModel == nullptr)
        return E_FAIL;

    struct ScopedModelRelease
    {
        ModelManagerAbstract*& ModelRef;
        ~ScopedModelRelease()
        {
            if (ModelRef != nullptr)
            {
                ModelFactory::ReleaseModel(ModelRef);
                ModelRef = nullptr;
            }
        }
    } captureModelGuard{ captureModel };

    HRESULT hr = captureModel->InitModel(m_pDevice);
    if (FAILED(hr))
        return hr;

    ID3DBlob* hdrPsBlob = nullptr;
    hr = CompileShader(L"HdrToCubemap.ps", &hdrPsBlob);
    if (FAILED(hr) || hdrPsBlob == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    ID3D11PixelShader* hdrToCubePS = nullptr;
    hr = m_pDevice->CreatePixelShader(
        hdrPsBlob->GetBufferPointer(),
        hdrPsBlob->GetBufferSize(),
        nullptr,
        &hdrToCubePS
    );
    hdrPsBlob->Release();
    if (FAILED(hr) || hdrToCubePS == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    D3D11_TEXTURE2D_DESC cubeDesc{};
    cubeDesc.Width = cubeSize;
    cubeDesc.Height = cubeSize;
    cubeDesc.MipLevels = 0;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.Usage = D3D11_USAGE_DEFAULT;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2D* cubeTexture = nullptr;
    hr = m_pDevice->CreateTexture2D(&cubeDesc, nullptr, &cubeTexture);
    if (FAILED(hr) || cubeTexture == nullptr)
    {
        hdrToCubePS->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC cubeSrvDesc{};
    cubeSrvDesc.Format = cubeDesc.Format;
    cubeSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cubeSrvDesc.TextureCube.MostDetailedMip = 0;
    cubeSrvDesc.TextureCube.MipLevels = static_cast<UINT>(-1);

    ID3D11ShaderResourceView* cubeSRV = nullptr;
    hr = m_pDevice->CreateShaderResourceView(cubeTexture, &cubeSrvDesc, &cubeSRV);
    if (FAILED(hr) || cubeSRV == nullptr)
    {
        cubeTexture->Release();
        hdrToCubePS->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ID3D11RenderTargetView* faceRTV[6] = {};
    for (UINT face = 0; face < 6; ++face)
    {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = cubeDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.FirstArraySlice = face;
        rtvDesc.Texture2DArray.ArraySize = 1;

        hr = m_pDevice->CreateRenderTargetView(cubeTexture, &rtvDesc, &faceRTV[face]);
        if (FAILED(hr) || faceRTV[face] == nullptr)
        {
            for (UINT i = 0; i < 6; ++i)
            {
                if (faceRTV[i] != nullptr)
                    faceRTV[i]->Release();
            }
            cubeSRV->Release();
            cubeTexture->Release();
            hdrToCubePS->Release();
            return FAILED(hr) ? hr : E_FAIL;
        }
    }

    UINT oldViewportCount = 1;
    D3D11_VIEWPORT oldViewport{};
    m_pDeviceContext->RSGetViewports(&oldViewportCount, &oldViewport);

    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    m_pDeviceContext->OMGetRenderTargets(1, &oldRTV, &oldDSV);

    ID3D11RasterizerState* oldRasterState = nullptr;
    m_pDeviceContext->RSGetState(&oldRasterState);

    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_FRONT;
    rsDesc.DepthClipEnable = TRUE;

    ID3D11RasterizerState* cubeRasterState = nullptr;
    hr = m_pDevice->CreateRasterizerState(&rsDesc, &cubeRasterState);
    if (FAILED(hr))
    {
        for (UINT i = 0; i < 6; ++i)
            faceRTV[i]->Release();
        cubeSRV->Release();
        cubeTexture->Release();
        hdrToCubePS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        if (oldRasterState) oldRasterState->Release();
        return hr;
    }

    D3D11_VIEWPORT cubeViewport{};
    cubeViewport.TopLeftX = 0.0f;
    cubeViewport.TopLeftY = 0.0f;
    cubeViewport.Width = static_cast<float>(cubeSize);
    cubeViewport.Height = static_cast<float>(cubeSize);
    cubeViewport.MinDepth = 0.0f;
    cubeViewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &cubeViewport);
    m_pDeviceContext->RSSetState(cubeRasterState);

    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->VSSetShader(m_pSkyVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(hdrToCubePS, nullptr, 0);
    m_pDeviceContext->PSSetShaderResources(0, 1, &equirectSRV);
    m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerState);

    XMVECTOR eye = XMVectorZero();
    CubeCaptureFace captureFaces[6]{};
    GetCubeCaptureFaces(captureFaces);

    captureModel->SetModelMatrix(XMMatrixIdentity());

    ID3D11Buffer* cameraVPBuffer = camera->GetVPBuffer();
    if (cameraVPBuffer == nullptr)
    {
        for (UINT i = 0; i < 6; ++i)
            faceRTV[i]->Release();
        cubeSRV->Release();
        cubeTexture->Release();
        hdrToCubePS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        if (oldRasterState) oldRasterState->Release();
        if (cubeRasterState) cubeRasterState->Release();
        return E_FAIL;
    }

    const XMMATRIX proj = BuildCubemapCaptureProjection();
    for (UINT face = 0; face < 6; ++face)
    {
        const float clear[4] = { 0, 0, 0, 1 };
        m_pDeviceContext->OMSetRenderTargets(1, &faceRTV[face], nullptr);
        m_pDeviceContext->ClearRenderTargetView(faceRTV[face], clear);

        const XMMATRIX view = XMMatrixLookToLH(eye, captureFaces[face].Target, captureFaces[face].Up);
        const XMMATRIX vp = XMMatrixTranspose(view * proj);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = m_pDeviceContext->Map(cameraVPBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
            break;
        std::memcpy(mapped.pData, &vp, sizeof(XMMATRIX));
        m_pDeviceContext->Unmap(cameraVPBuffer, 0);
        m_pDeviceContext->VSSetConstantBuffers(1, 1, &cameraVPBuffer);

        captureModel->Render();
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

    if (SUCCEEDED(hr))
        m_pDeviceContext->GenerateMips(cubeSRV);

    m_pDeviceContext->OMSetRenderTargets(1, &oldRTV, oldDSV);
    m_pDeviceContext->RSSetViewports(1, &oldViewport);
    m_pDeviceContext->RSSetState(oldRasterState);

    if (oldRTV) oldRTV->Release();
    if (oldDSV) oldDSV->Release();
    if (oldRasterState) oldRasterState->Release();
    if (cubeRasterState) cubeRasterState->Release();

    for (UINT i = 0; i < 6; ++i)
        faceRTV[i]->Release();

    cubeTexture->Release();
    hdrToCubePS->Release();

    if (FAILED(hr))
    {
        cubeSRV->Release();
        return hr;
    }

    *outCubeSRV = cubeSRV;
    return S_OK;
}

HRESULT Render::ConvolveCubemapToIrradiance(
    ID3D11ShaderResourceView* environmentCubeSRV,
    UINT irradianceSize,
    ID3D11ShaderResourceView** outIrradianceSRV)
{
    if (environmentCubeSRV == nullptr || outIrradianceSRV == nullptr || camera == nullptr)
        return E_INVALIDARG;

    *outIrradianceSRV = nullptr;

    ModelManagerAbstract* captureModel = ModelFactory::CreateModel(ModelFactory::ModelCode::cube, m_pDeviceContext);
    if (captureModel == nullptr)
        return E_FAIL;

    struct ScopedModelRelease
    {
        ModelManagerAbstract*& ModelRef;
        ~ScopedModelRelease()
        {
            if (ModelRef != nullptr)
            {
                ModelFactory::ReleaseModel(ModelRef);
                ModelRef = nullptr;
            }
        }
    } captureModelGuard{ captureModel };

    HRESULT hr = captureModel->InitModel(m_pDevice);
    if (FAILED(hr))
        return hr;

    ID3DBlob* convolutionPsBlob = nullptr;
    hr = CompileShader(L"IrradianceConvolution.ps", &convolutionPsBlob);
    if (FAILED(hr) || convolutionPsBlob == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    ID3D11PixelShader* convolutionPS = nullptr;
    hr = m_pDevice->CreatePixelShader(
        convolutionPsBlob->GetBufferPointer(),
        convolutionPsBlob->GetBufferSize(),
        nullptr,
        &convolutionPS
    );
    convolutionPsBlob->Release();
    if (FAILED(hr) || convolutionPS == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    D3D11_TEXTURE2D_DESC cubeDesc{};
    cubeDesc.Width = irradianceSize;
    cubeDesc.Height = irradianceSize;
    cubeDesc.MipLevels = 1;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.Usage = D3D11_USAGE_DEFAULT;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    ID3D11Texture2D* irradianceTexture = nullptr;
    hr = m_pDevice->CreateTexture2D(&cubeDesc, nullptr, &irradianceTexture);
    if (FAILED(hr) || irradianceTexture == nullptr)
    {
        convolutionPS->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = cubeDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;

    ID3D11ShaderResourceView* irradianceSRV = nullptr;
    hr = m_pDevice->CreateShaderResourceView(irradianceTexture, &srvDesc, &irradianceSRV);
    if (FAILED(hr) || irradianceSRV == nullptr)
    {
        irradianceTexture->Release();
        convolutionPS->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ID3D11RenderTargetView* faceRTV[6] = {};
    for (UINT face = 0; face < 6; ++face)
    {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = cubeDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.FirstArraySlice = face;
        rtvDesc.Texture2DArray.ArraySize = 1;
        hr = m_pDevice->CreateRenderTargetView(irradianceTexture, &rtvDesc, &faceRTV[face]);
        if (FAILED(hr) || faceRTV[face] == nullptr)
        {
            for (UINT i = 0; i < 6; ++i)
            {
                if (faceRTV[i] != nullptr)
                    faceRTV[i]->Release();
            }
            irradianceSRV->Release();
            irradianceTexture->Release();
            convolutionPS->Release();
            return FAILED(hr) ? hr : E_FAIL;
        }
    }

    UINT oldViewportCount = 1;
    D3D11_VIEWPORT oldViewport{};
    m_pDeviceContext->RSGetViewports(&oldViewportCount, &oldViewport);

    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    m_pDeviceContext->OMGetRenderTargets(1, &oldRTV, &oldDSV);

    ID3D11RasterizerState* oldRasterState = nullptr;
    m_pDeviceContext->RSGetState(&oldRasterState);

    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_FRONT;
    rsDesc.DepthClipEnable = TRUE;

    ID3D11RasterizerState* cubeRasterState = nullptr;
    hr = m_pDevice->CreateRasterizerState(&rsDesc, &cubeRasterState);
    if (FAILED(hr))
    {
        for (UINT i = 0; i < 6; ++i)
            faceRTV[i]->Release();
        irradianceSRV->Release();
        irradianceTexture->Release();
        convolutionPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        if (oldRasterState) oldRasterState->Release();
        return hr;
    }

    D3D11_VIEWPORT cubeViewport{};
    cubeViewport.TopLeftX = 0.0f;
    cubeViewport.TopLeftY = 0.0f;
    cubeViewport.Width = static_cast<float>(irradianceSize);
    cubeViewport.Height = static_cast<float>(irradianceSize);
    cubeViewport.MinDepth = 0.0f;
    cubeViewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &cubeViewport);
    m_pDeviceContext->RSSetState(cubeRasterState);

    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->VSSetShader(m_pSkyVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(convolutionPS, nullptr, 0);
    m_pDeviceContext->PSSetShaderResources(0, 1, &environmentCubeSRV);
    m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerState);

    XMVECTOR eye = XMVectorZero();
    CubeCaptureFace captureFaces[6]{};
    GetCubeCaptureFaces(captureFaces);

    captureModel->SetModelMatrix(XMMatrixIdentity());

    ID3D11Buffer* cameraVPBuffer = camera->GetVPBuffer();
    if (cameraVPBuffer == nullptr)
    {
        for (UINT i = 0; i < 6; ++i)
            faceRTV[i]->Release();
        irradianceSRV->Release();
        irradianceTexture->Release();
        convolutionPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        if (oldRasterState) oldRasterState->Release();
        if (cubeRasterState) cubeRasterState->Release();
        return E_FAIL;
    }

    const XMMATRIX proj = BuildCubemapCaptureProjection();
    for (UINT face = 0; face < 6; ++face)
    {
        const float clear[4] = { 0, 0, 0, 1 };
        m_pDeviceContext->OMSetRenderTargets(1, &faceRTV[face], nullptr);
        m_pDeviceContext->ClearRenderTargetView(faceRTV[face], clear);

        const XMMATRIX view = XMMatrixLookToLH(eye, captureFaces[face].Target, captureFaces[face].Up);
        const XMMATRIX vp = XMMatrixTranspose(view * proj);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = m_pDeviceContext->Map(cameraVPBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
            break;
        std::memcpy(mapped.pData, &vp, sizeof(XMMATRIX));
        m_pDeviceContext->Unmap(cameraVPBuffer, 0);
        m_pDeviceContext->VSSetConstantBuffers(1, 1, &cameraVPBuffer);

        captureModel->Render();
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

    m_pDeviceContext->OMSetRenderTargets(1, &oldRTV, oldDSV);
    m_pDeviceContext->RSSetViewports(1, &oldViewport);
    m_pDeviceContext->RSSetState(oldRasterState);

    if (oldRTV) oldRTV->Release();
    if (oldDSV) oldDSV->Release();
    if (oldRasterState) oldRasterState->Release();
    if (cubeRasterState) cubeRasterState->Release();

    for (UINT i = 0; i < 6; ++i)
        faceRTV[i]->Release();

    irradianceTexture->Release();
    convolutionPS->Release();

    if (FAILED(hr))
    {
        irradianceSRV->Release();
        return hr;
    }

    *outIrradianceSRV = irradianceSRV;
    return S_OK;
}

HRESULT Render::RebuildIrradianceFromEnvironment()
{
    if (m_pIrradianceSRV)
    {
        m_pIrradianceSRV->Release();
        m_pIrradianceSRV = nullptr;
    }

    if (m_pEnvironmentSRV == nullptr)
        return E_FAIL;

    const UINT size = (m_irradianceMapSize >= 64) ? 64u : 32u;
    ID3D11ShaderResourceView* newIrradiance = nullptr;
    const HRESULT hr = ConvolveCubemapToIrradiance(m_pEnvironmentSRV, size, &newIrradiance);
    if (FAILED(hr) || newIrradiance == nullptr)
    {
        if (newIrradiance != nullptr)
            newIrradiance->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    m_pIrradianceSRV = newIrradiance;
    return S_OK;
}

HRESULT Render::GenerateBRDFLUT(
    UINT lutWidth,
    UINT lutHeight,
    ID3D11ShaderResourceView** outBRDFLUTSRV)
{
    if (outBRDFLUTSRV == nullptr)
        return E_INVALIDARG;
    *outBRDFLUTSRV = nullptr;

    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    m_pDeviceContext->OMGetRenderTargets(1, &oldRTV, &oldDSV);

    UINT oldViewportCount = 1;
    D3D11_VIEWPORT oldViewport{};
    m_pDeviceContext->RSGetViewports(&oldViewportCount, &oldViewport);

    D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    m_pDeviceContext->IAGetPrimitiveTopology(&oldTopology);

    ID3D11InputLayout* oldLayout = nullptr;
    ID3D11VertexShader* oldVS = nullptr;
    ID3D11PixelShader* oldPS = nullptr;
    m_pDeviceContext->IAGetInputLayout(&oldLayout);
    m_pDeviceContext->VSGetShader(&oldVS, nullptr, nullptr);
    m_pDeviceContext->PSGetShader(&oldPS, nullptr, nullptr);

    ID3DBlob* fullScreenVsBlob = nullptr;
    HRESULT hr = CompileShader(L"BRDFLutVS.vs", &fullScreenVsBlob);
    if (FAILED(hr) || fullScreenVsBlob == nullptr)
    {
        if (oldLayout) oldLayout->Release();
        if (oldVS) oldVS->Release();
        if (oldPS) oldPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ID3D11VertexShader* fullScreenVS = nullptr;
    hr = m_pDevice->CreateVertexShader(
        fullScreenVsBlob->GetBufferPointer(),
        fullScreenVsBlob->GetBufferSize(),
        nullptr,
        &fullScreenVS
    );
    fullScreenVsBlob->Release();
    if (FAILED(hr) || fullScreenVS == nullptr)
    {
        if (oldLayout) oldLayout->Release();
        if (oldVS) oldVS->Release();
        if (oldPS) oldPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ID3DBlob* brdfPsBlob = nullptr;
    hr = CompileShader(L"BRDFIntegration.ps", &brdfPsBlob);
    if (FAILED(hr) || brdfPsBlob == nullptr)
    {
        fullScreenVS->Release();
        if (oldLayout) oldLayout->Release();
        if (oldVS) oldVS->Release();
        if (oldPS) oldPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ID3D11PixelShader* brdfPS = nullptr;
    hr = m_pDevice->CreatePixelShader(
        brdfPsBlob->GetBufferPointer(),
        brdfPsBlob->GetBufferSize(),
        nullptr,
        &brdfPS
    );
    brdfPsBlob->Release();
    if (FAILED(hr) || brdfPS == nullptr)
    {
        fullScreenVS->Release();
        if (oldLayout) oldLayout->Release();
        if (oldVS) oldVS->Release();
        if (oldPS) oldPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = lutWidth;
    texDesc.Height = lutHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* lutTexture = nullptr;
    hr = m_pDevice->CreateTexture2D(&texDesc, nullptr, &lutTexture);
    if (FAILED(hr) || lutTexture == nullptr)
    {
        brdfPS->Release();
        fullScreenVS->Release();
        if (oldLayout) oldLayout->Release();
        if (oldVS) oldVS->Release();
        if (oldPS) oldPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ID3D11RenderTargetView* lutRTV = nullptr;
    hr = m_pDevice->CreateRenderTargetView(lutTexture, nullptr, &lutRTV);
    if (FAILED(hr) || lutRTV == nullptr)
    {
        lutTexture->Release();
        brdfPS->Release();
        fullScreenVS->Release();
        if (oldLayout) oldLayout->Release();
        if (oldVS) oldVS->Release();
        if (oldPS) oldPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    ID3D11ShaderResourceView* lutSRV = nullptr;
    hr = m_pDevice->CreateShaderResourceView(lutTexture, nullptr, &lutSRV);
    if (FAILED(hr) || lutSRV == nullptr)
    {
        lutRTV->Release();
        lutTexture->Release();
        brdfPS->Release();
        fullScreenVS->Release();
        if (oldLayout) oldLayout->Release();
        if (oldVS) oldVS->Release();
        if (oldPS) oldPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    float clearColor[4] = { 0, 0, 0, 0 };
    m_pDeviceContext->OMSetRenderTargets(1, &lutRTV, nullptr);
    m_pDeviceContext->ClearRenderTargetView(lutRTV, clearColor);

    D3D11_VIEWPORT lutViewport{};
    lutViewport.TopLeftX = 0.0f;
    lutViewport.TopLeftY = 0.0f;
    lutViewport.Width = static_cast<float>(lutWidth);
    lutViewport.Height = static_cast<float>(lutHeight);
    lutViewport.MinDepth = 0.0f;
    lutViewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &lutViewport);

    m_pDeviceContext->IASetInputLayout(nullptr);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pDeviceContext->VSSetShader(fullScreenVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(brdfPS, nullptr, 0);
    m_pDeviceContext->Draw(3, 0);

    m_pDeviceContext->OMSetRenderTargets(1, &oldRTV, oldDSV);
    if (oldViewportCount > 0)
        m_pDeviceContext->RSSetViewports(1, &oldViewport);
    m_pDeviceContext->IASetPrimitiveTopology(oldTopology);
    m_pDeviceContext->IASetInputLayout(oldLayout);
    m_pDeviceContext->VSSetShader(oldVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(oldPS, nullptr, 0);

    if (oldLayout) oldLayout->Release();
    if (oldVS) oldVS->Release();
    if (oldPS) oldPS->Release();
    if (oldRTV) oldRTV->Release();
    if (oldDSV) oldDSV->Release();

    lutRTV->Release();
    lutTexture->Release();
    brdfPS->Release();
    fullScreenVS->Release();

    *outBRDFLUTSRV = lutSRV;
    return S_OK;
}

HRESULT Render::PrefilterCubemapSpecular(
    ID3D11ShaderResourceView* environmentCubeSRV,
    UINT prefilterSize,
    UINT mipLevels,
    ID3D11ShaderResourceView** outPrefilterSRV)
{
    if (environmentCubeSRV == nullptr || outPrefilterSRV == nullptr || camera == nullptr)
        return E_INVALIDARG;

    *outPrefilterSRV = nullptr;

    ModelManagerAbstract* captureModel = ModelFactory::CreateModel(ModelFactory::ModelCode::cube, m_pDeviceContext);
    if (captureModel == nullptr)
        return E_FAIL;

    struct ScopedModelRelease
    {
        ModelManagerAbstract*& ModelRef;
        ~ScopedModelRelease()
        {
            if (ModelRef != nullptr)
            {
                ModelFactory::ReleaseModel(ModelRef);
                ModelRef = nullptr;
            }
        }
    } captureModelGuard{ captureModel };

    HRESULT hr = captureModel->InitModel(m_pDevice);
    if (FAILED(hr))
        return hr;

    ID3DBlob* prefilterPsBlob = nullptr;
    hr = CompileShader(L"SpecularPrefilter.ps", &prefilterPsBlob);
    if (FAILED(hr) || prefilterPsBlob == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    ID3D11PixelShader* prefilterPS = nullptr;
    hr = m_pDevice->CreatePixelShader(
        prefilterPsBlob->GetBufferPointer(),
        prefilterPsBlob->GetBufferSize(),
        nullptr,
        &prefilterPS
    );
    prefilterPsBlob->Release();
    if (FAILED(hr) || prefilterPS == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    D3D11_BUFFER_DESC prefilterCbDesc{};
    prefilterCbDesc.Usage = D3D11_USAGE_DEFAULT;
    prefilterCbDesc.ByteWidth = sizeof(SpecularPrefilterCB);
    prefilterCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    ID3D11Buffer* prefilterCB = nullptr;
    hr = m_pDevice->CreateBuffer(&prefilterCbDesc, nullptr, &prefilterCB);
    if (FAILED(hr) || prefilterCB == nullptr)
    {
        prefilterPS->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    D3D11_TEXTURE2D_DESC cubeDesc{};
    cubeDesc.Width = prefilterSize;
    cubeDesc.Height = prefilterSize;
    cubeDesc.MipLevels = mipLevels;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.Usage = D3D11_USAGE_DEFAULT;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    ID3D11Texture2D* prefilterTexture = nullptr;
    hr = m_pDevice->CreateTexture2D(&cubeDesc, nullptr, &prefilterTexture);
    if (FAILED(hr) || prefilterTexture == nullptr)
    {
        prefilterCB->Release();
        prefilterPS->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC prefilterSrvDesc{};
    prefilterSrvDesc.Format = cubeDesc.Format;
    prefilterSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    prefilterSrvDesc.TextureCube.MostDetailedMip = 0;
    prefilterSrvDesc.TextureCube.MipLevels = mipLevels;

    ID3D11ShaderResourceView* prefilterSRV = nullptr;
    hr = m_pDevice->CreateShaderResourceView(prefilterTexture, &prefilterSrvDesc, &prefilterSRV);
    if (FAILED(hr) || prefilterSRV == nullptr)
    {
        prefilterTexture->Release();
        prefilterCB->Release();
        prefilterPS->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    std::vector<ID3D11RenderTargetView*> faceRTVs;
    faceRTVs.resize(static_cast<size_t>(mipLevels) * 6u, nullptr);
    for (UINT mip = 0; mip < mipLevels; ++mip)
    {
        for (UINT face = 0; face < 6; ++face)
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = cubeDesc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = mip;
            rtvDesc.Texture2DArray.FirstArraySlice = face;
            rtvDesc.Texture2DArray.ArraySize = 1;

            hr = m_pDevice->CreateRenderTargetView(
                prefilterTexture,
                &rtvDesc,
                &faceRTVs[static_cast<size_t>(mip) * 6u + static_cast<size_t>(face)]
            );
            if (FAILED(hr))
            {
                for (ID3D11RenderTargetView* rtv : faceRTVs)
                {
                    if (rtv != nullptr)
                        rtv->Release();
                }
                prefilterSRV->Release();
                prefilterTexture->Release();
                prefilterCB->Release();
                prefilterPS->Release();
                return hr;
            }
        }
    }

    UINT oldViewportCount = 1;
    D3D11_VIEWPORT oldViewport{};
    m_pDeviceContext->RSGetViewports(&oldViewportCount, &oldViewport);

    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    m_pDeviceContext->OMGetRenderTargets(1, &oldRTV, &oldDSV);

    ID3D11RasterizerState* oldRasterState = nullptr;
    m_pDeviceContext->RSGetState(&oldRasterState);

    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_FRONT;
    rsDesc.DepthClipEnable = TRUE;

    ID3D11RasterizerState* cubeRasterState = nullptr;
    hr = m_pDevice->CreateRasterizerState(&rsDesc, &cubeRasterState);
    if (FAILED(hr))
    {
        for (ID3D11RenderTargetView* rtv : faceRTVs)
        {
            if (rtv != nullptr)
                rtv->Release();
        }
        prefilterSRV->Release();
        prefilterTexture->Release();
        prefilterCB->Release();
        prefilterPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        if (oldRasterState) oldRasterState->Release();
        return hr;
    }

    m_pDeviceContext->RSSetState(cubeRasterState);
    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->VSSetShader(m_pSkyVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(prefilterPS, nullptr, 0);
    m_pDeviceContext->PSSetShaderResources(0, 1, &environmentCubeSRV);
    m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerState);
    m_pDeviceContext->PSSetConstantBuffers(4, 1, &prefilterCB);

    XMVECTOR eye = XMVectorZero();
    CubeCaptureFace captureFaces[6]{};
    GetCubeCaptureFaces(captureFaces);

    captureModel->SetModelMatrix(XMMatrixIdentity());

    ID3D11Buffer* cameraVPBuffer = camera->GetVPBuffer();
    if (cameraVPBuffer == nullptr)
    {
        for (ID3D11RenderTargetView* rtv : faceRTVs)
        {
            if (rtv != nullptr)
                rtv->Release();
        }
        prefilterSRV->Release();
        prefilterTexture->Release();
        prefilterCB->Release();
        prefilterPS->Release();
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
        if (oldRasterState) oldRasterState->Release();
        if (cubeRasterState) cubeRasterState->Release();
        return E_FAIL;
    }

    const XMMATRIX proj = BuildCubemapCaptureProjection();
    const UINT sourceMipCount = ResolveCubemapMipCountFromSRV(environmentCubeSRV);

    for (UINT mip = 0; mip < mipLevels; ++mip)
    {
        const UINT mipWidth = (prefilterSize >> mip) > 0u ? (prefilterSize >> mip) : 1u;
        const UINT mipHeight = (prefilterSize >> mip) > 0u ? (prefilterSize >> mip) : 1u;

        D3D11_VIEWPORT cubeViewport{};
        cubeViewport.TopLeftX = 0.0f;
        cubeViewport.TopLeftY = 0.0f;
        cubeViewport.Width = static_cast<float>(mipWidth);
        cubeViewport.Height = static_cast<float>(mipHeight);
        cubeViewport.MinDepth = 0.0f;
        cubeViewport.MaxDepth = 1.0f;
        m_pDeviceContext->RSSetViewports(1, &cubeViewport);

        const float roughness = (mipLevels > 1) ? (static_cast<float>(mip) / static_cast<float>(mipLevels - 1)) : 0.0f;
        SpecularPrefilterCB prefilterData{};
        prefilterData.Params = XMFLOAT4(roughness, static_cast<float>(sourceMipCount), 0.0f, 0.0f);
        m_pDeviceContext->UpdateSubresource(prefilterCB, 0, nullptr, &prefilterData, 0, 0);

        for (UINT face = 0; face < 6; ++face)
        {
            ID3D11RenderTargetView* rtv = faceRTVs[static_cast<size_t>(mip) * 6u + static_cast<size_t>(face)];
            const float clear[4] = { 0, 0, 0, 1 };
            m_pDeviceContext->OMSetRenderTargets(1, &rtv, nullptr);
            m_pDeviceContext->ClearRenderTargetView(rtv, clear);

            const XMMATRIX view = XMMatrixLookToLH(eye, captureFaces[face].Target, captureFaces[face].Up);
            const XMMATRIX vp = XMMatrixTranspose(view * proj);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = m_pDeviceContext->Map(cameraVPBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr))
                break;
            std::memcpy(mapped.pData, &vp, sizeof(XMMATRIX));
            m_pDeviceContext->Unmap(cameraVPBuffer, 0);
            m_pDeviceContext->VSSetConstantBuffers(1, 1, &cameraVPBuffer);

            captureModel->Render();
        }

        if (FAILED(hr))
            break;
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

    m_pDeviceContext->OMSetRenderTargets(1, &oldRTV, oldDSV);
    if (oldViewportCount > 0)
        m_pDeviceContext->RSSetViewports(1, &oldViewport);
    m_pDeviceContext->RSSetState(oldRasterState);

    if (oldRTV) oldRTV->Release();
    if (oldDSV) oldDSV->Release();
    if (oldRasterState) oldRasterState->Release();
    if (cubeRasterState) cubeRasterState->Release();

    for (ID3D11RenderTargetView* rtv : faceRTVs)
    {
        if (rtv != nullptr)
            rtv->Release();
    }

    prefilterTexture->Release();
    prefilterCB->Release();
    prefilterPS->Release();

    if (FAILED(hr))
    {
        prefilterSRV->Release();
        return hr;
    }

    *outPrefilterSRV = prefilterSRV;
    return S_OK;
}

HRESULT Render::RebuildSpecularIBLFromEnvironment()
{
    if (m_pSpecularPrefilterSRV != nullptr)
    {
        m_pSpecularPrefilterSRV->Release();
        m_pSpecularPrefilterSRV = nullptr;
    }

    if (m_pEnvironmentSRV == nullptr)
        return E_FAIL;

    if (m_pBRDFLutSRV == nullptr)
    {
        ID3D11ShaderResourceView* brdfLut = nullptr;
        const HRESULT lutHr = GenerateBRDFLUT(512, 512, &brdfLut);
        if (FAILED(lutHr) || brdfLut == nullptr)
        {
            if (brdfLut != nullptr)
                brdfLut->Release();
            return FAILED(lutHr) ? lutHr : E_FAIL;
        }
        m_pBRDFLutSRV = brdfLut;
    }

    const UINT size = (m_specularPrefilterSize < 16u) ? 16u : m_specularPrefilterSize;
    const UINT maxMipsBySize = CountMipLevelsFromSize(size);
    UINT mipLevels = (m_specularPrefilterMipLevels < 2u) ? 2u : m_specularPrefilterMipLevels;
    if (mipLevels > maxMipsBySize)
        mipLevels = maxMipsBySize;

    ID3D11ShaderResourceView* newPrefilter = nullptr;
    const HRESULT prefilterHr = PrefilterCubemapSpecular(m_pEnvironmentSRV, size, mipLevels, &newPrefilter);
    if (FAILED(prefilterHr) || newPrefilter == nullptr)
    {
        if (newPrefilter != nullptr)
            newPrefilter->Release();
        return FAILED(prefilterHr) ? prefilterHr : E_FAIL;
    }

    m_pSpecularPrefilterSRV = newPrefilter;
    m_specularPrefilterMipLevels = mipLevels;
    return S_OK;
}

void Render::ReleaseTextureResources()
{
    if (m_pAlbedoTextureSRV) { m_pAlbedoTextureSRV->Release(); m_pAlbedoTextureSRV = nullptr; }
    if (m_pNormalTextureSRV) { m_pNormalTextureSRV->Release(); m_pNormalTextureSRV = nullptr; }
    if (m_pRoughnessTextureSRV) { m_pRoughnessTextureSRV->Release(); m_pRoughnessTextureSRV = nullptr; }
    if (m_pSamplerState) { m_pSamplerState->Release(); m_pSamplerState = nullptr; }
}

void Render::ReleaseSkyResources()
{
    if (m_pEnvironmentSRV) { m_pEnvironmentSRV->Release(); m_pEnvironmentSRV = nullptr; }
    if (m_pIrradianceSRV) { m_pIrradianceSRV->Release(); m_pIrradianceSRV = nullptr; }
    if (m_pSpecularPrefilterSRV) { m_pSpecularPrefilterSRV->Release(); m_pSpecularPrefilterSRV = nullptr; }
    if (m_pBRDFLutSRV) { m_pBRDFLutSRV->Release(); m_pBRDFLutSRV = nullptr; }
    if (m_pSkyVertexShader) { m_pSkyVertexShader->Release(); m_pSkyVertexShader = nullptr; }
    if (m_pSkyPixelShader) { m_pSkyPixelShader->Release(); m_pSkyPixelShader = nullptr; }
    if (m_pSkyRasterState) { m_pSkyRasterState->Release(); m_pSkyRasterState = nullptr; }
    if (m_pSkyDepthState) { m_pSkyDepthState->Release(); m_pSkyDepthState = nullptr; }
    for (HdriCubemapCacheEntry& cacheItem : m_hdriCubemapCache)
    {
        if (cacheItem.CubemapSRV != nullptr)
        {
            cacheItem.CubemapSRV->Release();
            cacheItem.CubemapSRV = nullptr;
        }
    }
    m_hdriCubemapCache.clear();
}

void Render::InitImGui()
{
    if (ImGui::GetCurrentContext() != nullptr)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(m_hWnd);
    ImGui_ImplDX11_Init(m_pDevice, m_pDeviceContext);
}

void Render::ShutdownImGui()
{
    if (ImGui::GetCurrentContext() == nullptr)
        return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Render::RenderImGui()
{
    if (ImGui::GetCurrentContext() == nullptr)
        return;

    if (!m_labUiMode && m_forceCopyPostprocess)
    {
        // In lab mode off, force-copy is unavailable and should not affect rendering.
        m_forceCopyPostprocess = false;
    }

    if (!m_labUiMode &&
        (m_debugViewMode == DebugView_AlbedoOnly ||
         m_debugViewMode == DebugView_LightMask ||
         m_debugViewMode == DebugView_NormalWS ||
         m_debugViewMode == DebugView_TBNSign))
    {
        m_debugViewMode = DebugView_Final;
    }

    if (!m_labUiMode)
    {
        // Lab-only ambient debug source must not affect the standard mode.
        m_labAmbientSourceMode = 0;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    auto showDisabledHint = [](const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", text);
    };

    const bool isBrdfDebugView =
        (m_debugViewMode == DebugView_NDF) ||
        (m_debugViewMode == DebugView_Geometry) ||
        (m_debugViewMode == DebugView_Fresnel);

    const bool isFinalView = (m_debugViewMode == DebugView_Final);
    const bool lightAffectsCurrentView =
        (m_debugViewMode != DebugView_AlbedoOnly) &&
        (m_debugViewMode != DebugView_NormalWS) &&
        (m_debugViewMode != DebugView_TBNSign);
    const int materialIndex = (m_materialPresetIndex >= 0 && m_materialPresetIndex < (int)IM_ARRAYSIZE(kMaterialPresets))
        ? m_materialPresetIndex
        : 0;
    const MaterialPreset& activePreset = kMaterialPresets[materialIndex];
    const bool presetHasNormalSource = (activePreset.NormalPath != nullptr);
    const bool presetHasRoughnessSource = (activePreset.RoughnessPath != nullptr || activePreset.HeightPath != nullptr);
    const bool normalMapLoaded = (m_pNormalTextureSRV != nullptr);
    const bool roughnessMapLoaded = (m_pRoughnessTextureSRV != nullptr);
    const bool canUseNormalMap = presetHasNormalSource && normalMapLoaded;
    const bool canUseRoughnessMap = presetHasRoughnessSource && roughnessMapLoaded;

    ImGui::Begin("Lights");
    static int selectedLight = 0;
    const char* lightNames[] = { "Light 1", "Light 2", "Light 3" };
    ImGui::Combo("Selected light", &selectedLight, lightNames, IM_ARRAYSIZE(lightNames));
    selectedLight = (selectedLight < 0) ? 0 : (selectedLight > 2 ? 2 : selectedLight);

    if (!lightAffectsCurrentView)
        ImGui::BeginDisabled(true);

    ImGui::ColorEdit3("Color", &m_lightColors[selectedLight].x);
    if (!lightAffectsCurrentView) showDisabledHint("Lights do not affect this debug view.");
    ImGui::SliderFloat("Intensity", &m_lightBrightness[selectedLight], 0.0f, 6.0f);
    if (!lightAffectsCurrentView) showDisabledHint("Lights do not affect this debug view.");
    if (m_labUiMode)
    {
        ImGui::SliderFloat("Range", &m_lightRanges[selectedLight], 1.0f, 30.0f);
        if (!lightAffectsCurrentView) showDisabledHint("Lights do not affect this debug view.");
    }
    ImGui::DragFloat3("Position", &m_lightPositions[selectedLight].x, 0.05f, -20.0f, 20.0f);
    if (!lightAffectsCurrentView) showDisabledHint("Lights do not affect this debug view.");

    if (!lightAffectsCurrentView)
    {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Light controls are inactive in current debug view.");
    }

    if (m_labUiMode)
    {
        ImGui::Separator();
        for (int i = 0; i < 3; ++i)
        {
            ImGui::Text(
                "L%d pos(%.2f %.2f %.2f) range=%.1f I=%.2f",
                i + 1,
                m_lightPositions[i].x, m_lightPositions[i].y, m_lightPositions[i].z,
                m_lightRanges[i],
                m_lightBrightness[i]
            );
        }
    }
    ImGui::End();

    ImGui::Begin("Material");
    if (!canUseNormalMap)
        m_enableNormalMap = false;
    if (!canUseRoughnessMap)
        m_enableRoughnessMap = false;

    bool gridModeUi = m_gridMode;
    ImGui::Checkbox("Grid mode (instancing)", &gridModeUi);
    m_gridMode = gridModeUi;

    ImGui::Checkbox("Use albedo texture", &m_enableTextures);

    if (!canUseNormalMap)
        ImGui::BeginDisabled(true);
    ImGui::Checkbox("Use normal map", &m_enableNormalMap);
    if (!canUseNormalMap)
    {
        if (!presetHasNormalSource) showDisabledHint("Current material preset has no normal map source.");
        else showDisabledHint("Normal map texture is not loaded.");
        ImGui::EndDisabled();
    }

    if (!canUseRoughnessMap)
        ImGui::BeginDisabled(true);
    ImGui::Checkbox("Use roughness map", &m_enableRoughnessMap);
    if (!canUseRoughnessMap)
    {
        if (!presetHasRoughnessSource) showDisabledHint("Current material preset has no roughness/height map source.");
        else showDisabledHint("Roughness/height texture is not loaded.");
        ImGui::EndDisabled();
    }

    ImGui::Checkbox("Enable skybox", &m_enableSkybox);

    if (m_labUiMode)
    {
        if (ImGui::Button("Refresh skybox list"))
        {
            RefreshSkyboxList();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload skybox"))
        {
            m_enableSkybox = true;
            InitSkyResources();
        }
    }

    if (m_environmentEntries.empty())
        RefreshSkyboxList();

    std::string currentSkyboxName = "<none>";
    if (!m_environmentEntries.empty() && m_skyboxFileIndex >= 0 && m_skyboxFileIndex < static_cast<int>(m_environmentEntries.size()))
        currentSkyboxName = WideToUtf8(m_environmentEntries[m_skyboxFileIndex].DisplayName);

    if (ImGui::BeginCombo("Skybox file", currentSkyboxName.c_str()))
    {
        for (int i = 0; i < static_cast<int>(m_environmentEntries.size()); ++i)
        {
            const bool isSelected = (i == m_skyboxFileIndex);
            const std::string optionName = WideToUtf8(m_environmentEntries[i].DisplayName);
            if (ImGui::Selectable(optionName.c_str(), isSelected))
            {
                m_skyboxFileIndex = i;
                m_enableSkybox = true;
                InitSkyResources();
            }
            if (ImGui::IsItemHovered() &&
                m_environmentEntries[i].SourceKind == EnvironmentSourceKind::Hdri &&
                !m_environmentEntries[i].HasConvertedDDS &&
                !m_environmentEntries[i].RuntimeConverted)
            {
                ImGui::SetTooltip("Select to convert HDRI to cubemap at runtime");
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const bool canUseDiffuseIBL = (m_pIrradianceSRV != nullptr);
    if (!canUseDiffuseIBL)
    {
        m_enableDiffuseIBL = false;
        ImGui::BeginDisabled(true);
    }
    ImGui::Checkbox("Use Diffuse IBL", &m_enableDiffuseIBL);
    if (!canUseDiffuseIBL)
    {
        showDisabledHint("Irradiance map is not available for current environment.");
        ImGui::EndDisabled();
    }
    const bool canEditIblIntensity = m_enableDiffuseIBL && canUseDiffuseIBL;
    if (!canEditIblIntensity)
        ImGui::BeginDisabled(true);
    ImGui::SliderFloat("IBL intensity", &m_iblIntensity, 0.0f, 5.0f);
    if (!canEditIblIntensity)
    {
        showDisabledHint("Enable Diffuse IBL and load irradiance map first.");
        ImGui::EndDisabled();
    }

    const bool canUseSpecularIBL = (m_pSpecularPrefilterSRV != nullptr && m_pBRDFLutSRV != nullptr);
    if (!canUseSpecularIBL)
    {
        m_enableSpecularIBL = false;
        ImGui::BeginDisabled(true);
    }
    ImGui::Checkbox("Use Specular IBL", &m_enableSpecularIBL);
    if (!canUseSpecularIBL)
    {
        showDisabledHint("Specular prefilter cubemap or BRDF LUT is not available.");
        ImGui::EndDisabled();
    }

    const bool canEditSpecularIblIntensity = m_enableSpecularIBL && canUseSpecularIBL;
    if (!canEditSpecularIblIntensity)
        ImGui::BeginDisabled(true);
    ImGui::SliderFloat("Specular IBL intensity", &m_specularIBLIntensity, 0.0f, 5.0f);
    if (!canEditSpecularIblIntensity)
    {
        showDisabledHint("Enable Specular IBL and ensure prefilter/LUT resources are loaded.");
        ImGui::EndDisabled();
    }

    if (m_labUiMode)
    {
        static const char* ambientSourceModes[] = { "Irradiance", "Constant" };
        int ambientMode = (m_labAmbientSourceMode == 1) ? 1 : 0;
        if (ImGui::Combo("Ambient source (lab)", &ambientMode, ambientSourceModes, IM_ARRAYSIZE(ambientSourceModes)))
        {
            m_labAmbientSourceMode = (ambientMode == 1) ? 1 : 0;
        }

        static const char* irradianceQualityModes[] = { "32 (fast)", "64 (smooth)" };
        int qualityIndex = (m_irradianceMapSize >= 64) ? 1 : 0;
        if (ImGui::Combo("Irradiance quality (lab)", &qualityIndex, irradianceQualityModes, IM_ARRAYSIZE(irradianceQualityModes)))
        {
            m_irradianceMapSize = (qualityIndex == 1) ? 64 : 32;
            if (m_pEnvironmentSRV != nullptr)
            {
                const HRESULT rebuildHr = RebuildIrradianceFromEnvironment();
                if (FAILED(rebuildHr))
                {
                    m_enableDiffuseIBL = false;
                    DebugLogA("[LAB3][DIAG] irradiance rebuild failed after quality change (hr=0x%08X)\n", static_cast<unsigned int>(rebuildHr));
                }
            }
        }
    }

    if (m_labUiMode)
    {
        if (!isFinalView)
            ImGui::BeginDisabled(true);
        ImGui::Checkbox("Force copy postprocess", &m_forceCopyPostprocess);
        if (!isFinalView)
        {
            showDisabledHint("Force copy affects only 'Final PBR' mode.");
            ImGui::EndDisabled();
        }
    }

    if (m_labUiMode)
    {
        const bool canNormalStrength = m_enableNormalMap && canUseNormalMap;
        if (!canNormalStrength)
            ImGui::BeginDisabled(true);
        ImGui::SliderFloat("Normal strength", &m_normalStrength, 0.0f, 2.0f);
        if (!canNormalStrength)
        {
            showDisabledHint("Enable normal map and load normal texture first.");
            ImGui::EndDisabled();
        }

        const bool canRoughStrength = m_enableRoughnessMap && canUseRoughnessMap;
        if (!canRoughStrength)
            ImGui::BeginDisabled(true);
        ImGui::SliderFloat("Roughness map strength", &m_roughnessMapStrength, 0.0f, 1.0f);
        if (!canRoughStrength)
        {
            showDisabledHint("Enable roughness map and load roughness texture first.");
            ImGui::EndDisabled();
        }
    }

    const bool lockSingleMaterialByRoughnessTexture = (m_enableRoughnessMap && canUseRoughnessMap);
    const bool lockSingleMaterialControls = lockSingleMaterialByRoughnessTexture;
    if (lockSingleMaterialControls)
        ImGui::BeginDisabled(true);
    ImGui::SliderFloat("Roughness", &m_materialRoughness, 0.04f, 1.0f);
    if (lockSingleMaterialByRoughnessTexture)
        showDisabledHint("Roughness slider is locked while roughness map is enabled.");
    ImGui::SliderFloat("Metalness", &m_materialMetalness, 0.0f, 1.0f);
    if (lockSingleMaterialControls)
    {
        showDisabledHint("Metalness slider is locked while roughness map is enabled.");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Roughness map is active: Roughness/Metalness sliders are locked.");
    }
    else if (m_gridMode)
    {
        ImGui::TextDisabled("Roughness/Metalness shift the grid distribution in Grid mode.");
    }
    ImGui::ColorEdit3("Color", &m_materialColor.x);
    if (m_labUiMode && ImGui::Button("Reference material preset"))
    {
        m_materialRoughness = 0.22f;
        m_materialMetalness = 0.88f;
        m_materialColor = XMFLOAT3(0.98f, 0.98f, 0.98f);
    }

    static const char* materialNames[] = { "Marble", "Roof", "Legacy cat" };
    int materialIdx = m_materialPresetIndex;
    if (ImGui::Combo("Material preset", &materialIdx, materialNames, IM_ARRAYSIZE(materialNames)))
    {
        m_materialPresetIndex = materialIdx;
        InitTextureResources();
    }
    if (m_labUiMode && ImGui::Button("Reload material textures"))
    {
        InitTextureResources();
    }

    if (!m_gridMode)
        ImGui::BeginDisabled(true);
    ImGui::SliderInt("Grid resolution", &m_gridResolution, 2, 10);
    if (!m_gridMode) showDisabledHint("Grid resolution is used only in Grid mode.");
    ImGui::SliderFloat("Grid spacing", &m_gridSpacing, 1.0f, 4.5f);
    if (!m_gridMode)
    {
        showDisabledHint("Grid spacing is used only in Grid mode.");
        ImGui::EndDisabled();
    }

    int uiMode = 0;
    if (m_labUiMode)
    {
        static const char* viewModesFull[] =
        {
            "Final PBR",
            "NDF",
            "Geometry",
            "Fresnel",
            "Albedo only",
            "Light mask",
            "Normal WS",
            "TBN sign"
        };
        switch (m_debugViewMode)
        {
        case DebugView_NDF: uiMode = 1; break;
        case DebugView_Geometry: uiMode = 2; break;
        case DebugView_Fresnel: uiMode = 3; break;
        case DebugView_AlbedoOnly: uiMode = 4; break;
        case DebugView_LightMask: uiMode = 5; break;
        case DebugView_NormalWS: uiMode = 6; break;
        case DebugView_TBNSign: uiMode = 7; break;
        default: uiMode = 0; break;
        }

        if (ImGui::Combo("View mode", &uiMode, viewModesFull, IM_ARRAYSIZE(viewModesFull)))
        {
            switch (uiMode)
            {
            case 1: m_debugViewMode = DebugView_NDF; break;
            case 2: m_debugViewMode = DebugView_Geometry; break;
            case 3: m_debugViewMode = DebugView_Fresnel; break;
            case 4: m_debugViewMode = DebugView_AlbedoOnly; break;
            case 5: m_debugViewMode = DebugView_LightMask; break;
            case 6: m_debugViewMode = DebugView_NormalWS; break;
            case 7: m_debugViewMode = DebugView_TBNSign; break;
            default: m_debugViewMode = DebugView_Final; break;
            }
        }
    }
    else
    {
        static const char* viewModesLab[] =
        {
            "Final PBR",
            "NDF",
            "Geometry",
            "Fresnel"
        };
        switch (m_debugViewMode)
        {
        case DebugView_NDF: uiMode = 1; break;
        case DebugView_Geometry: uiMode = 2; break;
        case DebugView_Fresnel: uiMode = 3; break;
        default: uiMode = 0; break;
        }

        if (ImGui::Combo("View mode", &uiMode, viewModesLab, IM_ARRAYSIZE(viewModesLab)))
        {
            switch (uiMode)
            {
            case 1: m_debugViewMode = DebugView_NDF; break;
            case 2: m_debugViewMode = DebugView_Geometry; break;
            case 3: m_debugViewMode = DebugView_Fresnel; break;
            default: m_debugViewMode = DebugView_Final; break;
            }
        }
    }

    if (m_labUiMode)
    {
        if (!isBrdfDebugView)
            ImGui::BeginDisabled(true);
        ImGui::Checkbox("Extend views", &m_extendViews);
        if (!isBrdfDebugView)
        {
            showDisabledHint("Extend views affects only NDF/Geometry/Fresnel modes.");
            ImGui::EndDisabled();
        }

        const int useTexture = (m_enableTextures && m_pAlbedoTextureSRV != nullptr) ? 1 : 0;
        const int useNormalMap = (m_enableNormalMap && m_pNormalTextureSRV != nullptr) ? 1 : 0;
        const int useRoughnessMap = (m_enableRoughnessMap && m_pRoughnessTextureSRV != nullptr) ? 1 : 0;
        ImGui::Text("Texture SRV: %s", m_pAlbedoTextureSRV ? "loaded" : "null");
        ImGui::Text("Normal SRV: %s", m_pNormalTextureSRV ? "loaded" : "null");
        ImGui::Text("Roughness SRV: %s", m_pRoughnessTextureSRV ? "loaded" : "null");
        ImGui::Text("Skybox SRV: %s", m_pEnvironmentSRV ? "loaded" : "null");
        ImGui::Text("Irradiance SRV: %s", m_pIrradianceSRV ? "loaded" : "null");
        ImGui::Text("Specular prefilter SRV: %s", m_pSpecularPrefilterSRV ? "loaded" : "null");
        ImGui::Text("BRDF LUT SRV: %s", m_pBRDFLutSRV ? "loaded" : "null");
        ImGui::Text("Skybox selected: %s", currentSkyboxName.c_str());
        ImGui::Text("Environment entries: %d", static_cast<int>(m_environmentEntries.size()));
        ImGui::Text("Single sphere scale: %.2f", m_singleSphereScale);
        ImGui::Text("UseTexture in shader: %d", useTexture);
        ImGui::Text("UseNormalMap in shader: %d", useNormalMap);
        ImGui::Text("UseRoughnessMap in shader: %d", useRoughnessMap);
        ImGui::Text("UseDiffuseIBL in shader: %d", (m_enableDiffuseIBL && m_pIrradianceSRV != nullptr) ? 1 : 0);
        ImGui::Text("UseSpecularIBL in shader: %d",
            (m_enableSpecularIBL && m_pSpecularPrefilterSRV != nullptr && m_pBRDFLutSRV != nullptr) ? 1 : 0);
        ImGui::Text("Ambient source (lab): %s", (m_labAmbientSourceMode == 1) ? "Constant" : "Irradiance");
        ImGui::Text("Irradiance size (lab): %d", m_irradianceMapSize);
        const bool forceCopyEffective = m_labUiMode && m_forceCopyPostprocess;
        ImGui::Text("Tonemap effective: %s", (IsTonemapEnabled() && !forceCopyEffective) ? "yes" : "no");
        if (ImGui::Button("Dump diagnostics to Output"))
        {
            m_requestDiagnosticsDump = true;
        }
        ImGui::Text("Hotkeys: 8=Dump diagnostics, 9=Toggle force copy");
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Render::RenderSkybox()
{
    if (!m_enableSkybox || !m_pEnvironmentSRV || !m_pSkyVertexShader || !m_pSkyPixelShader || !m_currentModel)
        return;

    ID3D11RasterizerState* oldRS = nullptr;
    ID3D11DepthStencilState* oldDS = nullptr;
    UINT oldStencilRef = 0;
    m_pDeviceContext->RSGetState(&oldRS);
    m_pDeviceContext->OMGetDepthStencilState(&oldDS, &oldStencilRef);

    XMMATRIX skyModel =
        XMMatrixScaling(30.0f, 30.0f, 30.0f) *
        XMMatrixTranslation(camera->position.x, camera->position.y, camera->position.z);
    m_currentModel->SetModelMatrix(skyModel);

    m_pDeviceContext->RSSetState(m_pSkyRasterState);
    m_pDeviceContext->OMSetDepthStencilState(m_pSkyDepthState, 0);
    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->VSSetShader(m_pSkyVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pSkyPixelShader, nullptr, 0);
    m_pDeviceContext->PSSetShaderResources(0, 1, &m_pEnvironmentSRV);
    m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerState);

    m_currentModel->Render();

    ID3D11ShaderResourceView* nullSRVs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    m_pDeviceContext->PSSetShaderResources(0, 6, nullSRVs);
    m_pDeviceContext->RSSetState(oldRS);
    m_pDeviceContext->OMSetDepthStencilState(oldDS, oldStencilRef);

    if (oldRS) oldRS->Release();
    if (oldDS) oldDS->Release();
}

void Render::RenderLightMarkers()
{
    if (!m_currentModel || !m_pLightMarkerPixelShader || !m_pLightMarkerColorCB)
        return;

    ID3D11ShaderResourceView* nullSRVs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    m_pDeviceContext->PSSetShaderResources(0, 6, nullSRVs);
    m_pDeviceContext->PSSetShader(m_pLightMarkerPixelShader, nullptr, 0);
    m_pDeviceContext->PSSetConstantBuffers(4, 1, &m_pLightMarkerColorCB);

    for (int i = 0; i < 3; ++i)
    {
        m_pointLights[i]->SetPosition(m_lightPositions[i]);
        m_pointLights[i]->SetRange(m_lightRanges[i]);
        m_pointLights[i]->SetColor(m_lightColors[i]);
        m_pointLights[i]->SetIntensity(m_lightBaseIntensity[i] * m_lightBrightness[i]);

        PointLightGPU gpu{};
        m_pointLights[i]->Fill(gpu);

        const float scale = 0.12f;
        XMMATRIX lightModel =
            XMMatrixScaling(scale, scale, scale) *
            XMMatrixTranslation(gpu.Position.x, gpu.Position.y, gpu.Position.z);
        m_currentModel->SetModelMatrix(lightModel);

        const float emissiveScale = 0.3f + 0.9f * m_lightBrightness[i];
        LightMarkerColorCB colorData{};
        colorData.Color = XMFLOAT4(
            m_lightColors[i].x * emissiveScale,
            m_lightColors[i].y * emissiveScale,
            m_lightColors[i].z * emissiveScale,
            1.0f
        );
        m_pDeviceContext->UpdateSubresource(m_pLightMarkerColorCB, 0, nullptr, &colorData, 0, 0);

        m_currentModel->Render();
    }

    m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);
}

void Render::UpdateCamera(WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch(wParam) {
        case 'W':
        case VK_UP:
            camera->Move(MoveDirection::FORWARD);
            break;
        case 'S':
        case VK_DOWN:
            camera->Move(MoveDirection::BACKWARD);
            break;
        case 'A':
        case VK_LEFT:
            camera->Move(MoveDirection::LEFT);
            break;
        case 'D':
        case VK_RIGHT:
            camera->Move(MoveDirection::RIGHT);
            break;
      
        case VK_ADD:
        case 0xBB:
            //camera->Move({ 0.0f, 0.0f, 1.0f });
            break;
        case VK_SPACE:
            m_currentModel->ChangeRotationable();
            break;
        case '1': m_debugViewMode = DebugView_NDF; break;
        case '2': m_debugViewMode = DebugView_Geometry; break;
        case '3': m_debugViewMode = DebugView_Fresnel; break;
        case '4': m_debugViewMode = DebugView_Final; break;
        case '5': m_gridMode = !m_gridMode; break;
        case '6': m_enableTextures = !m_enableTextures; break;
        case '7': m_enableSkybox = !m_enableSkybox; break;
        case '8': m_requestDiagnosticsDump = true; break;
        case '9':
            if (m_labUiMode)
                m_forceCopyPostprocess = !m_forceCopyPostprocess;
            else
                m_forceCopyPostprocess = false;
            break;

        case 'R':
            m_materialRoughness += 0.05f;
            if (m_materialRoughness > 1.0f) m_materialRoughness = 1.0f;
            break;
        case 'F':
            m_materialRoughness -= 0.05f;
            if (m_materialRoughness < 0.04f) m_materialRoughness = 0.04f;
            break;
        case 'T':
            m_materialMetalness += 0.05f;
            if (m_materialMetalness > 1.0f) m_materialMetalness = 1.0f;
            break;
        case 'G':
            m_materialMetalness -= 0.05f;
            if (m_materialMetalness < 0.0f) m_materialMetalness = 0.0f;
            break;
    }

}

void Render::HandleMouse(UINT message, LPARAM lParam)
{
    switch (message)
    {
    case WM_RBUTTONDOWN:
        camera->RightButton(true, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
    case WM_RBUTTONUP:
        camera->RightButton(false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
    case WM_MOUSEMOVE:
        camera->Rotate({ (float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam) });
        break;
    default:
        break;
    }
}

void Render::DrawSceneObjects()
{
    const float gridSphereScale = 0.35f;
    if (m_prevGridMode != m_gridMode)
    {
        if (!m_gridMode)
        {
            // Smoothly restore the main sphere size after leaving grid mode.
            m_singleSphereScale = gridSphereScale;
            m_singleSphereScaleTarget = 1.0f;
        }
        else
        {
            m_singleSphereScale = gridSphereScale;
            m_singleSphereScaleTarget = gridSphereScale;
        }
        m_prevGridMode = m_gridMode;
    }

    if (!m_gridMode)
    {
        const float blendSpeed = 0.22f;
        m_singleSphereScale += (m_singleSphereScaleTarget - m_singleSphereScale) * blendSpeed;
        if (fabsf(m_singleSphereScale - m_singleSphereScaleTarget) < 0.001f)
            m_singleSphereScale = m_singleSphereScaleTarget;
        XMMATRIX singleModel = XMMatrixScaling(m_singleSphereScale, m_singleSphereScale, m_singleSphereScale);
        m_currentModel->SetModelMatrix(singleModel);
        RenderScene(m_materialRoughness, m_materialMetalness, m_materialColor);
        m_currentModel->Update(0.0f);
        m_currentModel->Render();
        return;
    }

    const int rows = (m_gridResolution > 2) ? m_gridResolution : 2;
    const int cols = (m_gridResolution > 2) ? m_gridResolution : 2;
    const float halfRows = (rows - 1) * 0.5f;
    const float halfCols = (cols - 1) * 0.5f;
    const float scale = gridSphereScale;

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const float roughnessBase = static_cast<float>(col) / static_cast<float>(cols - 1);
            const float metalnessBase = static_cast<float>(rows - 1 - row) / static_cast<float>(rows - 1);

            // Apply user roughness/metalness as grid-domain shifts:
            // slider center = 0.5 means no shift.
            const float roughnessShift = (m_materialRoughness - 0.5f);
            const float metalnessShift = (m_materialMetalness - 0.5f);
            float roughness = roughnessBase + roughnessShift;
            float metalness = metalnessBase + metalnessShift;
            if (roughness < 0.04f) roughness = 0.04f;
            if (roughness > 1.0f) roughness = 1.0f;
            if (metalness < 0.0f) metalness = 0.0f;
            if (metalness > 1.0f) metalness = 1.0f;

            const float x = (col - halfCols) * m_gridSpacing;
            const float y = (row - halfRows) * m_gridSpacing;

            XMMATRIX model = XMMatrixScaling(scale, scale, scale) * XMMatrixTranslation(x, -y, 0.0f);
            m_currentModel->SetModelMatrix(model);

            RenderScene(roughness, metalness, m_materialColor);
            m_currentModel->Render();
        }
    }
}

void Render::RenderStart()
{
    m_PostProcessingPass->Update(m_pDevice, m_pDeviceContext);
    if (m_pAnnotation) m_pAnnotation->BeginEvent(L"Render Frame");

    // пїЅпїЅпїЅпїЅпїЅпїЅпїЅ
    if (m_pAnnotation) m_pAnnotation->BeginEvent(L"Clear");
    //m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);
    m_pDeviceContext->ClearState();
    ID3D11ShaderResourceView* nullSRVs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    m_pDeviceContext->PSSetShaderResources(0, 6, nullSRVs);
    m_pRenderedSceneTexture->set(m_pDevice, m_pDeviceContext);
    float BackColor[4] = { 0.06f, 0.08f, 0.10f, 1.0f };
    //m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView, BackColor);
    m_pRenderedSceneTexture->clear(BackColor, m_pDevice, m_pDeviceContext);
    if (m_pAnnotation) m_pAnnotation->EndEvent();
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<FLOAT>(width);
    viewport.Height = static_cast<FLOAT>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &viewport);

    if (m_pSceneDepthView != nullptr)
    {
        m_pDeviceContext->ClearDepthStencilView(m_pSceneDepthView, D3D11_CLEAR_DEPTH, 1.0f, 0);
        ID3D11RenderTargetView* sceneRTV = m_pRenderedSceneTexture->getRTV();
        m_pDeviceContext->OMSetRenderTargets(1, &sceneRTV, m_pSceneDepthView);
    }

    camera->CameraUpdate((float)width / (float)height);

    if (m_requestDiagnosticsDump)
    {
        DumpPipelineState("manual dump");
        m_requestDiagnosticsDump = false;
    }

    RenderSkybox();

    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);
    m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerState);
    m_pDeviceContext->PSSetShaderResources(0, 1, &m_pAlbedoTextureSRV);
    m_pDeviceContext->PSSetShaderResources(1, 1, &m_pNormalTextureSRV);
    m_pDeviceContext->PSSetShaderResources(2, 1, &m_pRoughnessTextureSRV);
    m_pDeviceContext->PSSetShaderResources(3, 1, &m_pIrradianceSRV);
    m_pDeviceContext->PSSetShaderResources(4, 1, &m_pSpecularPrefilterSRV);
    m_pDeviceContext->PSSetShaderResources(5, 1, &m_pBRDFLutSRV);

    if (m_pAnnotation) m_pAnnotation->BeginEvent(L"Draw Model");
    DrawSceneObjects();
    RenderLightMarkers();
    if (m_pAnnotation) m_pAnnotation->EndEvent();
    m_pDeviceContext->PSSetShaderResources(0, 6, nullSRVs);

    const bool forceCopyEffective = m_labUiMode && m_forceCopyPostprocess;
    const bool useTonemap = IsTonemapEnabled() && !forceCopyEffective;
    if (useTonemap)
    {
        m_PostProcessingPass->applyTonemapEffect(m_pDevice, m_pDeviceContext, m_pAnnotation, m_pRenderedSceneTexture, m_pPostProcessedTexture);
    }
    else
    {
        m_PostProcessingPass->applyCopyEffect(m_pDevice, m_pDeviceContext, m_pRenderedSceneTexture, m_pPostProcessedTexture);
    }

    RenderImGui();

    m_pSwapChain->Present(0, 0);
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
        DX_CHECK(hr, L"пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ BackBuffer пїЅпїЅ SwapChain");
        return hr;
    }
        

    SetDObjName(pBackBuffer, "Main_BackBuffer_Texture");

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (FLOAT)width;
    viewport.Height = (FLOAT)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &viewport);

    if (!m_pRenderedSceneTexture)
        m_pRenderedSceneTexture = new RenderTargetTexture(width, height);
    else
        m_pRenderedSceneTexture->setScreenSize(width, height);
    hr = m_pRenderedSceneTexture->initResource(m_pDevice, m_pDeviceContext);
    if (FAILED(hr)) {
        DX_CHECK(hr, L"пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ RenderTargetView");
        return hr;
    }
    SetDObjName(m_pRenderedSceneTexture->getRTV(), "Main_RTV");

    if (!m_pPostProcessedTexture)
        m_pPostProcessedTexture = new RenderTargetTexture(width, height);
    else
        m_pPostProcessedTexture->setScreenSize(width, height);
    hr = m_pPostProcessedTexture->initResource(m_pDevice, m_pDeviceContext, pBackBuffer);
    if (FAILED(hr)) {
        DX_CHECK(hr, L"пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ RenderTargetView");
        return hr;
    }

    SetDObjName(m_pPostProcessedTexture->getRTV(), "PostProcessed_RTV");

    if (m_pSceneDepthView) { m_pSceneDepthView->Release(); m_pSceneDepthView = nullptr; }
    if (m_pSceneDepthTexture) { m_pSceneDepthTexture->Release(); m_pSceneDepthTexture = nullptr; }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_pDevice->CreateTexture2D(&depthDesc, nullptr, &m_pSceneDepthTexture);
    if (FAILED(hr)) {
        pBackBuffer->Release();
        return hr;
    }

    hr = m_pDevice->CreateDepthStencilView(m_pSceneDepthTexture, nullptr, &m_pSceneDepthView);
    if (FAILED(hr)) {
        pBackBuffer->Release();
        return hr;
    }

    pBackBuffer->Release();

    return hr;
}

void Render::Resize()
{
    if (m_pRenderedSceneTexture)
    {
        m_pRenderedSceneTexture->Release();
    }
    if (m_pPostProcessedTexture)
    {
        m_pPostProcessedTexture->Release();
    }
    if (m_pSceneDepthView)
    {
        m_pSceneDepthView->Release();
        m_pSceneDepthView = nullptr;
    }
    if (m_pSceneDepthTexture)
    {
        m_pSceneDepthTexture->Release();
        m_pSceneDepthTexture = nullptr;
    }

    if (m_pSwapChain)
    {
        HRESULT hr;

        RECT rc;
        GetClientRect(m_hWnd, &rc);
        UINT width = rc.right - rc.left;
        UINT height = rc.bottom - rc.top;

        hr = m_pSwapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

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

        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        }

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

