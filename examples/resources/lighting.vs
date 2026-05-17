#version 330 core

layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main()
{
    fragPosition = vec3(uModel * vec4(aPosition, 1.0));
    fragTexCoord = aTexCoord;
    fragNormal   = normalize(mat3(uModel) * aNormal);
    gl_Position  = uProjection * uView * vec4(fragPosition, 1.0);
}