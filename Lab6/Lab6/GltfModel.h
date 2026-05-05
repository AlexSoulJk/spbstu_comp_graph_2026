#pragma once

#include <string>
#include <vector>

#include <DirectXMath.h>
#include <d3d11.h>

#include "GltfAssetLoader.h"
#include "ModelManagerAbstract.h"

class GltfModel : public ModelManagerAbstract
{
public:
    struct MaterialInfo
    {
        DirectX::XMFLOAT3 BaseColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
        float Roughness = 1.0f;
        float Metalness = 1.0f;
        ID3D11ShaderResourceView* AlbedoSRV = nullptr;
        ID3D11ShaderResourceView* NormalSRV = nullptr;
        ID3D11ShaderResourceView* RoughnessSRV = nullptr;
    };

    struct PrimitiveInfo
    {
        ID3D11Buffer* VertexBuffer = nullptr;
        ID3D11Buffer* IndexBuffer = nullptr;
        UINT IndexCount = 0;
        int MaterialIndex = -1;
    };

    GltfModel(
        ID3D11DeviceContext* context,
        const std::wstring& scenePathRelative,
        const std::wstring& displayName);
    ~GltfModel() override;

    HRESULT InitModel(ID3D11Device* device) override;
    void Update(float dt) override;
    void Render() override;

    size_t GetPrimitiveCount() const { return m_primitives.size(); }
    bool GetPrimitiveState(size_t index, PrimitiveInfo& outPrimitive, MaterialInfo& outMaterial) const;
    void DrawPrimitive(size_t index) const;

    DirectX::XMMATRIX GetNormalizationMatrix() const { return m_normalizationMatrix; }
    const std::wstring& GetDisplayName() const { return m_displayName; }

private:
    struct RoughnessPackResult
    {
        std::vector<unsigned char> PackedPixels;
        int Width = 0;
        int Height = 0;
        float AverageMetalness = 1.0f;
        bool IsValid() const { return Width > 0 && Height > 0 && !PackedPixels.empty(); }
    };

    struct LoadedTexture
    {
        std::wstring PathKey;
        bool SRGB = false;
        ID3D11ShaderResourceView* SRV = nullptr;
    };

    std::wstring ResolveScenePathAbsolute() const;
    static std::string WideToUtf8(const std::wstring& value);

    HRESULT CreateTextureFromRGBA8(
        ID3D11Device* device,
        const unsigned char* rgba8,
        int width,
        int height,
        bool useSrgb,
        ID3D11ShaderResourceView** outSrv);

    bool LoadImageRGBA8(const std::wstring& absolutePath, std::vector<unsigned char>& outPixels, int& outWidth, int& outHeight) const;
    bool BuildRoughnessTextureFromMetallicRoughness(
        const std::wstring& absolutePath,
        RoughnessPackResult& outResult) const;

    ID3D11ShaderResourceView* FindLoadedTexture(const std::wstring& pathKey, bool srgb) const;
    HRESULT LoadTextureCached(
        ID3D11Device* device,
        const std::wstring& absolutePath,
        bool srgb,
        ID3D11ShaderResourceView** outSrv);

    void ReleaseAllResources();

private:
    std::wstring m_scenePathRelative;
    std::wstring m_displayName;
    std::vector<PrimitiveInfo> m_primitives;
    std::vector<MaterialInfo> m_materials;
    std::vector<LoadedTexture> m_loadedTextures;
    std::vector<ID3D11ShaderResourceView*> m_ownedMaterialTextures;
    DirectX::XMMATRIX m_normalizationMatrix = DirectX::XMMatrixIdentity();
    bool m_initialized = false;
};

