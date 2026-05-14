#pragma once
#include "Cube.h"
#include "Sphere.h"

class ModelFactory {
public:
    enum ModelCode {
        cube,
        sphere,
    };
    static ModelManagerAbstract* CreateModel(ModelCode code, ID3D11DeviceContext* context) {
        switch (code) {
        case ModelCode::cube:
            return new Cube(context);
        case ModelCode::sphere:
            return new Sphere(context);
        default:
            return nullptr;
        }
    }

    static void ReleaseModel(ModelManagerAbstract* model) {
        if (model) delete model;
    }
};

