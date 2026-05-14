#ifndef RENDER_CLASS_H
#define RENDER_CLASS_H

#include <dxgi.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include <iostream>
#include <string>
#include <vector>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include "Camera.h"
#include "ModelFactory.h"
#include "PointLight.h"
#include "Postprocessing.h"
#include "RenderTargetTexture.h"

using namespace DirectX;

class Render
{
public:
    Render() = default;
    Render(HWND hwnd);

    HRESULT Init(WCHAR szTitle[], WCHAR szWindowClass[]);
    void Terminate();

    HRESULT InitBufferShader();

    HRESULT InitGeometry(ModelFactory::ModelCode code);
    HRESULT InitCamera();
    HRESULT InitModel(ModelFactory::ModelCode code);


    HRESULT CompileShader(const std::wstring& path, ID3DBlob** pCodeShader=nullptr);

    void RenderStart();
    void Resize();
    void UpdateCamera(WPARAM wParam, LPARAM lParam);
    void HandleMouse(UINT message, LPARAM lParam);
    void SetMousePos(int x, int y) { m_mousePos.x = x; m_mousePos.y = y; }
    
    HWND GetHWND() const { return m_hWnd; }
    IDXGISwapChain* GetSwapChain() const { return m_pSwapChain; }
    ~Render() {Terminate();}

private:
    HRESULT ConfigureBackBuffer();
    void SetMVPBuffer();
    void SetModel(ModelFactory::ModelCode code);
    void SetDObjName(ID3D11DeviceChild* resource, const std::string& name);
    void InitLights();
    HRESULT InitScenBuffer();
    void RenderScene(float roughness, float metalness, const XMFLOAT3& albedo);
    void DrawSceneObjects();
    void RenderSkybox();
    void RenderLightMarkers();
    int GetDebugMode() const;
    bool IsTonemapEnabled() const;
    void DumpPipelineState(const char* reason);
    HRESULT InitTextureResources();
    HRESULT InitSkyResources();
    void RefreshSkyboxList();
    std::wstring GetSelectedSkyboxRelativePath() const;
    std::wstring GetSelectedSkyboxDisplayName() const;
    void ReleaseTextureResources();
    void ReleaseSkyResources();
    HRESULT LoadHDRTexture2D(const std::wstring& fullPath, ID3D11ShaderResourceView** outSRV);
    HRESULT ConvertHDRIToCubemap(ID3D11ShaderResourceView* equirectSRV, UINT cubeSize, ID3D11ShaderResourceView** outCubeSRV);
    HRESULT ConvolveCubemapToIrradiance(ID3D11ShaderResourceView* environmentCubeSRV, UINT irradianceSize, ID3D11ShaderResourceView** outIrradianceSRV);
    HRESULT RebuildIrradianceFromEnvironment();
    bool TryGetCachedHdriCubemap(const std::wstring& keyLower, ID3D11ShaderResourceView** outSRV) const;
    void PutCachedHdriCubemap(const std::wstring& keyLower, ID3D11ShaderResourceView* cubeSRV);
    std::wstring ResolveFirstExistingPath(const std::wstring& relativePath) const;
    std::wstring BuildConvertedSkyboxRelativePath(const std::wstring& hdriFileName) const;
    bool DoesRelativeFileExist(const std::wstring& relativePath) const;
    void InitImGui();
    void ShutdownImGui();
    void RenderImGui();

    enum DebugViewMode
    {
        DebugView_NDF = 0,
        DebugView_Geometry = 1,
        DebugView_Fresnel = 2,
        DebugView_Final = 3,
        DebugView_AlbedoOnly = 4,
        DebugView_LightMask = 5,
        DebugView_NormalWS = 6,
        DebugView_TBNSign = 7
    };

    HWND m_hWnd;
    ID3D11Device* m_pDevice; //
    ID3D11DeviceContext* m_pDeviceContext; //

    IDXGISwapChain* m_pSwapChain; //
    RenderTargetTexture* m_pRenderedSceneTexture; //
    RenderTargetTexture* m_pPostProcessedTexture; //

    ID3D11Buffer* m_pSceneCB = nullptr; //
    std::vector<std::unique_ptr<PointLight>> m_pointLights;
    int m_debugViewMode = DebugView_Final;
    bool m_gridMode = false;
    bool m_enableTextures = true;
    bool m_enableNormalMap = true;
    bool m_enableRoughnessMap = true;
    bool m_enableSkybox = true;
    bool m_enableDiffuseIBL = true;
    bool m_forceCopyPostprocess = false;
    bool m_labUiMode = false;
    bool m_extendViews = true;
    float m_normalStrength = 1.0f;
    float m_roughnessMapStrength = 1.0f;
    float m_iblIntensity = 1.0f;
    int m_irradianceMapSize = 32;
    int m_labAmbientSourceMode = 0; // 0 = Irradiance, 1 = Constant
    int m_materialPresetIndex = 1;
    int m_skyboxFileIndex = 0;
    bool m_requestDiagnosticsDump = false;
    float m_materialRoughness = 0.35f;
    float m_materialMetalness = 0.0f;
    XMFLOAT3 m_materialColor = { 0.90f, 0.75f, 0.65f };
    bool m_prevGridMode = false;
    float m_singleSphereScale = 1.0f;
    float m_singleSphereScaleTarget = 1.0f;
    int m_gridResolution = 7;
    float m_gridSpacing = 2.6f;
    float m_lightBrightness[3] = { 1.0f, 1.0f, 1.0f };
    float m_lightBaseIntensity[3] = { 120.0f, 90.0f, 70.0f };
    float m_lightRanges[3] = { 12.0f, 12.0f, 12.0f };
    XMFLOAT3 m_lightPositions[3] =
    {
        XMFLOAT3(-2.0f, 1.0f, -2.0f),
        XMFLOAT3(2.0f, 1.2f, -2.2f),
        XMFLOAT3(0.0f, 2.4f, 1.0f)
    };
    XMFLOAT3 m_lightColors[3] =
    {
        XMFLOAT3(1.0f, 0.0f, 0.0f),
        XMFLOAT3(0.0f, 0.0f, 1.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f)
    };

    ID3D11PixelShader* m_pPixelShader; //
    ID3D11PixelShader* m_pLightMarkerPixelShader = nullptr;
    ID3D11VertexShader* m_pVertexShader; //
    ID3D11InputLayout* m_pInputLayout; //
    ID3D11Buffer* m_pLightMarkerColorCB = nullptr;
    ID3D11SamplerState* m_pSamplerState = nullptr;
    ID3D11ShaderResourceView* m_pAlbedoTextureSRV = nullptr;
    ID3D11ShaderResourceView* m_pNormalTextureSRV = nullptr;
    ID3D11ShaderResourceView* m_pRoughnessTextureSRV = nullptr;

    ID3D11VertexShader* m_pSkyVertexShader = nullptr;
    ID3D11PixelShader* m_pSkyPixelShader = nullptr;
    ID3D11ShaderResourceView* m_pEnvironmentSRV = nullptr;
    ID3D11ShaderResourceView* m_pIrradianceSRV = nullptr;
    ID3D11RasterizerState* m_pSkyRasterState = nullptr;
    ID3D11DepthStencilState* m_pSkyDepthState = nullptr;
    ID3D11Texture2D* m_pSceneDepthTexture = nullptr;
    ID3D11DepthStencilView* m_pSceneDepthView = nullptr;

    enum class EnvironmentSourceKind
    {
        SkyboxDDS = 0,
        Hdri = 1
    };

    struct EnvironmentEntry
    {
        EnvironmentSourceKind SourceKind = EnvironmentSourceKind::SkyboxDDS;
        std::wstring DisplayName;
        std::wstring KeyLower;
        std::wstring SourceFileName;
        std::wstring SkyboxRelativePath;
        std::wstring HdriRelativePath;
        bool HasConvertedDDS = false;
        bool RuntimeConverted = false;
    };

    struct HdriCubemapCacheEntry
    {
        std::wstring KeyLower;
        ID3D11ShaderResourceView* CubemapSRV = nullptr;
    };

    std::vector<EnvironmentEntry> m_environmentEntries;
    std::vector<HdriCubemapCacheEntry> m_hdriCubemapCache;
    bool m_currentEnvironmentIsHdri = false;

    WCHAR* m_szTitle;
    WCHAR* m_szWindowClass;
    POINT m_mousePos = { 0, 0 };
    Camera* camera; //
    ModelManagerAbstract* m_currentModel; //
    ID3DUserDefinedAnnotation* m_pAnnotation = nullptr; //
    Postprocessing* m_PostProcessingPass; //
};
#endif
