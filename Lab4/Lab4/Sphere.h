#pragma once

#include "ModelManagerAbstract.h"

class Sphere : public ModelManagerAbstract
{
private:
    float m_sphereAngle;
    unsigned int m_indexCount;

public:
    Sphere(ID3D11DeviceContext* context) : ModelManagerAbstract(context), m_sphereAngle(0.0f), m_indexCount(0) {}
    ~Sphere() override = default;

    void Update(float dt) override;
    void Render() override;
    HRESULT InitModel(ID3D11Device* device) override;
};

