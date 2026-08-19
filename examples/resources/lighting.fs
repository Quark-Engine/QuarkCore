#version 330 core

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform sampler2D uTexture;
uniform sampler2D shadowMaps[4];
uniform mat4      lightViews[4];
uniform mat4      lightProjections[4];
uniform vec4      colDiffuse;
uniform vec4      ambient;
uniform vec3      viewPos;

#define MAX_LIGHTS        4
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1

struct Light {
    vec3  position;
    vec3  target;
    vec4  color;
    float attenuation;
    int   enabled;
    int   type;
};

uniform Light lights[MAX_LIGHTS];

out vec4 finalColor;

float ShadowFactor(int lightIndex, vec3 worldPosition)
{
    vec4 lightSpacePosition = lightProjections[lightIndex] * lightViews[lightIndex]
        * vec4(worldPosition, 1.0);
    vec3 projected = lightSpacePosition.xyz / lightSpacePosition.w;
    projected = projected * 0.5 + 0.5;

    if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0)
        return 0.0;

    float currentDepth = projected.z;
    float bias = 0.004;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMaps[lightIndex], 0));
    float shadow = 0.0;

    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float closestDepth = texture(shadowMaps[lightIndex],
                projected.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > closestDepth ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

void main()
{
    vec4 texelColor = texture(uTexture, fragTexCoord);
    vec3 lightDot   = vec3(0.0);
    vec3 normal     = normalize(fragNormal);
    vec3 viewD      = normalize(viewPos - fragPosition);
    vec3 specular   = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1)
        {
            vec3 light = vec3(0.0);
            float lightAttenuation = 1.0;

            if (lights[i].type == LIGHT_DIRECTIONAL)
                light = -normalize(lights[i].target - lights[i].position);

            if (lights[i].type == LIGHT_POINT)
            {
                vec3 toLight = lights[i].position - fragPosition;
                float lightDistance = length(toLight);
                light = normalize(toLight);
                lightAttenuation = 1.0 /
                    (1.0 + lights[i].attenuation * lightDistance * lightDistance);
            }

            float NdotL = max(dot(normal, light), 0.0);
            float shadow = ShadowFactor(i, fragPosition);
            lightDot += lights[i].color.rgb * NdotL * lightAttenuation * (1.0 - shadow);

            float specCo = 0.0;
            if (NdotL > 0.0)
                specCo = pow(max(0.0, dot(viewD, reflect(-light, normal))), 16.0);
            specular += specCo * lightAttenuation * (1.0 - shadow);
        }
    }

    finalColor  = texelColor * (colDiffuse * vec4(lightDot, 1.0) + vec4(specular, 1.0));
    finalColor += texelColor * (ambient / 10.0) * colDiffuse;
    finalColor  = pow(finalColor, vec4(1.0 / 2.2));
}