#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec3 vNormal;

void main() {
    float depth = gl_FragCoord.z;
    outColor = vec4(depth, depth, depth, 1.0);
}
