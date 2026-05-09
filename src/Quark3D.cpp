#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"

#include <GL/glew.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstddef>
#include <string>
#include <vector>

namespace qc {

namespace {

struct Model3DState {
    std::vector<Model> loadedModels;
    unsigned int nextModelId = 1;
    Mat4 viewMatrix = Mat4::identity();
    Mat4 projectionMatrix = Mat4::identity();
    GLuint shader3D = 0;
    GLint modelLoc = -1;
    GLint viewLoc = -1;
    GLint projLoc = -1;
    GLint samplerLoc = -1;
    GLint lightPosLoc = -1;
    GLuint whiteTexture = 0;
    Vec3 lightPosition{5.0f, 5.0f, 5.0f};
    bool initialized = false;
};

Model3DState g3DState;

const char* kVertex3DShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

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

const char* kFragment3DShaderSource = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec3 uLightPos;

void main() {
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vFragPos);
    
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * vec3(1.0, 1.0, 1.0);
    
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0, 1.0, 1.0);
    
    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 result = (ambient + diffuse) * texColor.rgb;
    FragColor = vec4(result, texColor.a);
}
)";

GLuint Compile3DShader() {
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kVertex3DShaderSource, nullptr);
    glCompileShader(vertex);

    GLint vStatus;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &vStatus);
    if (!vStatus) {
        char log[512];
        glGetShaderInfoLog(vertex, 512, nullptr, log);
        TraceLog(LogLevel::Error, "GLSL", TextFormat("Vertex shader error: %s", log));
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kFragment3DShaderSource, nullptr);
    glCompileShader(fragment);

    GLint fStatus;
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &fStatus);
    if (!fStatus) {
        char log[512];
        glGetShaderInfoLog(fragment, 512, nullptr, log);
        TraceLog(LogLevel::Error, "GLSL", TextFormat("Fragment shader error: %s", log));
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint lStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &lStatus);
    if (!lStatus) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        TraceLog(LogLevel::Error, "GLSL", TextFormat("Program link error: %s", log));
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

void ProcessMesh(const aiMesh* aiMesh, const aiScene* scene, Mesh& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();

    for (unsigned int i = 0; i < aiMesh->mNumVertices; ++i) {
        Vertex3D vertex{};
        vertex.position = Vec3(aiMesh->mVertices[i].x, aiMesh->mVertices[i].y, aiMesh->mVertices[i].z);

        if (aiMesh->HasNormals()) {
            vertex.normal = Vec3(aiMesh->mNormals[i].x, aiMesh->mNormals[i].y, aiMesh->mNormals[i].z);
            vertex.normal = vertex.normal.normalized();
        } else {
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
        }

        if (aiMesh->mTextureCoords[0]) {
            vertex.texCoord.x = aiMesh->mTextureCoords[0][i].x;
            vertex.texCoord.y = aiMesh->mTextureCoords[0][i].y;
        }

        mesh.vertices.push_back(vertex);
    }

    for (unsigned int i = 0; i < aiMesh->mNumFaces; ++i) {
        const aiFace& face = aiMesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            mesh.indices.push_back(face.mIndices[j]);
        }
    }

    mesh.materialIndex = aiMesh->mMaterialIndex;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex3D), mesh.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, texCoord));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ProcessMaterials(const aiScene* scene, Model& model, const char* directory) {
    model.materials.clear();

    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* mat = scene->mMaterials[i];
        Material material{};

        aiString matName;
        if (mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
            material.name = matName.C_Str();
        }

        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        material.diffuse = Vec3(diffuse.r, diffuse.g, diffuse.b);

        float shininess = 32.0f;
        mat->Get(AI_MATKEY_SHININESS, shininess);
        material.shininess = shininess;

        if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString texPath;
            mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
            std::string fullPath = std::string(directory) + "/" + std::string(texPath.C_Str());
            Texture2D tex = LoadTexture(fullPath.c_str());
            if (tex.valid) {
                material.diffuseMap = tex.id;
            }
        }

        model.materials.push_back(material);
    }
}

void ProcessNode(const aiNode* node, const aiScene* scene, Model& model) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* aiMesh = scene->mMeshes[node->mMeshes[i]];
        Mesh mesh{};
        ProcessMesh(aiMesh, scene, mesh);
        model.meshes.push_back(mesh);
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ProcessNode(node->mChildren[i], scene, model);
    }
}

}  // namespace

Model LoadModel(const char* filePath) {
    Model model{};
    model.id = g3DState.nextModelId++;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        TraceLog(LogLevel::Error, "ASSIMP", importer.GetErrorString());
        return model;
    }

    std::string fullPath = filePath;
    size_t lastSlash = fullPath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        model.directory = fullPath.substr(0, lastSlash);
    }

    ProcessMaterials(scene, model, model.directory.c_str());
    ProcessNode(scene->mRootNode, scene, model);

    TraceLog(LogLevel::Info, "ASSIMP", TextFormat("Loaded model '%s' with %zu meshes", filePath, model.meshes.size()));
    return model;
}

void UnloadModel(Model& model) {
    for (auto& mesh : model.meshes) {
        if (mesh.vao != 0) glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo != 0) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ebo != 0) glDeleteBuffers(1, &mesh.ebo);
    }

    for (auto& material : model.materials) {
        if (material.diffuseMap != 0) {
            glDeleteTextures(1, &material.diffuseMap);
        }
    }

    model.meshes.clear();
    model.materials.clear();
    model.id = 0;
}

void Begin3D() {
    if (!g3DState.initialized) {
        g3DState.shader3D = Compile3DShader();
        g3DState.modelLoc = glGetUniformLocation(g3DState.shader3D, "uModel");
        g3DState.viewLoc = glGetUniformLocation(g3DState.shader3D, "uView");
        g3DState.projLoc = glGetUniformLocation(g3DState.shader3D, "uProjection");
        g3DState.samplerLoc = glGetUniformLocation(g3DState.shader3D, "uTexture");
        g3DState.lightPosLoc = glGetUniformLocation(g3DState.shader3D, "uLightPos");
        g3DState.initialized = true;

        std::vector<std::uint8_t> whitePixels(4, 255);
        glGenTextures(1, &g3DState.whiteTexture);
        glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(g3DState.shader3D);

    if (g3DState.samplerLoc >= 0) {
        glUniform1i(g3DState.samplerLoc, 0);
    }
    
    if (g3DState.lightPosLoc >= 0) {
        glUniform3f(g3DState.lightPosLoc, g3DState.lightPosition.x, g3DState.lightPosition.y, g3DState.lightPosition.z);
    }
}

void End3D() {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
}

void Set3DView(const Mat4& view, const Mat4& projection) {
    g3DState.viewMatrix = view;
    g3DState.projectionMatrix = projection;

    if (g3DState.initialized) {
        glUniformMatrix4fv(g3DState.viewLoc, 1, GL_FALSE, g3DState.viewMatrix.m);
        glUniformMatrix4fv(g3DState.projLoc, 1, GL_FALSE, g3DState.projectionMatrix.m);
    }
}

void DrawModel(const Model& model, const Vec3& position, float scale,
               float rotationX, float rotationY, float rotationZ) {
    Mat4 transform = Mat4::translation(position.x, position.y, position.z);
    transform = transform * Mat4::rotationY(rotationY);
    transform = transform * Mat4::rotationX(rotationX);
    transform = transform * Mat4::rotationZ(rotationZ);
    transform = transform * Mat4::scale(scale, scale, scale);

    DrawModelEx(model, transform);
}

void DrawModelEx(const Model& model, const Mat4& transform) {
    if (g3DState.modelLoc >= 0) {
        glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, transform.m);
    }

    for (const auto& mesh : model.meshes) {
        glActiveTexture(GL_TEXTURE0);
        
        GLuint textureId = g3DState.whiteTexture;
        if (mesh.materialIndex < model.materials.size()) {
            const Material& mat = model.materials[mesh.materialIndex];
            if (mat.diffuseMap != 0) {
                textureId = mat.diffuseMap;
            }
        }
        glBindTexture(GL_TEXTURE_2D, textureId);

        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }
}

}  // namespace qc
