#include "framework.h"
#include "GltfModel.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cwctype>
#include <limits>

#include "stb/stb_image.h"

using namespace DirectX;

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace
{
    static void DebugLogA(const char* fmt, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        OutputDebugStringA(buffer);
    }

    static void SetDebugName(ID3D11DeviceChild* child, const std::string& name)
    {
        if (child != nullptr)
            child->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.c_str());
    }

    static float Clamp01(float value)
    {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    static bool IsFinite3(const XMFLOAT3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    
    static bool IsFinite2(const XMFLOAT2& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y);
    }

    static std::wstring ToLowerWide(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return value;
    }
}

GltfModel::GltfModel(
    ID3D11DeviceContext* context,
    const std::wstring& scenePathRelative,
    const std::wstring& displayName)
    : ModelManagerAbstract(context)
    , m_scenePathRelative(scenePathRelative)
    , m_displayName(displayName)
{
}

GltfModel::~GltfModel()
{
    ReleaseAllResources();
}

std::string GltfModel::WideToUtf8(const std::wstring& value)
{
    if (value.empty())
        return std::string();

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return std::string();

    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &out[0], bytes, nullptr, nullptr);
    return out;
}

std::wstring GltfModel::ResolveScenePathAbsolute() const
{
    static const wchar_t* kPrefixes[] =
    {
        L"",
        L"..\\Lab6\\",
        L"..\\..\\Lab6\\",
        L"Lab6\\",
        L"..\\Lab5\\",
        L"..\\..\\Lab5\\",
        L"Lab5\\"
    };

    for (const wchar_t* prefix : kPrefixes)
    {
        const std::wstring candidate = std::wstring(prefix) + m_scenePathRelative;
        const DWORD attr = GetFileAttributesW(candidate.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return candidate;
    }

    return std::wstring();
}

HRESULT GltfModel::CreateTextureFromRGBA8(
    ID3D11Device* device,
    const unsigned char* rgba8,
    int width,
    int height,
    bool useSrgb,
    ID3D11ShaderResourceView** outSrv)
{
    if (device == nullptr || rgba8 == nullptr || width <= 0 || height <= 0 || outSrv == nullptr)
        return E_INVALIDARG;

    *outSrv = nullptr;

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = static_cast<UINT>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = useSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba8;
    init.SysMemPitch = static_cast<UINT>(width * 4);

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&texDesc, &init, &texture);
    if (FAILED(hr))
        return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = device->CreateShaderResourceView(texture, &srvDesc, outSrv);
    texture->Release();
    return hr;
}

bool GltfModel::LoadImageRGBA8(const std::wstring& absolutePath, std::vector<unsigned char>& outPixels, int& outWidth, int& outHeight) const
{
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;

    const std::string utf8 = WideToUtf8(absolutePath);
    if (utf8.empty())
        return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(utf8.c_str(), &width, &height, &channels, 4);
    if (data == nullptr || width <= 0 || height <= 0)
        return false;

    outPixels.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    stbi_image_free(data);

    outWidth = width;
    outHeight = height;
    return true;
}

bool GltfModel::BuildRoughnessTextureFromMetallicRoughness(
    const std::wstring& absolutePath,
    RoughnessPackResult& outResult) const
{
    outResult = {};

    std::vector<unsigned char> src;
    int width = 0;
    int height = 0;
    if (!LoadImageRGBA8(absolutePath, src, width, height))
        return false;

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    outResult.PackedPixels.resize(pixelCount * 4u);
    outResult.Width = width;
    outResult.Height = height;

    double metalAccum = 0.0;
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const unsigned char rough = src[i * 4u + 1u];
        const unsigned char metal = src[i * 4u + 2u];
        metalAccum += static_cast<double>(metal) / 255.0;

        outResult.PackedPixels[i * 4u + 0u] = rough;
        outResult.PackedPixels[i * 4u + 1u] = rough;
        outResult.PackedPixels[i * 4u + 2u] = rough;
        outResult.PackedPixels[i * 4u + 3u] = 255;
    }

    outResult.AverageMetalness = static_cast<float>(metalAccum / static_cast<double>(pixelCount));
    return true;
}

ID3D11ShaderResourceView* GltfModel::FindLoadedTexture(const std::wstring& pathKey, bool srgb) const
{
    for (size_t i = 0; i < m_loadedTextures.size(); ++i)
    {
        const LoadedTexture& item = m_loadedTextures[i];
        if (item.SRGB == srgb && item.PathKey == pathKey)
            return item.SRV;
    }
    return nullptr;
}

HRESULT GltfModel::LoadTextureCached(
    ID3D11Device* device,
    const std::wstring& absolutePath,
    bool srgb,
    ID3D11ShaderResourceView** outSrv)
{
    if (outSrv == nullptr)
        return E_INVALIDARG;
    *outSrv = nullptr;

    if (absolutePath.empty())
        return E_FAIL;

    const std::wstring key = ToLowerWide(absolutePath);
    ID3D11ShaderResourceView* cached = FindLoadedTexture(key, srgb);
    if (cached != nullptr)
    {
        *outSrv = cached;
        return S_OK;
    }

    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    if (!LoadImageRGBA8(absolutePath, pixels, width, height))
        return E_FAIL;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = CreateTextureFromRGBA8(device, pixels.data(), width, height, srgb, &srv);
    if (FAILED(hr) || srv == nullptr)
        return FAILED(hr) ? hr : E_FAIL;

    LoadedTexture loaded{};
    loaded.PathKey = key;
    loaded.SRGB = srgb;
    loaded.SRV = srv;
    m_loadedTextures.push_back(loaded);

    *outSrv = srv;
    return S_OK;
}

void GltfModel::ReleaseAllResources()
{
    for (size_t i = 0; i < m_primitives.size(); ++i)
    {
        if (m_primitives[i].VertexBuffer != nullptr)
        {
            m_primitives[i].VertexBuffer->Release();
            m_primitives[i].VertexBuffer = nullptr;
        }
        if (m_primitives[i].IndexBuffer != nullptr)
        {
            m_primitives[i].IndexBuffer->Release();
            m_primitives[i].IndexBuffer = nullptr;
        }
    }
    m_primitives.clear();

    for (size_t i = 0; i < m_loadedTextures.size(); ++i)
    {
        if (m_loadedTextures[i].SRV != nullptr)
        {
            m_loadedTextures[i].SRV->Release();
            m_loadedTextures[i].SRV = nullptr;
        }
    }
    m_loadedTextures.clear();

    for (size_t i = 0; i < m_ownedMaterialTextures.size(); ++i)
    {
        if (m_ownedMaterialTextures[i] != nullptr)
        {
            m_ownedMaterialTextures[i]->Release();
            m_ownedMaterialTextures[i] = nullptr;
        }
    }
    m_ownedMaterialTextures.clear();

    m_materials.clear();

    if (m_pModelBuffer != nullptr)
    {
        m_pModelBuffer->Release();
        m_pModelBuffer = nullptr;
    }

    m_initialized = false;
}

HRESULT GltfModel::InitModel(ID3D11Device* device)
{
    if (device == nullptr)
        return E_INVALIDARG;

    ReleaseAllResources();

    D3D11_BUFFER_DESC modelDesc{};
    modelDesc.Usage = D3D11_USAGE_DEFAULT;
    modelDesc.ByteWidth = sizeof(XMMATRIX);
    modelDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    HRESULT hr = device->CreateBuffer(&modelDesc, nullptr, &m_pModelBuffer);
    if (FAILED(hr))
        return hr;
    SetDebugName(m_pModelBuffer, "GLTF_ModelBuffer");

    const std::wstring scenePathAbs = ResolveScenePathAbsolute();
    if (scenePathAbs.empty())
    {
        OutputDebugStringW((L"[GLTF] Scene not found: " + m_scenePathRelative + L"\n").c_str());
        return E_FAIL;
    }
    DebugLogA("[GLTF] InitModel '%s' scene='%s'\n",
        WideToUtf8(m_displayName).c_str(),
        WideToUtf8(scenePathAbs).c_str());

    GltfCpuScene cpuScene{};
    std::string errorText;
    if (!LoadGltfCpuScene(scenePathAbs, cpuScene, &errorText))
    {
        std::wstring errorWide = L"[GLTF] Failed to load scene: " + m_scenePathRelative + L"\n";
        if (!errorText.empty())
        {
            errorWide += std::wstring(errorText.begin(), errorText.end());
            errorWide += L"\n";
        }
        OutputDebugStringW(errorWide.c_str());
        return E_FAIL;
    }
    DebugLogA("[GLTF] Parsed '%s': cpuMaterials=%llu cpuPrimitives=%llu cpuTextures=%llu\n",
        WideToUtf8(m_displayName).c_str(),
        static_cast<unsigned long long>(cpuScene.Materials.size()),
        static_cast<unsigned long long>(cpuScene.Primitives.size()),
        static_cast<unsigned long long>(cpuScene.Textures.size()));

    // Build material table.
    m_materials.resize(cpuScene.Materials.empty() ? 1u : cpuScene.Materials.size());
    if (cpuScene.Materials.empty())
    {
        m_materials[0] = MaterialInfo{};
    }
    else
    {
        for (size_t i = 0; i < cpuScene.Materials.size(); ++i)
        {
            const GltfCpuMaterial& srcMat = cpuScene.Materials[i];
            MaterialInfo dstMat{};
            dstMat.BaseColor = XMFLOAT3(
                Clamp01(srcMat.BaseColorFactor.x),
                Clamp01(srcMat.BaseColorFactor.y),
                Clamp01(srcMat.BaseColorFactor.z));
            dstMat.Roughness = Clamp01(srcMat.RoughnessFactor);
            dstMat.Metalness = Clamp01(srcMat.MetalnessFactor);

            if (srcMat.BaseColorTexture >= 0 && srcMat.BaseColorTexture < static_cast<int>(cpuScene.Textures.size()))
            {
                const std::wstring& path = cpuScene.Textures[srcMat.BaseColorTexture].AbsolutePath;
                ID3D11ShaderResourceView* albedoSrv = nullptr;
                if (SUCCEEDED(LoadTextureCached(device, path, true, &albedoSrv)))
                {
                    dstMat.AlbedoSRV = albedoSrv;
                }
                else
                {
                    DebugLogA("[GLTF] Material[%llu] albedo load failed: %s\n",
                        static_cast<unsigned long long>(i),
                        WideToUtf8(path).c_str());
                }
            }

            if (srcMat.NormalTexture >= 0 && srcMat.NormalTexture < static_cast<int>(cpuScene.Textures.size()))
            {
                const std::wstring& path = cpuScene.Textures[srcMat.NormalTexture].AbsolutePath;
                ID3D11ShaderResourceView* normalSrv = nullptr;
                if (SUCCEEDED(LoadTextureCached(device, path, false, &normalSrv)))
                {
                    dstMat.NormalSRV = normalSrv;
                }
                else
                {
                    DebugLogA("[GLTF] Material[%llu] normal load failed: %s\n",
                        static_cast<unsigned long long>(i),
                        WideToUtf8(path).c_str());
                }
            }

            if (srcMat.MetallicRoughnessTexture >= 0 && srcMat.MetallicRoughnessTexture < static_cast<int>(cpuScene.Textures.size()))
            {
                const std::wstring& path = cpuScene.Textures[srcMat.MetallicRoughnessTexture].AbsolutePath;
                RoughnessPackResult packed{};
                if (BuildRoughnessTextureFromMetallicRoughness(path, packed) && packed.IsValid())
                {
                    ID3D11ShaderResourceView* roughSrv = nullptr;
                    if (SUCCEEDED(CreateTextureFromRGBA8(device, packed.PackedPixels.data(), packed.Width, packed.Height, false, &roughSrv)) && roughSrv != nullptr)
                    {
                        m_ownedMaterialTextures.push_back(roughSrv);
                        dstMat.RoughnessSRV = roughSrv;
                        dstMat.Metalness = Clamp01(dstMat.Metalness * packed.AverageMetalness);
                    }
                }
                else
                {
                    DebugLogA("[GLTF] Material[%llu] metallicRoughness pack failed: %s\n",
                        static_cast<unsigned long long>(i),
                        WideToUtf8(path).c_str());
                }
            }

            m_materials[i] = dstMat;
            DebugLogA("[GLTF] Material[%llu] base=(%.3f %.3f %.3f) rough=%.3f metal=%.3f hasA=%d hasN=%d hasR=%d\n",
                static_cast<unsigned long long>(i),
                dstMat.BaseColor.x, dstMat.BaseColor.y, dstMat.BaseColor.z,
                dstMat.Roughness, dstMat.Metalness,
                (dstMat.AlbedoSRV != nullptr) ? 1 : 0,
                (dstMat.NormalSRV != nullptr) ? 1 : 0,
                (dstMat.RoughnessSRV != nullptr) ? 1 : 0);
        }
    }

    // Build primitive GPU buffers.
    m_primitives.reserve(cpuScene.Primitives.size());

    XMFLOAT3 boundsMin(
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)());
    XMFLOAT3 boundsMax(
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest());

    for (size_t i = 0; i < cpuScene.Primitives.size(); ++i)
    {
        const GltfCpuPrimitive& src = cpuScene.Primitives[i];
        if (src.Vertices.empty() || src.Indices.empty())
            continue;

        PrimitiveInfo primitive{};
        primitive.IndexCount = static_cast<UINT>(src.Indices.size());
        primitive.MaterialIndex = src.MaterialIndex;

        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.ByteWidth = static_cast<UINT>(sizeof(ModelManagerAbstract::Vertex) * src.Vertices.size());
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vbData{};
        vbData.pSysMem = src.Vertices.data();

        hr = device->CreateBuffer(&vbDesc, &vbData, &primitive.VertexBuffer);
        if (FAILED(hr))
            return hr;

        D3D11_BUFFER_DESC ibDesc{};
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        ibDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * src.Indices.size());
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA ibData{};
        ibData.pSysMem = src.Indices.data();

        hr = device->CreateBuffer(&ibDesc, &ibData, &primitive.IndexBuffer);
        if (FAILED(hr))
            return hr;

        SetDebugName(primitive.VertexBuffer, "GLTF_Primitive_VB");
        SetDebugName(primitive.IndexBuffer, "GLTF_Primitive_IB");

        size_t invalidPosCount = 0;
        size_t invalidNormalCount = 0;
        size_t invalidUvCount = 0;
        size_t tinyNormalCount = 0;
        for (size_t v = 0; v < src.Vertices.size(); ++v)
        {
            const ModelManagerAbstract::Vertex& vtx = src.Vertices[v];
            const XMFLOAT3& p = vtx.xyz;
            boundsMin.x = (p.x < boundsMin.x) ? p.x : boundsMin.x;
            boundsMin.y = (p.y < boundsMin.y) ? p.y : boundsMin.y;
            boundsMin.z = (p.z < boundsMin.z) ? p.z : boundsMin.z;
            boundsMax.x = (p.x > boundsMax.x) ? p.x : boundsMax.x;
            boundsMax.y = (p.y > boundsMax.y) ? p.y : boundsMax.y;
            boundsMax.z = (p.z > boundsMax.z) ? p.z : boundsMax.z;

            if (!IsFinite3(vtx.xyz)) ++invalidPosCount;
            if (!IsFinite3(vtx.normal)) ++invalidNormalCount;
            if (!IsFinite2(vtx.uv)) ++invalidUvCount;
            const float nLen2 = vtx.normal.x * vtx.normal.x + vtx.normal.y * vtx.normal.y + vtx.normal.z * vtx.normal.z;
            if (nLen2 < 1e-10f) ++tinyNormalCount;
        }

        if (invalidPosCount > 0 || invalidNormalCount > 0 || invalidUvCount > 0 || tinyNormalCount > 0)
        {
            DebugLogA(
                "[GLTF][WARN] Primitive[%llu] invalid/tiny attrs: pos=%llu normal=%llu uv=%llu tinyNormal=%llu\n",
                static_cast<unsigned long long>(i),
                static_cast<unsigned long long>(invalidPosCount),
                static_cast<unsigned long long>(invalidNormalCount),
                static_cast<unsigned long long>(invalidUvCount),
                static_cast<unsigned long long>(tinyNormalCount));
        }

        m_primitives.push_back(primitive);
    }

    if (m_primitives.empty())
        return E_FAIL;

    const XMFLOAT3 center(
        (boundsMin.x + boundsMax.x) * 0.5f,
        (boundsMin.y + boundsMax.y) * 0.5f,
        (boundsMin.z + boundsMax.z) * 0.5f);

    const float extentX = boundsMax.x - boundsMin.x;
    const float extentY = boundsMax.y - boundsMin.y;
    const float extentZ = boundsMax.z - boundsMin.z;
    float maxExtent = (std::max)(extentX, (std::max)(extentY, extentZ));
    if (maxExtent < 1e-4f)
        maxExtent = 1.0f;

    const float targetSize = 2.0f;
    const float scale = targetSize / maxExtent;
    m_normalizationMatrix =
        XMMatrixTranslation(-center.x, -center.y, -center.z) *
        XMMatrixScaling(scale, scale, scale);

    DebugLogA("[GLTF] '%s' gpuPrimitives=%llu boundsMin=(%.3f %.3f %.3f) boundsMax=(%.3f %.3f %.3f) maxExtent=%.3f normScale=%.3f\n",
        WideToUtf8(m_displayName).c_str(),
        static_cast<unsigned long long>(m_primitives.size()),
        boundsMin.x, boundsMin.y, boundsMin.z,
        boundsMax.x, boundsMax.y, boundsMax.z,
        maxExtent, scale);

    m_initialized = true;
    return S_OK;
}

void GltfModel::Update(float dt)
{
    UNREFERENCED_PARAMETER(dt);

    if (is_rotationable)
    {
        m_rotationAngle += 0.001f;
        if (m_rotationAngle > XM_2PI)
            m_rotationAngle -= XM_2PI;

        XMVECTOR scaleVec = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
        XMVECTOR rotQuat = XMQuaternionIdentity();
        XMVECTOR transVec = XMVectorZero();
        XMMATRIX model = XMMatrixRotationY(m_rotationAngle);
        if (XMMatrixDecompose(&scaleVec, &rotQuat, &transVec, m_modelMatrix))
        {
            model =
                XMMatrixScalingFromVector(scaleVec) *
                XMMatrixRotationY(m_rotationAngle) *
                XMMatrixTranslationFromVector(transVec);
        }
        m_modelMatrix = model;
    }

    if (m_context != nullptr && m_pModelBuffer != nullptr)
    {
        XMMATRIX modelT = XMMatrixTranspose(m_modelMatrix);
        m_context->UpdateSubresource(m_pModelBuffer, 0, nullptr, &modelT, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, &m_pModelBuffer);
    }
}

void GltfModel::Render()
{
    if (!m_initialized || m_context == nullptr)
        return;

    for (size_t i = 0; i < m_primitives.size(); ++i)
        DrawPrimitive(i);
}

bool GltfModel::GetPrimitiveState(size_t index, PrimitiveInfo& outPrimitive, MaterialInfo& outMaterial) const
{
    if (index >= m_primitives.size())
        return false;

    outPrimitive = m_primitives[index];
    if (outPrimitive.MaterialIndex >= 0 && outPrimitive.MaterialIndex < static_cast<int>(m_materials.size()))
    {
        outMaterial = m_materials[outPrimitive.MaterialIndex];
    }
    else
    {
        outMaterial = m_materials.empty() ? MaterialInfo{} : m_materials[0];
    }
    return true;
}

void GltfModel::DrawPrimitive(size_t index) const
{
    if (index >= m_primitives.size() || m_context == nullptr)
        return;

    const PrimitiveInfo& primitive = m_primitives[index];
    if (primitive.VertexBuffer == nullptr || primitive.IndexBuffer == nullptr || primitive.IndexCount == 0)
        return;

    UINT stride = sizeof(ModelManagerAbstract::Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, &primitive.VertexBuffer, &stride, &offset);
    m_context->IASetIndexBuffer(primitive.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->DrawIndexed(primitive.IndexCount, 0, 0);
}
