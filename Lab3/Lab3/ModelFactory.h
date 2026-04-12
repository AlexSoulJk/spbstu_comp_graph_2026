#pragma once
#include "Cube.h"

class ModelFactory {
public:
    enum ModelCode {
        cube,
    };
    static ModelManagerAbstract* CreateModel(ModelCode code, ID3D11DeviceContext* context) {
        switch (code) {
        case ModelCode::cube:
            return new Cube(context);
        default:
            return nullptr;
        }
    }

    static void ReleaseModel(ModelManagerAbstract* model) {
        if (model) delete model;
    }
};

