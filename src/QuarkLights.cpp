#include "QuarkCore/QuarkLights.hpp"

static int s_LightsCount = 0;

Light CreateLight(int type, Vec3 position, Vec3 target, Color color, Shader shader)
{
    Light light;

    if (s_LightsCount < QC_MAX_LIGHTS)
    {
        light.enabled = true;
        light.type = type;
        light.position = position;
        light.target = target;
        light.color = color;

        light.enabledLoc = GetShaderLocation(shader, TextFormat("lights[%i].enabled", s_LightsCount));
        light.typeLoc = GetShaderLocation(shader, TextFormat("lights[%i].type", s_LightsCount));
        light.positionLoc = GetShaderLocation(shader, TextFormat("lights[%i].position", s_LightsCount));
        light.targetLoc = GetShaderLocation(shader, TextFormat("lights[%i].target", s_LightsCount));
        light.colorLoc = GetShaderLocation(shader, TextFormat("lights[%i].color", s_LightsCount));
        light.attenuationLoc = GetShaderLocation(shader, TextFormat("lights[%i].attenuation", s_LightsCount));

        UpdateLightValues(shader, light);

        s_LightsCount++;
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
    {
        SetShaderValue(shader, light.attenuationLoc, light.attenuation);
    }
}
