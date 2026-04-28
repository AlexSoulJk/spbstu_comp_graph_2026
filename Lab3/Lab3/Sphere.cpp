#include "framework.h"
#include "Sphere.h"

#include <vector>
#include <cstdint>

static void SetDebugName(ID3D11DeviceChild* child, const std::string& name) {
    if (child) {
        child->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
    }
}

void Sphere::Update(float dt)
{
    UNREFERENCED_PARAMETER(dt);

    if (is_rotationable)
    {
        m_sphereAngle += 0.001f;
        if (m_sphereAngle > XM_2PI) m_sphereAngle -= XM_2PI;

        XMVECTOR scaleVec = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
        XMVECTOR rotQuat = XMQuaternionIdentity();
        XMVECTOR transVec = XMVectorZero();
        XMMATRIX model = XMMatrixRotationY(m_sphereAngle);
        if (XMMatrixDecompose(&scaleVec, &rotQuat, &transVec, m_modelMatrix))
        {
            model =
                XMMatrixScalingFromVector(scaleVec) *
                XMMatrixRotationY(m_sphereAngle) *
                XMMatrixTranslationFromVector(transVec);
        }

        m_modelMatrix = model;
        XMMATRIX modelT = XMMatrixTranspose(m_modelMatrix);
        m_context->UpdateSubresource(m_pModelBuffer, 0, nullptr, &modelT, 0, 0);
    }

    m_context->VSSetConstantBuffers(0, 1, &m_pModelBuffer);
}

void Sphere::Render()
{
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &stride, &offset);
    m_context->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->DrawIndexed(m_indexCount, 0, 0);
}

HRESULT Sphere::InitModel(ID3D11Device* device)
{
    const uint32_t stacks = 32;
    const uint32_t slices = 32;
    const float radius = 1.0f;

    std::vector<Vertex> vertices;
    vertices.reserve((stacks + 1) * (slices + 1));

    for (uint32_t stack = 0; stack <= stacks; ++stack)
    {
        const float v = static_cast<float>(stack) / static_cast<float>(stacks);
        const float phi = XM_PI * v;
        const float sinPhi = sinf(phi);
        const float cosPhi = cosf(phi);

        for (uint32_t slice = 0; slice <= slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = XM_2PI * u;
            const float sinTheta = sinf(theta);
            const float cosTheta = cosf(theta);

            const float x = radius * sinPhi * cosTheta;
            const float y = radius * cosPhi;
            const float z = radius * sinPhi * sinTheta;

            Vertex vertex{};
            vertex.xyz = { x, y, z };
            vertex.normal = { x / radius, y / radius, z / radius };
            // Match reference UV orientation for DDS textures.
            vertex.uv = { u, 1.0f - v };
            vertices.push_back(vertex);
        }
    }

    std::vector<uint16_t> indices;
    indices.reserve(stacks * slices * 6);

    const uint32_t ring = slices + 1;
    for (uint32_t stack = 0; stack < stacks; ++stack)
    {
        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            const uint16_t i0 = static_cast<uint16_t>(stack * ring + slice);
            const uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            const uint16_t i2 = static_cast<uint16_t>((stack + 1) * ring + slice);
            const uint16_t i3 = static_cast<uint16_t>(i2 + 1);

            // Keep triangle winding aligned with reference implementation.
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i1);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }

    m_indexCount = static_cast<unsigned int>(indices.size());

    D3D11_BUFFER_DESC vertexDesc{};
    vertexDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexData{};
    vertexData.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&vertexDesc, &vertexData, &m_pVertexBuffer);
    if (FAILED(hr))
        return hr;
    SetDebugName(m_pVertexBuffer, "Sphere_VertexBuffer");

    D3D11_BUFFER_DESC indexDesc{};
    indexDesc.Usage = D3D11_USAGE_DEFAULT;
    indexDesc.ByteWidth = static_cast<UINT>(sizeof(uint16_t) * indices.size());
    indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA indexData{};
    indexData.pSysMem = indices.data();

    hr = device->CreateBuffer(&indexDesc, &indexData, &m_pIndexBuffer);
    if (FAILED(hr))
        return hr;
    SetDebugName(m_pIndexBuffer, "Sphere_IndexBuffer");

    D3D11_BUFFER_DESC modelDesc{};
    modelDesc.Usage = D3D11_USAGE_DEFAULT;
    modelDesc.ByteWidth = sizeof(XMMATRIX);
    modelDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = device->CreateBuffer(&modelDesc, nullptr, &m_pModelBuffer);
    if (FAILED(hr))
        return hr;
    SetDebugName(m_pModelBuffer, "Sphere_ModelBuffer");

    return hr;
}
