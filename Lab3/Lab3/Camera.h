#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <vector>
#include <iostream>
#include <algorithm>
using namespace DirectX;
struct CameraBuffer
{
    DirectX::XMMATRIX vp;
};

enum struct MoveDirection {
    FORWARD = 0,
    BACKWARD,
    LEFT,
    RIGHT,
};

class Camera {


public:
    Camera();
    Camera(ID3D11DeviceContext* context);
    ~Camera() {
        if (m_pVPBuffer) {
            m_pVPBuffer->Release();
            m_pVPBuffer = nullptr;
        }
    }

    void Move(MoveDirection direction);
    void Rotate(XMFLOAT2 angleDirection);
    void RightButton(bool pressed, int posX, int posY);

    HRESULT CameraUpdate(float aspectRatio);
    HRESULT InitVPBuffer(ID3D11Device* device);
    XMFLOAT3 position;
    XMFLOAT3 curDirection;
    XMFLOAT3 right;
    XMFLOAT3 up = { 0.0, 1.0, 0.0 };
    float speed;
    float sensitivity;
    float LRAngle; 
    float UDAngle;

private:

    bool isRightButtonPressed;
    float mousePosX;
    float mousePosY;

    XMMATRIX GetVP(float aspectRatio) const;

    ID3D11DeviceContext* m_pDeviceContext;
    ID3D11Buffer* m_pVPBuffer;
};