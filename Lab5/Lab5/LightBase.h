#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <string>
using namespace DirectX;

struct PointLightGPU
{
    XMFLOAT3 Position;
    float Range;
    XMFLOAT3 Color;
    float Intensity;
};

class LightBase
{
public:
    virtual ~LightBase() = default;

    void SetName(const std::wstring& n) { m_name = n; }
    const std::wstring& GetName() const { return m_name; }

    void SetColor(const XMFLOAT3& c) { m_color = c; }
    const XMFLOAT3& GetColor() const { return m_color; }

    virtual void Fill(PointLightGPU& out) const = 0;

protected:
    std::wstring m_name = L"Light";
    XMFLOAT3 m_color = { 1.0f, 1.0f, 1.0f };
};
