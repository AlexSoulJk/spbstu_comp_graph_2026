#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "ModelManagerAbstract.h"

struct GltfCpuMaterial
{
    DirectX::XMFLOAT4 BaseColorFactor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    float RoughnessFactor = 1.0f;
    float MetalnessFactor = 1.0f;
    int BaseColorTexture = -1;
    int MetallicRoughnessTexture = -1;
    int NormalTexture = -1;
};

struct GltfCpuTexture
{
    std::wstring AbsolutePath;
};

struct GltfCpuPrimitive
{
    std::vector<ModelManagerAbstract::Vertex> Vertices;
    std::vector<uint32_t> Indices;
    int MaterialIndex = -1;
};

struct GltfCpuScene
{
    std::vector<GltfCpuTexture> Textures;
    std::vector<GltfCpuMaterial> Materials;
    std::vector<GltfCpuPrimitive> Primitives;
};

bool LoadGltfCpuScene(
    const std::wstring& gltfAbsolutePath,
    GltfCpuScene& outScene,
    std::string* outErrorMessage = nullptr);

