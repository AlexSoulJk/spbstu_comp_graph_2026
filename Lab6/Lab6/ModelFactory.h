#pragma once

#include "Cube.h"
#include "GltfModel.h"
#include "Sphere.h"

class ModelFactory
{
public:
    enum ModelCode
    {
        cube,
        sphere,
        gltfImpala,
        gltfLightSaber,
        gltfR2D2,
    };

    static ModelManagerAbstract* CreateModel(ModelCode code, ID3D11DeviceContext* context)
    {
        switch (code)
        {
        case ModelCode::cube:
            return new Cube(context);
        case ModelCode::sphere:
            return new Sphere(context);
        case ModelCode::gltfImpala:
            return new GltfModel(context, L"models\\Impala\\scene.gltf", L"Impala");
        case ModelCode::gltfLightSaber:
            return new GltfModel(context, L"models\\LightSaber\\scene.gltf", L"LightSaber");
        case ModelCode::gltfR2D2:
            return new GltfModel(context, L"models\\R2D2\\scene.gltf", L"R2D2");
        default:
            return nullptr;
        }
    }

    static void ReleaseModel(ModelManagerAbstract* model)
    {
        if (model != nullptr)
            delete model;
    }
};

