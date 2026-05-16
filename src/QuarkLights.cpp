#include "QuarkCore/QuarkLights.hpp"

namespace qc {

static int s_lightsCount = 0;

Light CreateLight(int type, Vec3 position, Vec3 target, Color color, Shader shader)
{
    Light light;

    if (s_lightsCount < QC_MAX_LIGHTS)
    {
        light.enabled = true;
        light.type = type;
        light.position = position;
        light.target = target;
        light.color = color;

        light.enabledLoc = GetShaderLocation(shader, TextFormat("lights[%i].enabled", s_lightsCount));
        light.typeLoc = GetShaderLocation(shader, TextFormat("lights[%i].type", s_lightsCount));
        light.positionLoc = GetShaderLocation(shader, TextFormat("lights[%i].position", s_lightsCount));
        light.targetLoc = GetShaderLocation(shader, TextFormat("lights[%i].target", s_lightsCount));
        light.colorLoc = GetShaderLocation(shader, TextFormat("lights[%i].color", s_lightsCount));
        light.attenuationLoc = GetShaderLocation(shader, TextFormat("lights[%i].attenuation", s_lightsCount));

        UpdateLightValues(shader, light);

        s_lightsCount++;
    }

    return light;
}

void UpdateLightValues(Shader shader, const Light& light)
{
    SetShaderValue(shader, light.enabledLoc, light.enabled ? 1 : 0);
    SetShaderValue(shader, light.typeLoc, light.type);

    SetShaderValue(shader, light.positionLoc, light.position);
    SetShaderValue(shader, light.targetLoc, light.target);

    SetShaderValue(shader, light.colorLoc, light.color);

    if (light.attenuationLoc >= 0)
        SetShaderValue(shader, light.attenuationLoc, light.attenuation);
}

} // namespace qc
