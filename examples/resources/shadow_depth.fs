#version 330 core

out vec4 finalColor;

void main()
{
    float depth = gl_FragCoord.z;
    finalColor = vec4(depth, depth, depth, 1.0);
}
