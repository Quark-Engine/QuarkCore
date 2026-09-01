#include "QuarkGL3D.hpp"

#include <glad/glad.h>

namespace qc {

namespace {

static const char* kVS3D = R"(
#version 330 core

layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vFragPos = vec3(uModel * vec4(aPosition, 1.0));
    vNormal = mat3(uModel) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}
)";

static const char* kFS3D = R"(
#version 330 core

in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4 uColor;

void main() {
    vec4 tex = texture(uTexture, vTexCoord);
    vec3 result = tex.rgb * uColor.rgb;
    FragColor = vec4(result, tex.a * uColor.a);
}
)";

static void SetUniformMatrix(GLint location, const Mat4& matrix) {
    if (location >= 0) {
        glUniformMatrix4fv(location, 1, GL_FALSE, matrix.m);
    }
}

static void SetUniformColor(GLint location, Color color) {
    if (location >= 0) {
        glUniform4f(location,
            color.r / 255.0f,
            color.g / 255.0f,
            color.b / 255.0f,
            color.a / 255.0f);
    }
}

static void BindWhiteTexture(const Model3DState& state) {
    if (state.whiteTexture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state.whiteTexture);
    }
}

} // namespace

void QuarkGL3D::Init3DState(Model3DState& state) {
    if (state.initialized) return;

    state.shader3D = Compile3DShader();
    state.modelLoc = glGetUniformLocation(state.shader3D, "uModel");
    state.viewLoc = glGetUniformLocation(state.shader3D, "uView");
    state.projLoc = glGetUniformLocation(state.shader3D, "uProjection");
    state.samplerLoc = glGetUniformLocation(state.shader3D, "uTexture");
    state.lightPosLoc = glGetUniformLocation(state.shader3D, "uLightPos");
    state.colorLoc = glGetUniformLocation(state.shader3D, "uColor");
    state.initialized = true;

    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &state.whiteTexture);
    glBindTexture(GL_TEXTURE_2D, state.whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    const uint8_t black[4] = {0, 0, 0, 255};
    glGenTextures(1, &state.blackTexture);
    glBindTexture(GL_TEXTURE_2D, state.blackTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    const uint8_t flatNormal[4] = {128, 128, 255, 255};
    glGenTextures(1, &state.flatNormalTexture);
    glBindTexture(GL_TEXTURE_2D, state.flatNormalTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatNormal);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

GLuint QuarkGL3D::Compile3DShader() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kVS3D, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &kFS3D, nullptr);
    glCompileShader(fs);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

void QuarkGL3D::Init3DGeometry(Model3DState& state) {
    if (state.planeVAO != 0) return;

    auto setup = [](GLuint& vao, GLuint& vbo, GLuint& ebo,
                    const float* data, size_t dataSize,
                    const unsigned int* indices, size_t indexSize) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(dataSize), data, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexSize), indices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(0));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(12));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(24));

        glBindVertexArray(0);
    };

    float planeVertices[] = {
        -0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
         0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
         0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
    };
    unsigned int planeIndices[] = {0, 1, 2, 0, 2, 3};
    state.planeIndexCount = 6;
    setup(state.planeVAO, state.planeVBO, state.planeEBO,
          planeVertices, sizeof(planeVertices), planeIndices, sizeof(planeIndices));

    float cubeVertices[] = {
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,-1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 0.0f,-1.0f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  0.0f, 0.0f,-1.0f, 1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 0.0f,-1.0f, 0.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,-1.0f, 0.0f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  0.0f,-1.0f, 0.0f, 1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f,-1.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,-1.0f, 0.0f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    unsigned int cubeIndices[] = {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23
    };
    state.cubeIndexCount = 36;
    setup(state.cubeVAO, state.cubeVBO, state.cubeEBO,
          cubeVertices, sizeof(cubeVertices), cubeIndices, sizeof(cubeIndices));

    std::vector<float> sphereVertices;
    std::vector<unsigned int> sphereIndices;
    const int rings = 16;
    const int slices = 16;
    for (int r = 0; r <= rings; ++r) {
        float phi = PI * r / rings;
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f * PI * s / slices;
            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);
            sphereVertices.insert(sphereVertices.end(), {x, y, z, x, y, z, static_cast<float>(s) / slices, static_cast<float>(r) / rings});
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < slices; ++s) {
            sphereIndices.push_back(static_cast<unsigned int>(r * (slices + 1) + s));
            sphereIndices.push_back(static_cast<unsigned int>((r + 1) * (slices + 1) + s));
            sphereIndices.push_back(static_cast<unsigned int>((r + 1) * (slices + 1) + (s + 1)));
            sphereIndices.push_back(static_cast<unsigned int>(r * (slices + 1) + s));
            sphereIndices.push_back(static_cast<unsigned int>((r + 1) * (slices + 1) + (s + 1)));
            sphereIndices.push_back(static_cast<unsigned int>(r * (slices + 1) + (s + 1)));
        }
    }
    state.sphereIndexCount = static_cast<int>(sphereIndices.size());
    setup(state.sphereVAO, state.sphereVBO, state.sphereEBO,
          sphereVertices.data(), sphereVertices.size() * sizeof(float),
          sphereIndices.data(), sphereIndices.size() * sizeof(unsigned int));

    auto dynVao = [](GLuint& vao, GLuint& vbo) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, position)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, normal)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, texCoord)));

        glBindVertexArray(0);
    };

    dynVao(state.lineVAO, state.lineVBO);
    dynVao(state.triVAO, state.triVBO);
}

void QuarkGL3D::Set3DView(Model3DState& state, const Mat4& view, const Mat4& projection) {
    state.viewMatrix = view;
    state.projectionMatrix = projection;
    if (state.initialized) {
        SetUniformMatrix(state.viewLoc, state.viewMatrix);
        SetUniformMatrix(state.projLoc, state.projectionMatrix);
    }
}

Vec3 QuarkGL3D::TransformPoint(const Mat4& matrix, const Vec3& point) {
    float x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
    float y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
    float z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
    float w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];

    if (w != 0.0f) {
        x /= w;
        y /= w;
        z /= w;
    }

    return {x, y, z};
}

Mat4 QuarkGL3D::ApplyCurrentMatrix(const Mat4& current, const Mat4& transform) {
    return current * transform;
}

void QuarkGL3D::ApplyDrawState(Model3DState& state, const Mat4& model, Color color, GLuint textureId) {
    glUseProgram(state.shader3D);
    SetUniformMatrix(state.viewLoc, state.viewMatrix);
    SetUniformMatrix(state.projLoc, state.projectionMatrix);
    SetUniformMatrix(state.modelLoc, model);
    SetUniformColor(state.colorLoc, color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId != 0 ? textureId : state.whiteTexture);
}

void QuarkGL3D::FlushLines3D(Model3DState& state) {
    if (state.lineVertices.empty()) return;

    glUseProgram(state.shader3D);
    SetUniformMatrix(state.viewLoc, state.viewMatrix);
    SetUniformMatrix(state.projLoc, state.projectionMatrix);
    SetUniformMatrix(state.modelLoc, Mat4::identity());
    SetUniformColor(state.colorLoc, state.currentLineColor);
    BindWhiteTexture(state);

    glBindVertexArray(state.lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, state.lineVBO);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(state.lineVertices.size() * sizeof(Vertex3D)), state.lineVertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(state.lineVertices.size()));
    glBindVertexArray(0);
    state.lineVertices.clear();
}

void QuarkGL3D::FlushTriangles3D(Model3DState& state) {
    if (state.triVertices.empty()) return;

    glUseProgram(state.shader3D);
    SetUniformMatrix(state.viewLoc, state.viewMatrix);
    SetUniformMatrix(state.projLoc, state.projectionMatrix);
    SetUniformMatrix(state.modelLoc, Mat4::identity());
    SetUniformColor(state.colorLoc, state.currentTriColor);
    BindWhiteTexture(state);

    glBindVertexArray(state.triVAO);
    glBindBuffer(GL_ARRAY_BUFFER, state.triVBO);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(state.triVertices.size() * sizeof(Vertex3D)), state.triVertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(state.triVertices.size()));
    glBindVertexArray(0);
    state.triVertices.clear();
}

void QuarkGL3D::DrawTriangle3DImpl(Model3DState& state, const Mat4& currentMatrix,
                                  Vertex3D v1, Vertex3D v2, Vertex3D v3, Color color) {
    if (!state.triVertices.empty() && (color.r != state.currentTriColor.r ||
        color.g != state.currentTriColor.g ||
        color.b != state.currentTriColor.b ||
        color.a != state.currentTriColor.a)) {
        FlushTriangles3D(state);
    }

    state.currentTriColor = color;
    SetUniformColor(state.colorLoc, color);

    state.triVertices.push_back({TransformPoint(currentMatrix, v1.position), v1.normal, v1.texCoord});
    state.triVertices.push_back({TransformPoint(currentMatrix, v2.position), v2.normal, v2.texCoord});
    state.triVertices.push_back({TransformPoint(currentMatrix, v3.position), v3.normal, v3.texCoord});
}

void QuarkGL3D::DrawLine3D(Model3DState& state, const Mat4& currentMatrix,
                          Vec3 start, Vec3 end, Color color) {
    if (!state.lineVertices.empty() && (color.r != state.currentLineColor.r ||
        color.g != state.currentLineColor.g ||
        color.b != state.currentLineColor.b ||
        color.a != state.currentLineColor.a)) {
        FlushLines3D(state);
    }

    state.currentLineColor = color;
    SetUniformColor(state.colorLoc, color);

    state.lineVertices.push_back({TransformPoint(currentMatrix, start), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
    state.lineVertices.push_back({TransformPoint(currentMatrix, end), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
}

} // namespace qc
