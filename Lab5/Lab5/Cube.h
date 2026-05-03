#pragma once
#include "ModelManagerAbstract.h"
class Cube : public ModelManagerAbstract {
private:
    float m_CubeAngle;
public:
    Cube(ID3D11DeviceContext* context) : ModelManagerAbstract(context), m_CubeAngle() {}
    ~Cube() override = default;
    void Update(float dt) override;
    void Render() override;
    HRESULT InitModel(ID3D11Device* device);
};
