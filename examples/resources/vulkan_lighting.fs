#version 450

struct Light {
    vec3  position;
    vec3  target;
    vec4  color;
    float attenuation;
    int   enabled;
    int   type;
};

layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 2) uniform sampler2D shadowMaps[4];
layout(set = 0, binding = 3) uniform ShadowMatrices {
    mat4 lightViews[4];
    mat4 lightProjections[4];
};
layout(set = 0, binding = 4) uniform LightsBlock {
    vec4 ambient;
    vec4 colDiffuse;
    vec4 viewPos;
    Light lights[4];
};

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

float ShadowFactor(int lightIndex, vec3 worldPosition)
{
    vec4 lightSpacePosition = lightProjections[lightIndex] * lightViews[lightIndex] * vec4(worldPosition, 1.0);
    vec3 projected = lightSpacePosition.xyz / max(lightSpacePosition.w, 0.0001);
    projected.xy = projected.xy * 0.5 + 0.5;

    if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0)
        return 0.0;

    float currentDepth = projected.z;
    float bias = 0.004;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMaps[lightIndex], 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closestDepth = texture(shadowMaps[lightIndex], projected.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > closestDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec4 texelColor = texture(albedo, vTexCoord) * vColor;
    vec3 normal = normalize(vNormal);
    vec3 viewD = normalize(viewPos.xyz - vWorldPos);
    vec3 lightDot = vec3(0.0);
    vec3 specular = vec3(0.0);

    for (int i = 0; i < 4; ++i) {
        if (lights[i].enabled == 1) {
            vec3 lightDir = vec3(0.0);
            float lightAttenuation = 1.0;

            if (lights[i].type == 0)
                lightDir = -normalize(lights[i].target - lights[i].position);

            if (lights[i].type == 1) {
                vec3 toLight = lights[i].position - vWorldPos;
                float lightDistance = length(toLight);
                lightDir = normalize(toLight);
                lightAttenuation = 1.0 / (1.0 + lights[i].attenuation * lightDistance * lightDistance);
            }

            float NdotL = max(dot(normal, lightDir), 0.0);
            float shadow = ShadowFactor(i, vWorldPos);
            lightDot += lights[i].color.rgb * NdotL * lightAttenuation * (1.0 - shadow);

            float specCo = 0.0;
            if (NdotL > 0.0)
                specCo = pow(max(0.0, dot(viewD, reflect(-lightDir, normal))), 16.0);
            specular += specCo * lightAttenuation * (1.0 - shadow);
        }
    }

    vec4 finalColor = texelColor * (colDiffuse * vec4(lightDot, 1.0) + vec4(specular, 1.0));
    finalColor += texelColor * (ambient / 10.0) * colDiffuse;
    finalColor = pow(finalColor, vec4(1.0 / 2.2));
    outColor = vec4(finalColor.rgb, texelColor.a);
}
