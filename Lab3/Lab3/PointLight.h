#pragma once
#include "LightBase.h"

class PointLight : public LightBase
{
public:
    void SetPosition(const XMFLOAT3& p) { m_position = p; }
    const XMFLOAT3& GetPosition() const { return m_position; }

    void SetRange(float r) { m_range = r; }
    float GetRange() const { return m_range; }

    void SetIntensity(float i) { m_intensity = i; }
    float GetIntensity() const { return m_intensity; }

    void Fill(PointLightGPU& out) const override;

private:
    XMFLOAT3 m_position = { 0.0f, 0.0f, -1.0f };
    float    m_range = 10.0f;
    float    m_intensity = 10.0f;
};

