#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <string>
using namespace DirectX;
class ModelManagerAbstract
{
public:

    struct Vertex {
        XMFLOAT3 xyz;
        XMFLOAT3 normal;
        XMFLOAT2 uv;
        XMFLOAT3 tangent;
        XMFLOAT3 bitangent;
    };
    DirectX::XMMATRIX GetModelMatrix() const { return m_modelMatrix; }
    ModelManagerAbstract(ID3D11DeviceContext* context);
    virtual ~ModelManagerAbstract() {
        if (m_pVertexBuffer) {
            m_pVertexBuffer->Release(); 
            m_pVertexBuffer = nullptr;
        }
        if (m_pIndexBuffer) {
            m_pIndexBuffer->Release(); 
            m_pIndexBuffer = nullptr;
        }
        if (m_pModelBuffer) {
            m_pModelBuffer->Release(); 
            m_pModelBuffer = nullptr;
        }
    }
    virtual HRESULT InitModel(ID3D11Device* device) = 0;
    void ChangeRotationable() {
        is_rotationable = !is_rotationable;
    }
    void SetModelMatrix(const DirectX::XMMATRIX& model)
    {
        m_modelMatrix = model;
        if (m_context && m_pModelBuffer)
        {
            DirectX::XMMATRIX modelT = DirectX::XMMatrixTranspose(m_modelMatrix);
            m_context->UpdateSubresource(m_pModelBuffer, 0, nullptr, &modelT, 0, 0);
            m_context->VSSetConstantBuffers(0, 1, &m_pModelBuffer);
        }
    }
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;
protected:

    ID3D11Buffer* m_pModelBuffer = nullptr;
    ID3D11Buffer* m_pVertexBuffer = nullptr;
    ID3D11Buffer* m_pIndexBuffer = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    DirectX::XMMATRIX m_modelMatrix = DirectX::XMMatrixIdentity();
    float m_rotationAngle = 0.0f;
    bool is_rotationable = false;
};


