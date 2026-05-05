#include "framework.h"
#include "GltfAssetLoader.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <string>
#include <vector>
#include <cstdio>

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_IMPLEMENTATION
#include "src/tiny_gltf.h"

using namespace DirectX;

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

    static std::string WideToUtf8(const std::wstring& value)
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

    static std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
            return std::wstring();

        const int chars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
        if (chars <= 0)
            return std::wstring();

        std::wstring out(static_cast<size_t>(chars), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &out[0], chars);
        return out;
    }

    static bool FileExists(const std::wstring& path)
    {
        const DWORD attr = GetFileAttributesW(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
    }

    static std::wstring GetDirectoryPart(const std::wstring& fullPath)
    {
        const size_t slash = fullPath.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return std::wstring();
        return fullPath.substr(0, slash);
    }

    static std::wstring JoinPath(const std::wstring& dir, std::wstring name)
    {
        std::replace(name.begin(), name.end(), L'/', L'\\');
        if (dir.empty())
            return name;
        if (name.empty())
            return dir;
        if (name.size() > 1 && name[1] == L':')
            return name;
        if (name[0] == L'\\')
            return name;
        return dir + L"\\" + name;
    }

    static std::wstring ToLowerWide(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return value;
    }

    static std::wstring GetExtensionLower(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return std::wstring();
        return ToLowerWide(path.substr(dot));
    }

    static bool ReadVec3Accessor(
        const tinygltf::Model& model,
        int accessorIndex,
        std::vector<XMFLOAT3>& out)
    {
        if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size()))
            return false;

        const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
            return false;

        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3)
            return false;

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
            return false;

        const tinygltf::Buffer& buffer = model.buffers[view.buffer];
        const size_t stride = accessor.ByteStride(view) == 0 ? sizeof(float) * 3u : static_cast<size_t>(accessor.ByteStride(view));
        const size_t start = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset);
        if (start >= buffer.data.size())
            return false;

        out.resize(accessor.count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const size_t offset = start + i * stride;
            if (offset + sizeof(float) * 3u > buffer.data.size())
                return false;
            const float* src = reinterpret_cast<const float*>(buffer.data.data() + offset);
            out[i] = XMFLOAT3(src[0], src[1], src[2]);
        }
        return true;
    }

    static bool ReadVec2Accessor(
        const tinygltf::Model& model,
        int accessorIndex,
        std::vector<XMFLOAT2>& out)
    {
        if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size()))
            return false;

        const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
            return false;

        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC2)
            return false;

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
            return false;

        const tinygltf::Buffer& buffer = model.buffers[view.buffer];
        const size_t stride = accessor.ByteStride(view) == 0 ? sizeof(float) * 2u : static_cast<size_t>(accessor.ByteStride(view));
        const size_t start = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset);
        if (start >= buffer.data.size())
            return false;

        out.resize(accessor.count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const size_t offset = start + i * stride;
            if (offset + sizeof(float) * 2u > buffer.data.size())
                return false;
            const float* src = reinterpret_cast<const float*>(buffer.data.data() + offset);
            out[i] = XMFLOAT2(src[0], src[1]);
        }
        return true;
    }

    static bool ReadVec4Accessor(
        const tinygltf::Model& model,
        int accessorIndex,
        std::vector<XMFLOAT4>& out)
    {
        if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size()))
            return false;

        const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
            return false;

        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC4)
            return false;

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
            return false;

        const tinygltf::Buffer& buffer = model.buffers[view.buffer];
        const size_t stride = accessor.ByteStride(view) == 0 ? sizeof(float) * 4u : static_cast<size_t>(accessor.ByteStride(view));
        const size_t start = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset);
        if (start >= buffer.data.size())
            return false;

        out.resize(accessor.count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const size_t offset = start + i * stride;
            if (offset + sizeof(float) * 4u > buffer.data.size())
                return false;
            const float* src = reinterpret_cast<const float*>(buffer.data.data() + offset);
            out[i] = XMFLOAT4(src[0], src[1], src[2], src[3]);
        }
        return true;
    }

    static bool ReadIndexAccessor(
        const tinygltf::Model& model,
        int accessorIndex,
        std::vector<uint32_t>& out)
    {
        if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size()))
            return false;

        const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
            return false;

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
            return false;

        const tinygltf::Buffer& buffer = model.buffers[view.buffer];
        const size_t start = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset);
        if (start >= buffer.data.size())
            return false;

        out.resize(accessor.count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            switch (accessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                const size_t offset = start + i * sizeof(uint8_t);
                if (offset + sizeof(uint8_t) > buffer.data.size())
                    return false;
                out[i] = static_cast<uint32_t>(*(reinterpret_cast<const uint8_t*>(buffer.data.data() + offset)));
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                const size_t offset = start + i * sizeof(uint16_t);
                if (offset + sizeof(uint16_t) > buffer.data.size())
                    return false;
                out[i] = static_cast<uint32_t>(*(reinterpret_cast<const uint16_t*>(buffer.data.data() + offset)));
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                const size_t offset = start + i * sizeof(uint32_t);
                if (offset + sizeof(uint32_t) > buffer.data.size())
                    return false;
                out[i] = *(reinterpret_cast<const uint32_t*>(buffer.data.data() + offset));
                break;
            }
            default:
                return false;
            }
        }
        return true;
    }

    static void BuildNormalsIfMissing(
        std::vector<ModelManagerAbstract::Vertex>& vertices,
        const std::vector<uint32_t>& indices)
    {
        for (size_t i = 0; i < vertices.size(); ++i)
            vertices[i].normal = XMFLOAT3(0.0f, 0.0f, 0.0f);

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t i0 = indices[i + 0];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                continue;

            const XMVECTOR p0 = XMLoadFloat3(&vertices[i0].xyz);
            const XMVECTOR p1 = XMLoadFloat3(&vertices[i1].xyz);
            const XMVECTOR p2 = XMLoadFloat3(&vertices[i2].xyz);

            const XMVECTOR e1 = XMVectorSubtract(p1, p0);
            const XMVECTOR e2 = XMVectorSubtract(p2, p0);
            const XMVECTOR n = XMVector3Cross(e1, e2);

            XMVECTOR n0 = XMLoadFloat3(&vertices[i0].normal);
            XMVECTOR n1 = XMLoadFloat3(&vertices[i1].normal);
            XMVECTOR n2 = XMLoadFloat3(&vertices[i2].normal);
            n0 = XMVectorAdd(n0, n);
            n1 = XMVectorAdd(n1, n);
            n2 = XMVectorAdd(n2, n);
            XMStoreFloat3(&vertices[i0].normal, n0);
            XMStoreFloat3(&vertices[i1].normal, n1);
            XMStoreFloat3(&vertices[i2].normal, n2);
        }

        for (size_t i = 0; i < vertices.size(); ++i)
        {
            XMVECTOR n = XMLoadFloat3(&vertices[i].normal);
            if (XMVectorGetX(XMVector3LengthSq(n)) < 1e-12f)
                n = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            n = XMVector3Normalize(n);
            XMStoreFloat3(&vertices[i].normal, n);
        }
    }

    static void BuildTangentsIfMissing(
        std::vector<ModelManagerAbstract::Vertex>& vertices,
        const std::vector<uint32_t>& indices)
    {
        std::vector<XMVECTOR> tan1(vertices.size(), XMVectorZero());
        std::vector<XMVECTOR> tan2(vertices.size(), XMVectorZero());

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t i0 = indices[i + 0];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                continue;

            const XMFLOAT3& p0 = vertices[i0].xyz;
            const XMFLOAT3& p1 = vertices[i1].xyz;
            const XMFLOAT3& p2 = vertices[i2].xyz;
            const XMFLOAT2& uv0 = vertices[i0].uv;
            const XMFLOAT2& uv1 = vertices[i1].uv;
            const XMFLOAT2& uv2 = vertices[i2].uv;

            const float x1 = p1.x - p0.x;
            const float y1 = p1.y - p0.y;
            const float z1 = p1.z - p0.z;
            const float x2 = p2.x - p0.x;
            const float y2 = p2.y - p0.y;
            const float z2 = p2.z - p0.z;

            const float s1 = uv1.x - uv0.x;
            const float t1 = uv1.y - uv0.y;
            const float s2 = uv2.x - uv0.x;
            const float t2 = uv2.y - uv0.y;

            const float denom = s1 * t2 - s2 * t1;
            if (fabsf(denom) < 1e-8f)
                continue;

            const float r = 1.0f / denom;
            const XMVECTOR sdir = XMVectorSet(
                (t2 * x1 - t1 * x2) * r,
                (t2 * y1 - t1 * y2) * r,
                (t2 * z1 - t1 * z2) * r,
                0.0f);
            const XMVECTOR tdir = XMVectorSet(
                (s1 * x2 - s2 * x1) * r,
                (s1 * y2 - s2 * y1) * r,
                (s1 * z2 - s2 * z1) * r,
                0.0f);

            tan1[i0] = XMVectorAdd(tan1[i0], sdir);
            tan1[i1] = XMVectorAdd(tan1[i1], sdir);
            tan1[i2] = XMVectorAdd(tan1[i2], sdir);

            tan2[i0] = XMVectorAdd(tan2[i0], tdir);
            tan2[i1] = XMVectorAdd(tan2[i1], tdir);
            tan2[i2] = XMVectorAdd(tan2[i2], tdir);
        }

        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const XMVECTOR n = XMLoadFloat3(&vertices[i].normal);
            XMVECTOR t = tan1[i];

            t = XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t))));
            if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-8f)
            {
                t = (fabsf(vertices[i].normal.z) > 0.999f)
                    ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
                    : XMVector3Normalize(XMVector3Cross(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), n));
            }
            else
            {
                t = XMVector3Normalize(t);
            }

            XMVECTOR b = XMVector3Cross(n, t);
            const float handedness = (XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), tan2[i])) < 0.0f) ? -1.0f : 1.0f;
            b = XMVectorScale(b, handedness);
            b = XMVector3Normalize(b);

            XMStoreFloat3(&vertices[i].tangent, t);
            XMStoreFloat3(&vertices[i].bitangent, b);
        }
    }

    static XMFLOAT4X4 BuildNodeLocalMatrix(const tinygltf::Node& node)
    {
        XMMATRIX local = XMMatrixIdentity();
        if (node.matrix.size() == 16)
        {
            // glTF matrices are provided as 16 floats in column-major memory order.
            // DirectX row-major matrices with row-vectors use the same linear memory
            // order for affine transforms (translation at indices 12..14), so we can
            // map the array directly without reordering.
            local = XMMATRIX(
                static_cast<float>(node.matrix[0]),  static_cast<float>(node.matrix[1]),  static_cast<float>(node.matrix[2]),  static_cast<float>(node.matrix[3]),
                static_cast<float>(node.matrix[4]),  static_cast<float>(node.matrix[5]),  static_cast<float>(node.matrix[6]),  static_cast<float>(node.matrix[7]),
                static_cast<float>(node.matrix[8]),  static_cast<float>(node.matrix[9]),  static_cast<float>(node.matrix[10]), static_cast<float>(node.matrix[11]),
                static_cast<float>(node.matrix[12]), static_cast<float>(node.matrix[13]), static_cast<float>(node.matrix[14]), static_cast<float>(node.matrix[15]));
        }
        else
        {
            XMVECTOR t = XMVectorZero();
            XMVECTOR r = XMQuaternionIdentity();
            XMVECTOR s = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);

            if (node.translation.size() == 3)
            {
                t = XMVectorSet(
                    static_cast<float>(node.translation[0]),
                    static_cast<float>(node.translation[1]),
                    static_cast<float>(node.translation[2]),
                    1.0f);
            }
            if (node.rotation.size() == 4)
            {
                r = XMVectorSet(
                    static_cast<float>(node.rotation[0]),
                    static_cast<float>(node.rotation[1]),
                    static_cast<float>(node.rotation[2]),
                    static_cast<float>(node.rotation[3]));
            }
            if (node.scale.size() == 3)
            {
                s = XMVectorSet(
                    static_cast<float>(node.scale[0]),
                    static_cast<float>(node.scale[1]),
                    static_cast<float>(node.scale[2]),
                    0.0f);
            }

            local = XMMatrixScalingFromVector(s) * XMMatrixRotationQuaternion(r) * XMMatrixTranslationFromVector(t);
        }

        XMFLOAT4X4 result;
        XMStoreFloat4x4(&result, local);
        return result;
    }

    struct NodeState
    {
        int MeshIndex = -1;
        XMFLOAT4X4 Local = {};
        std::vector<int> Children;
    };

    static void AppendNodePrimitives(
        const std::vector<NodeState>& nodes,
        int nodeIndex,
        const XMMATRIX& parentWorld,
        const std::vector<std::vector<GltfCpuPrimitive>>& meshPrimitives,
        GltfCpuScene& outScene)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            return;

        const NodeState& node = nodes[nodeIndex];
        const XMMATRIX local = XMLoadFloat4x4(&node.Local);
        const XMMATRIX world = local * parentWorld;
        const XMMATRIX normalWorld = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        if (node.MeshIndex >= 0 && node.MeshIndex < static_cast<int>(meshPrimitives.size()))
        {
            const std::vector<GltfCpuPrimitive>& primitives = meshPrimitives[node.MeshIndex];
            for (size_t p = 0; p < primitives.size(); ++p)
            {
                GltfCpuPrimitive transformed = primitives[p];
                for (size_t v = 0; v < transformed.Vertices.size(); ++v)
                {
                    ModelManagerAbstract::Vertex& vertex = transformed.Vertices[v];
                    XMVECTOR p4 = XMVectorSet(vertex.xyz.x, vertex.xyz.y, vertex.xyz.z, 1.0f);
                    XMVECTOR n4 = XMVectorSet(vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f);
                    XMVECTOR t4 = XMVectorSet(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, 0.0f);
                    XMVECTOR b4 = XMVectorSet(vertex.bitangent.x, vertex.bitangent.y, vertex.bitangent.z, 0.0f);

                    p4 = XMVector3TransformCoord(p4, world);
                    n4 = XMVector3Normalize(XMVector3TransformNormal(n4, normalWorld));
                    t4 = XMVector3Normalize(XMVector3TransformNormal(t4, normalWorld));
                    b4 = XMVector3Normalize(XMVector3TransformNormal(b4, normalWorld));

                    XMStoreFloat3(&vertex.xyz, p4);
                    XMStoreFloat3(&vertex.normal, n4);
                    XMStoreFloat3(&vertex.tangent, t4);
                    XMStoreFloat3(&vertex.bitangent, b4);
                }

                // Robust normal orientation for nodes with mirrored / odd transforms:
                // compare geometric face normal and averaged vertex normals,
                // then flip TBN if they disagree.
                if (transformed.Indices.size() >= 3)
                {
                    const uint32_t i0 = transformed.Indices[0];
                    const uint32_t i1 = transformed.Indices[1];
                    const uint32_t i2 = transformed.Indices[2];
                    if (i0 < transformed.Vertices.size() &&
                        i1 < transformed.Vertices.size() &&
                        i2 < transformed.Vertices.size())
                    {
                        const XMFLOAT3 p0 = transformed.Vertices[i0].xyz;
                        const XMFLOAT3 p1 = transformed.Vertices[i1].xyz;
                        const XMFLOAT3 p2 = transformed.Vertices[i2].xyz;
                        const XMVECTOR e1 = XMVectorSubtract(XMLoadFloat3(&p1), XMLoadFloat3(&p0));
                        const XMVECTOR e2 = XMVectorSubtract(XMLoadFloat3(&p2), XMLoadFloat3(&p0));
                        XMVECTOR faceN = XMVector3Cross(e1, e2);
                        if (XMVectorGetX(XMVector3LengthSq(faceN)) > 1e-12f)
                        {
                            faceN = XMVector3Normalize(faceN);
                            XMVECTOR avgN = XMVectorZero();
                            avgN = XMVectorAdd(avgN, XMLoadFloat3(&transformed.Vertices[i0].normal));
                            avgN = XMVectorAdd(avgN, XMLoadFloat3(&transformed.Vertices[i1].normal));
                            avgN = XMVectorAdd(avgN, XMLoadFloat3(&transformed.Vertices[i2].normal));
                            if (XMVectorGetX(XMVector3LengthSq(avgN)) > 1e-12f)
                            {
                                avgN = XMVector3Normalize(avgN);
                                const float nd = XMVectorGetX(XMVector3Dot(faceN, avgN));
                                if (nd < 0.0f)
                                {
                                    for (size_t k = 0; k < transformed.Vertices.size(); ++k)
                                    {
                                        transformed.Vertices[k].normal.x = -transformed.Vertices[k].normal.x;
                                        transformed.Vertices[k].normal.y = -transformed.Vertices[k].normal.y;
                                        transformed.Vertices[k].normal.z = -transformed.Vertices[k].normal.z;
                                        transformed.Vertices[k].tangent.x = -transformed.Vertices[k].tangent.x;
                                        transformed.Vertices[k].tangent.y = -transformed.Vertices[k].tangent.y;
                                        transformed.Vertices[k].tangent.z = -transformed.Vertices[k].tangent.z;
                                        transformed.Vertices[k].bitangent.x = -transformed.Vertices[k].bitangent.x;
                                        transformed.Vertices[k].bitangent.y = -transformed.Vertices[k].bitangent.y;
                                        transformed.Vertices[k].bitangent.z = -transformed.Vertices[k].bitangent.z;
                                    }
                                    DebugLogA("[GLTF] node %d primitive %llu: flipped TBN orientation (faceN·avgN=%.4f)\n",
                                        nodeIndex,
                                        static_cast<unsigned long long>(p),
                                        nd);
                                }
                            }
                        }
                    }
                }
                outScene.Primitives.push_back(std::move(transformed));
            }
        }

        for (size_t i = 0; i < node.Children.size(); ++i)
            AppendNodePrimitives(nodes, node.Children[i], world, meshPrimitives, outScene);
    }

    static void PushError(std::string* outErrorMessage, const char* message)
    {
        if (outErrorMessage == nullptr)
            return;
        if (!outErrorMessage->empty())
            *outErrorMessage += "\n";
        *outErrorMessage += message;
    }
}

bool LoadGltfCpuScene(
    const std::wstring& gltfAbsolutePath,
    GltfCpuScene& outScene,
    std::string* outErrorMessage)
{
    outScene = {};
    if (outErrorMessage != nullptr)
        outErrorMessage->clear();

    if (!FileExists(gltfAbsolutePath))
    {
        PushError(outErrorMessage, "GLTF file does not exist.");
        return false;
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warn;
    std::string err;

    const std::string fileUtf8 = WideToUtf8(gltfAbsolutePath);
    if (fileUtf8.empty())
    {
        PushError(outErrorMessage, "Failed to convert GLTF path to UTF-8.");
        return false;
    }

    bool ok = false;
    const std::wstring extensionLower = GetExtensionLower(gltfAbsolutePath);

    if (extensionLower == L".glb")
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, fileUtf8);
    else
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, fileUtf8);

    if (!warn.empty())
        OutputDebugStringA(("[GLTF][WARN] " + warn + "\n").c_str());
    if (!err.empty())
    {
        OutputDebugStringA(("[GLTF][ERROR] " + err + "\n").c_str());
        PushError(outErrorMessage, err.c_str());
    }
    if (!ok)
    {
        PushError(outErrorMessage, "tinygltf failed to parse the scene.");
        return false;
    }

    // Collect texture URIs.
    const std::wstring baseDir = GetDirectoryPart(gltfAbsolutePath);
    outScene.Textures.reserve(model.textures.size());
    for (size_t textureIndex = 0; textureIndex < model.textures.size(); ++textureIndex)
    {
        GltfCpuTexture textureEntry{};
        const tinygltf::Texture& tex = model.textures[textureIndex];
        if (tex.source >= 0 && tex.source < static_cast<int>(model.images.size()))
        {
            const tinygltf::Image& image = model.images[tex.source];
            if (!image.uri.empty())
            {
                textureEntry.AbsolutePath = JoinPath(baseDir, Utf8ToWide(image.uri));
            }
        }
        outScene.Textures.push_back(textureEntry);
    }

    // Collect materials.
    outScene.Materials.reserve(model.materials.size());
    for (size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex)
    {
        const tinygltf::Material& src = model.materials[materialIndex];
        GltfCpuMaterial dst{};

        if (src.values.find("baseColorFactor") != src.values.end())
        {
            const tinygltf::ColorValue c = src.values.at("baseColorFactor").ColorFactor();
            if (c.size() == 4)
            {
                dst.BaseColorFactor = XMFLOAT4(
                    static_cast<float>(c[0]),
                    static_cast<float>(c[1]),
                    static_cast<float>(c[2]),
                    static_cast<float>(c[3]));
            }
        }
        if (src.values.find("roughnessFactor") != src.values.end())
            dst.RoughnessFactor = static_cast<float>(src.values.at("roughnessFactor").Factor());
        if (src.values.find("metallicFactor") != src.values.end())
            dst.MetalnessFactor = static_cast<float>(src.values.at("metallicFactor").Factor());
        if (src.values.find("baseColorTexture") != src.values.end())
            dst.BaseColorTexture = src.values.at("baseColorTexture").TextureIndex();
        if (src.values.find("metallicRoughnessTexture") != src.values.end())
            dst.MetallicRoughnessTexture = src.values.at("metallicRoughnessTexture").TextureIndex();
        if (src.additionalValues.find("normalTexture") != src.additionalValues.end())
            dst.NormalTexture = src.additionalValues.at("normalTexture").TextureIndex();

        outScene.Materials.push_back(dst);
    }

    // Build mesh primitives in object-local space.
    std::vector<std::vector<GltfCpuPrimitive>> meshPrimitives;
    meshPrimitives.resize(model.meshes.size());
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
    {
        const tinygltf::Mesh& mesh = model.meshes[meshIndex];
        std::vector<GltfCpuPrimitive>& dstPrimitives = meshPrimitives[meshIndex];
        dstPrimitives.reserve(mesh.primitives.size());

        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            const tinygltf::Primitive& primitive = mesh.primitives[primitiveIndex];
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            auto attrPos = primitive.attributes.find("POSITION");
            if (attrPos == primitive.attributes.end())
                continue;

            std::vector<XMFLOAT3> positions;
            std::vector<XMFLOAT3> normals;
            std::vector<XMFLOAT2> uvs;
            std::vector<XMFLOAT4> tangents4;
            std::vector<uint32_t> indices;

            if (!ReadVec3Accessor(model, attrPos->second, positions))
                continue;

            auto attrNormal = primitive.attributes.find("NORMAL");
            if (attrNormal != primitive.attributes.end())
                ReadVec3Accessor(model, attrNormal->second, normals);

            auto attrUv = primitive.attributes.find("TEXCOORD_0");
            if (attrUv != primitive.attributes.end())
                ReadVec2Accessor(model, attrUv->second, uvs);

            auto attrTangent = primitive.attributes.find("TANGENT");
            if (attrTangent != primitive.attributes.end())
                ReadVec4Accessor(model, attrTangent->second, tangents4);

            if (primitive.indices >= 0)
            {
                if (!ReadIndexAccessor(model, primitive.indices, indices))
                    continue;
            }
            else
            {
                indices.resize(positions.size());
                for (size_t i = 0; i < positions.size(); ++i)
                    indices[i] = static_cast<uint32_t>(i);
            }

            if (indices.size() < 3 || positions.empty())
                continue;

            GltfCpuPrimitive dst{};
            dst.MaterialIndex = primitive.material;
            dst.Vertices.resize(positions.size());
            dst.Indices = indices;

            for (size_t v = 0; v < positions.size(); ++v)
            {
                ModelManagerAbstract::Vertex vertex{};
                vertex.xyz = positions[v];
                vertex.normal = (v < normals.size()) ? normals[v] : XMFLOAT3(0.0f, 0.0f, 0.0f);
                // glTF UV convention is already aligned for this D3D pipeline.
                // Do not flip V here, otherwise albedo/normal/roughness mappings diverge.
                vertex.uv = (v < uvs.size()) ? XMFLOAT2(uvs[v].x, uvs[v].y) : XMFLOAT2(0.0f, 0.0f);
                if (v < tangents4.size())
                {
                    const XMFLOAT4 tangent = tangents4[v];
                    XMFLOAT3 t(tangent.x, tangent.y, tangent.z);
                    XMVECTOR nVec = XMLoadFloat3(&vertex.normal);
                    XMVECTOR tVec = XMLoadFloat3(&t);
                    tVec = XMVectorSubtract(tVec, XMVectorScale(nVec, XMVectorGetX(XMVector3Dot(nVec, tVec))));
                    if (XMVectorGetX(XMVector3LengthSq(tVec)) < 1e-8f)
                        tVec = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
                    tVec = XMVector3Normalize(tVec);
                    XMStoreFloat3(&vertex.tangent, tVec);

                    XMVECTOR bVec = XMVector3Cross(nVec, tVec);
                    bVec = XMVectorScale(bVec, tangent.w < 0.0f ? -1.0f : 1.0f);
                    bVec = XMVector3Normalize(bVec);
                    XMStoreFloat3(&vertex.bitangent, bVec);
                }
                else
                {
                    vertex.tangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
                    vertex.bitangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
                }
                dst.Vertices[v] = vertex;
            }

            const bool hasNormals = !normals.empty();
            const bool hasTangents = !tangents4.empty();
            if (!hasNormals)
                BuildNormalsIfMissing(dst.Vertices, dst.Indices);
            if (!hasTangents)
                BuildTangentsIfMissing(dst.Vertices, dst.Indices);

            dstPrimitives.push_back(std::move(dst));
        }
    }

    // Build node tree.
    std::vector<NodeState> nodes;
    nodes.resize(model.nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex)
    {
        const tinygltf::Node& srcNode = model.nodes[nodeIndex];
        NodeState& dstNode = nodes[nodeIndex];
        dstNode.MeshIndex = srcNode.mesh;
        dstNode.Local = BuildNodeLocalMatrix(srcNode);
        dstNode.Children.reserve(srcNode.children.size());
        for (size_t i = 0; i < srcNode.children.size(); ++i)
            dstNode.Children.push_back(srcNode.children[i]);
    }

    // Expand into transformed draw primitives.
    if (model.defaultScene >= 0 && model.defaultScene < static_cast<int>(model.scenes.size()))
    {
        const tinygltf::Scene& scene = model.scenes[model.defaultScene];
        for (size_t i = 0; i < scene.nodes.size(); ++i)
            AppendNodePrimitives(nodes, scene.nodes[i], XMMatrixIdentity(), meshPrimitives, outScene);
    }
    else
    {
        std::vector<bool> isChild(nodes.size(), false);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            for (size_t c = 0; c < nodes[i].Children.size(); ++c)
            {
                const int childIndex = nodes[i].Children[c];
                if (childIndex >= 0 && childIndex < static_cast<int>(nodes.size()))
                    isChild[childIndex] = true;
            }
        }

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (!isChild[i])
                AppendNodePrimitives(nodes, static_cast<int>(i), XMMatrixIdentity(), meshPrimitives, outScene);
        }
    }

    if (outScene.Primitives.empty())
    {
        PushError(outErrorMessage, "No drawable triangle primitives found in GLTF scene.");
        return false;
    }

    return true;
}
