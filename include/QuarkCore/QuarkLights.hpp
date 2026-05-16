#pragma once

#include "QuarkCore.hpp"

namespace qc {

#define QC_MAX_LIGHTS 4

typedef enum {
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT
} LightType;

struct Light {
    int type = 0;
    bool enabled = false;
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Color color{0,0,0,255};
    float attenuation = 1.0f;

    int enabledLoc = -1;
    int typeLoc = -1;
    int positionLoc = -1;
    int targetLoc = -1;
    int colorLoc = -1;
    int attenuationLoc = -1;
};

QCAPI Light CreateLight(int type, Vec3 position, Vec3 target, Color color, Shader shader);

QCAPI void UpdateLightValues(Shader shader, const Light& light);

} // namespace qc
