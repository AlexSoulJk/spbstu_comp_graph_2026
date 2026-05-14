#pragma once
#include "Cube.h"


static void SetDebugName(ID3D11DeviceChild* child, const std::string& name) {
    if (child) {
        child->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
    }
}

void Cube::Update(float dt) {
    if (is_rotationable)
    {
        m_CubeAngle += 0.001f;
        if (m_CubeAngle > XM_2PI) m_CubeAngle -= XM_2PI;

        XMVECTOR scaleVec = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
        XMVECTOR rotQuat = XMQuaternionIdentity();
        XMVECTOR transVec = XMVectorZero();
        XMMATRIX model = XMMatrixRotationY(m_CubeAngle);
        if (XMMatrixDecompose(&scaleVec, &rotQuat, &transVec, m_modelMatrix))
        {
            model =
                XMMatrixScalingFromVector(scaleVec) *
                XMMatrixRotationY(m_CubeAngle) *
                XMMatrixTranslationFromVector(transVec);
        }

        m_modelMatrix = model;
        XMMATRIX mT = XMMatrixTranspose(m_modelMatrix);
        m_context->UpdateSubresource(m_pModelBuffer, 0, nullptr, &mT, 0, 0);
    }
    m_context->VSSetConstantBuffers(0, 1, &m_pModelBuffer);
}

void Cube::Render() {
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &stride, &offset);
    m_context->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->DrawIndexed(36, 0, 0);
}

HRESULT Cube::InitModel(ID3D11Device* device) {
    HRESULT hr;
    static const Vertex vertices[] =
    {
        { {-1.0f, -1.0f,  1.0f}, { 0.0f,  -1.0f,  0.0f}, {0.0f, 1.0f} },
        { { 1.0f, -1.0f,  1.0f}, { 0.0f,  -1.0f,  0.0f}, {1.0f, 1.0f} },
        { { 1.0f, -1.0f, -1.0f}, { 0.0f,  -1.0f,  0.0f}, {1.0f, 0.0f} },
        { {-1.0f, -1.0f, -1.0f}, { 0.0f,  -1.0f,  0.0f}, {0.0f, 0.0f} },

        { {-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f, 0.0f}, {0.0f, 1.0f} },
        { { 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f, 0.0f}, {1.0f, 1.0f} },
        { { 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f, 0.0f}, {1.0f, 0.0f} },
        { {-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f, 0.0f}, {0.0f, 0.0f} },

        { { 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },
        { { 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
        { { 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
        { { 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },

        { {-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },
        { {-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
        { {-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
        { {-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },

        { { 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f} },
        { {-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f} },
        { {-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f} },
        { { 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f} },

        { {-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f,  -1.0f}, {0.0f, 1.0f} },
        { { 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f,  -1.0f}, {1.0f, 1.0f} },
        { { 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f,  -1.0f}, {1.0f, 0.0f} },
        { {-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f,  -1.0f}, {0.0f, 0.0f} },
    };

    WORD indices[] =
    {
        0, 2, 1,
        0, 3, 2,

        4, 6, 5,
        4, 7, 6,

        8, 10, 9,
        8, 11, 10,

        12, 14, 13,
        12, 15, 14,

        16, 18, 17,
        16, 19, 18,

        20, 22, 21,
        20, 23, 22
    };

    // ???????? ???????
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(Vertex) * ARRAYSIZE(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;
    hr = device->CreateBuffer(&bd, &initData, &m_pVertexBuffer);
    if (FAILED(hr))
        return hr;

    SetDebugName(m_pVertexBuffer, "Cube_VertexBuffer");

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(WORD) * ARRAYSIZE(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    initData.pSysMem = indices;
    hr = device->CreateBuffer(&bd, &initData, &m_pIndexBuffer);
    if (FAILED(hr))
        return hr;

    SetDebugName(m_pIndexBuffer, "Cube_IndexBuffer");

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(XMMATRIX);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;

    hr = device->CreateBuffer(&bd, nullptr, &m_pModelBuffer);

    if (FAILED(hr))
        return hr;

    SetDebugName(m_pModelBuffer, "Cube_ConstantBuffer");
    return hr;
}

