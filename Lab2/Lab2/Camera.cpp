#include "Camera.h"

static void SetDebugName(ID3D11DeviceChild* child, const std::string& name) {
    if (child) {
        child->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
    }
}

Camera::Camera() :
    position(0, 0, -5.0f),
    speed(0.1f),
    LRAngle(0.0f),
    UDAngle(0.0f),
    m_pDeviceContext(nullptr),
    m_pVPBuffer(nullptr),
    isRightButtonPressed(false),
    mousePosX(0.0),
    mousePosY(0.0),
    sensitivity(0.001)
{
}



Camera::Camera(ID3D11DeviceContext* context) : Camera() {
    m_pDeviceContext = context;
};

HRESULT Camera::CameraUpdate(float aspectRatio) {
    curDirection = { cosf(UDAngle) * sinf(LRAngle),
        sinf(UDAngle),
        cosf(UDAngle) * cosf(LRAngle) };

    XMStoreFloat3(&right, XMVector3Cross(XMLoadFloat3(&curDirection), XMLoadFloat3(&up)));

    XMMATRIX vp = GetVP(aspectRatio);
    XMMATRIX vpT = XMMatrixTranspose(vp);
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = m_pDeviceContext->Map(m_pVPBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, &vpT, sizeof(XMMATRIX));
        m_pDeviceContext->Unmap(m_pVPBuffer, 0);
    }
    m_pDeviceContext->VSSetConstantBuffers(1, 1, &m_pVPBuffer);
    return hr;
}

void Camera::Move(MoveDirection direction) {
    switch (direction)
    {
    case MoveDirection::FORWARD:
        position.x += curDirection.x * speed;
        position.y += curDirection.y * speed;
        position.z += curDirection.z * speed;
        break;
    case MoveDirection::BACKWARD:
        position.x -= curDirection.x * speed;
        position.y -= curDirection.y * speed;
        position.z -= curDirection.z * speed;
        break;
    case MoveDirection::LEFT:
        position.x += right.x * speed;
        position.y += right.y * speed;
        position.z += right.z * speed;
        break;
    case MoveDirection::RIGHT:
        position.x -= right.x * speed;
        position.y -= right.y * speed;
        position.z -= right.z * speed;
        break;
    default:
        break;
    }
};

void Camera::Rotate(XMFLOAT2 angleDirection) {
    if (isRightButtonPressed) {
        LRAngle += (angleDirection.x - mousePosX) * sensitivity;
        UDAngle -= (angleDirection.y - mousePosY) * sensitivity;

        if (LRAngle > XM_2PI) LRAngle -= XM_2PI;
        if (LRAngle < -XM_2PI) LRAngle += XM_2PI;

        if (UDAngle > XM_PIDIV2) UDAngle = XM_PIDIV2;
        if (UDAngle < -XM_PIDIV2) UDAngle = -XM_PIDIV2;

        mousePosX = angleDirection.x;
        mousePosY = angleDirection.y;
    }
}

void Camera::RightButton(bool pressed, int posX, int posY) {
    isRightButtonPressed = pressed;
    if (isRightButtonPressed)
    {
        mousePosX = posX;
        mousePosY = posY;
    }
};

XMMATRIX Camera::GetVP(float aspectRatio) const
{
    // Вычисляем направление взгляда камеры
    XMVECTOR direction = XMVectorSet(
        cosf(UDAngle) * sinf(LRAngle),
        sinf(UDAngle),
        cosf(UDAngle) * cosf(LRAngle),
        0.0f
    );

    // Позиция камеры
    XMVECTOR eyePos = XMLoadFloat3(&position);

    // Точка, в которую смотрит камера (фокус)
    XMVECTOR focusPoint = XMVectorAdd(eyePos, direction);

    // Вектор "вверх" для камеры
    static const XMVECTOR upDir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // Матрица вида (view matrix)
    XMMATRIX view = XMMatrixLookAtLH(eyePos, focusPoint, upDir);

    // Матрица проекции (projection matrix)
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 100.0f);

    // Возвращаем произведение матриц вида и проекции
    return view * proj;
}
HRESULT Camera::InitVPBuffer(ID3D11Device* device) {
    D3D11_BUFFER_DESC vpBufferDesc = {};
    vpBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    vpBufferDesc.ByteWidth = sizeof(XMMATRIX);
    vpBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    vpBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    auto hr =device->CreateBuffer(&vpBufferDesc, nullptr, &m_pVPBuffer);

    if (FAILED(hr))
        return hr;

    SetDebugName(m_pVPBuffer, "Camera_VP_Buffer");
    return hr;
}
