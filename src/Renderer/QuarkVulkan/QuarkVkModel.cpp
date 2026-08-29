#include "QuarkVkRenderer.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace qc {

namespace {

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

static Color MultiplyColor(Color lhs, Color rhs) {
    return Color{
        static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.r) * rhs.r) / 255u),
        static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.g) * rhs.g) / 255u),
        static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.b) * rhs.b) / 255u),
        static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.a) * rhs.a) / 255u)
    };
}

static void FreeMeshCpuData(Mesh& mesh) {
    delete[] mesh.vertices;
    delete[] mesh.texcoords;
    delete[] mesh.texcoords2;
    delete[] mesh.normals;
    delete[] mesh.tangents;
    delete[] mesh.colors;
    delete[] mesh.indices;
    delete[] mesh.boneIndices;
    delete[] mesh.boneWeights;
    delete[] mesh.animVertices;
    delete[] mesh.animNormals;

    mesh.vertices = nullptr;
    mesh.texcoords = nullptr;
    mesh.texcoords2 = nullptr;
    mesh.normals = nullptr;
    mesh.tangents = nullptr;
    mesh.colors = nullptr;
    mesh.indices = nullptr;
    mesh.boneIndices = nullptr;
    mesh.boneWeights = nullptr;
    mesh.animVertices = nullptr;
    mesh.animNormals = nullptr;

    mesh.vertexCount = 0;
    mesh.triangleCount = 0;
    mesh.boneCount = 0;
}

static std::string GetModelDirectory(const char* filePath) {
    if (!filePath) {
        return {};
    }

    std::string path = filePath;
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash + 1);
}

static Material LoadAssimpMaterial(QuarkVkRenderer& renderer, const char* filePath, aiMaterial* source) {
    Material material{};
    material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
    material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;

    if (!source) {
        return material;
    }

    aiColor4D diffuse{};
    if (AI_SUCCESS == aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &diffuse)) {
        material.maps[MATERIAL_MAP_ALBEDO].color = Color{
            static_cast<std::uint8_t>(std::clamp(diffuse.r, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(diffuse.g, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(diffuse.b, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(diffuse.a, 0.0f, 1.0f) * 255.0f)
        };
    }

    const std::array<std::pair<int, aiTextureType>, 7> textureTypes = {{
        { MATERIAL_MAP_ALBEDO, aiTextureType_BASE_COLOR },
        { MATERIAL_MAP_ALBEDO, aiTextureType_DIFFUSE },
        { MATERIAL_MAP_METALNESS, aiTextureType_METALNESS },
        { MATERIAL_MAP_NORMAL, aiTextureType_NORMALS },
        { MATERIAL_MAP_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS },
        { MATERIAL_MAP_OCCLUSION, aiTextureType_AMBIENT_OCCLUSION },
        { MATERIAL_MAP_EMISSION, aiTextureType_EMISSION_COLOR }
    }};
    for (const auto& [mapIndex, textureType] : textureTypes) {
        if (material.maps[mapIndex].texture.valid) continue;
        aiString texturePath;
        if (AI_SUCCESS != source->GetTexture(textureType, 0, &texturePath)) continue;

        std::string resolvedPath = GetModelDirectory(filePath);
        resolvedPath += texturePath.C_Str();
        TraceLog(LogLevel::Trace, "MODEL", TextFormat("[Vulkan] Model material map %d: %s", mapIndex, resolvedPath.c_str()));
        ITexture loadedTex = renderer.LoadTexture(resolvedPath.c_str());
        if (loadedTex.valid) {
            material.maps[mapIndex].texture.id = loadedTex.id;
            material.maps[mapIndex].texture.width = loadedTex.width;
            material.maps[mapIndex].texture.height = loadedTex.height;
            material.maps[mapIndex].texture.valid = loadedTex.valid;
        }
    }

    return material;
}

static Vec3 ReadVertexPosition(const aiMesh& mesh, unsigned int index) {
    return Vec3{
        mesh.mVertices[index].x,
        mesh.mVertices[index].y,
        mesh.mVertices[index].z
    };
}

static Vec3 ReadVertexNormal(const aiMesh& mesh, unsigned int index) {
    if (!mesh.HasNormals()) {
        return Vec3{0.0f, 0.0f, 0.0f};
    }
    return Vec3{
        mesh.mNormals[index].x,
        mesh.mNormals[index].y,
        mesh.mNormals[index].z
    };
}

static Vec2 ReadVertexTexCoord(const aiMesh& mesh, unsigned int index) {
    if (!mesh.HasTextureCoords(0)) {
        return Vec2{0.0f, 0.0f};
    }
    return Vec2{
        mesh.mTextureCoords[0][index].x,
        mesh.mTextureCoords[0][index].y
    };
}

static Color ReadVertexColor(const Mesh& mesh, int index, Color fallback) {
    if (!mesh.colors) {
        return fallback;
    }

    return Color{
        mesh.colors[index * 4 + 0],
        mesh.colors[index * 4 + 1],
        mesh.colors[index * 4 + 2],
        mesh.colors[index * 4 + 3]
    };
}

} // namespace

Model QuarkVkRenderer::LoadModel(const char* filePath) {
    TraceLog(LogLevel::Info, "MODEL", TextFormat("[Vulkan] Loading 3D model: %s", filePath ? filePath : "<null>"));

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        filePath,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !scene->mRootNode) {
        TraceLog(LogLevel::Error, "MODEL", TextFormat("[Vulkan] Failed to load model %s: %s",
            filePath ? filePath : "<null>", importer.GetErrorString()));
        return Model{};
    }

    TraceLog(LogLevel::Trace, "MODEL", TextFormat("[Vulkan] Assimp scene parsed: %u meshes, %u materials, %u textures, %u animations",
        scene->mNumMeshes, scene->mNumMaterials, scene->mNumTextures, scene->mNumAnimations));

    Model model{};
    model.directory = GetModelDirectory(filePath);
    model.meshCount = static_cast<int>(scene->mNumMeshes);
    model.materialCount = static_cast<int>(scene->mNumMaterials);
    model.meshes = (model.meshCount > 0) ? new Mesh[model.meshCount]{} : nullptr;
    model.materials = (model.materialCount > 0) ? new Material[model.materialCount]{} : nullptr;
    model.meshMaterial = (model.meshCount > 0) ? new int[model.meshCount]{} : nullptr;

    for (int i = 0; i < model.materialCount; ++i) {
        model.materials[i] = LoadAssimpMaterial(*this, filePath, scene->mMaterials[i]);
    }

    int totalVertices = 0;
    int totalTriangles = 0;

    for (int i = 0; i < model.meshCount; ++i) {
        const aiMesh* sourceMesh = scene->mMeshes[i];
        if (!sourceMesh) {
            continue;
        }

        Mesh& dst = model.meshes[i];
        dst.vertexCount = static_cast<int>(sourceMesh->mNumVertices);
        dst.triangleCount = static_cast<int>(sourceMesh->mNumFaces);
        totalVertices += dst.vertexCount;
        totalTriangles += dst.triangleCount;

        dst.vertices = (dst.vertexCount > 0) ? new float[dst.vertexCount * 3]{} : nullptr;
        dst.normals = (dst.vertexCount > 0) ? new float[dst.vertexCount * 3]{} : nullptr;
        dst.texcoords = (dst.vertexCount > 0) ? new float[dst.vertexCount * 2]{} : nullptr;
        dst.indices = (dst.triangleCount > 0) ? new unsigned short[dst.triangleCount * 3]{} : nullptr;

        for (int v = 0; v < dst.vertexCount; ++v) {
            const Vec3 position = ReadVertexPosition(*sourceMesh, static_cast<unsigned int>(v));
            const Vec3 normal = ReadVertexNormal(*sourceMesh, static_cast<unsigned int>(v));
            const Vec2 texCoord = ReadVertexTexCoord(*sourceMesh, static_cast<unsigned int>(v));

            dst.vertices[v * 3 + 0] = position.x;
            dst.vertices[v * 3 + 1] = position.y;
            dst.vertices[v * 3 + 2] = position.z;

            dst.normals[v * 3 + 0] = normal.x;
            dst.normals[v * 3 + 1] = normal.y;
            dst.normals[v * 3 + 2] = normal.z;

            dst.texcoords[v * 2 + 0] = texCoord.x;
            dst.texcoords[v * 2 + 1] = texCoord.y;
        }

        for (int f = 0; f < dst.triangleCount; ++f) {
            const aiFace& face = sourceMesh->mFaces[f];
            if (face.mNumIndices < 3) {
                continue;
            }

            dst.indices[f * 3 + 0] = static_cast<unsigned short>(face.mIndices[0]);
            dst.indices[f * 3 + 1] = static_cast<unsigned short>(face.mIndices[1]);
            dst.indices[f * 3 + 2] = static_cast<unsigned short>(face.mIndices[2]);
        }

        model.meshMaterial[i] = static_cast<int>(sourceMesh->mMaterialIndex);

        const char* meshName = sourceMesh->mName.length > 0 ? sourceMesh->mName.C_Str() : "unnamed";
        TraceLog(LogLevel::Trace, "MODEL", TextFormat("[Vulkan] Mesh #%d ('%s'): %d vertices, %d triangles, Material: #%d",
            i, meshName, dst.vertexCount, dst.triangleCount, model.meshMaterial[i]));
    }

    TraceLog(LogLevel::Info, "MODEL", TextFormat("[Vulkan] Model loaded successfully: %s (%d meshes, %d materials, %d total vertices, %d total triangles)",
        filePath ? filePath : "<null>", model.meshCount, model.materialCount, totalVertices, totalTriangles));
    return model;
}

void QuarkVkRenderer::UnloadModel(Model& model) {
    if (model.meshes) {
        for (int i = 0; i < model.meshCount; ++i) {
            UnloadMesh(model.meshes[i]);
        }
        delete[] model.meshes;
        model.meshes = nullptr;
    }

    if (model.materials) {
        for (int i = 0; i < model.materialCount; ++i) {
            Material& material = model.materials[i];
            if (material.maps) {
                std::set<uint32_t> unloadedTextures;
                for (int mapIndex = 0; mapIndex <= MATERIAL_MAP_BRDF; ++mapIndex) {
                    const Texture2D& mapTexture = material.maps[mapIndex].texture;
                    if (mapTexture.valid && unloadedTextures.insert(mapTexture.id).second) {
                        ITexture texture{ mapTexture.id, mapTexture.width, mapTexture.height, mapTexture.valid };
                        UnloadTexture(texture);
                    }
                }
                delete[] material.maps;
                material.maps = nullptr;
            }
        }
        delete[] model.materials;
        model.materials = nullptr;
    }

    delete[] model.meshMaterial;
    model.meshMaterial = nullptr;

    delete[] model.skeleton.bones;
    model.skeleton.bones = nullptr;
    delete[] model.skeleton.bindPose.transform;
    model.skeleton.bindPose.transform = nullptr;
    delete[] model.currentPose.transform;
    model.currentPose.transform = nullptr;
    delete[] model.boneMatrices;
    model.boneMatrices = nullptr;

    TraceLog(LogLevel::Info, "MODEL", TextFormat("[Vulkan] Model unloaded (%d meshes, %d materials)", model.meshCount, model.materialCount));

    model.meshCount = 0;
    model.materialCount = 0;
    model.directory.clear();
    model.id = 0;
    model.transform = Mat4::identity();
}

void QuarkVkRenderer::DrawModel(const Model& model, const Vec3& position, float scale,
                                float rotationX, float rotationY, float rotationZ) {
    Mat4 transform = Mat4::translation(position.x, position.y, position.z)
                   * Mat4::rotationY(rotationY)
                   * Mat4::rotationX(rotationX)
                   * Mat4::rotationZ(rotationZ)
                   * Mat4::scale(scale, scale, scale);
    DrawModelEx(model, transform);
}

void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform) {
    const Mat4 modelTransform = transform * model.transform;

    for (int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];
        const Material& material = (model.meshMaterial && i >= 0 && i < model.meshCount &&
                                    model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount)
            ? model.materials[model.meshMaterial[i]]
            : Material{};
        DrawMesh(mesh, material, modelTransform);
    }
}

void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint) {
    const Mat4 modelTransform = transform * model.transform;
    std::array<MaterialMap, MATERIAL_MAP_BRDF + 1> adjustedMaps{};

    for (int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];

        Material adjustedMaterial{};
        const Material* sourceMaterial = nullptr;
        if (model.meshMaterial && i >= 0 && i < model.meshCount &&
            model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount) {
            sourceMaterial = &model.materials[model.meshMaterial[i]];
        }

        if (sourceMaterial && sourceMaterial->maps) {
            std::copy(sourceMaterial->maps, sourceMaterial->maps + adjustedMaps.size(), adjustedMaps.begin());
            adjustedMaterial = *sourceMaterial;
            adjustedMaterial.maps = adjustedMaps.data();
            adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color =
                MultiplyColor(adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color, tint);
        } else {
            adjustedMaterial.maps = adjustedMaps.data();
            adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color = tint;
        }

        DrawMesh(mesh, adjustedMaterial, modelTransform);
    }
}

void QuarkVkRenderer::UploadMesh(Mesh& mesh, bool dynamic) {
    (void)dynamic;

    mesh.vaoId = 0;
    mesh.vboId = 0;
    mesh.eboId = 0;

    if (mesh.vertexCount < 0) mesh.vertexCount = 0;
    if (mesh.triangleCount < 0) mesh.triangleCount = 0;

    TraceLog(LogLevel::Trace, "MESH", TextFormat("[Vulkan] Uploaded mesh to CPU staging (%d vertices, %d triangles)", mesh.vertexCount, mesh.triangleCount));
}

void QuarkVkRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* data, int dataSize, int offset) {
    if (!data || dataSize <= 0 || offset < 0) {
        return;
    }

    auto copyBytes = [&](void* dst, std::size_t dstBytes) {
        if (!dst || static_cast<std::size_t>(offset) >= dstBytes) {
            return;
        }
        const std::size_t bytesToCopy = std::min<std::size_t>(
            static_cast<std::size_t>(dataSize),
            dstBytes - static_cast<std::size_t>(offset)
        );
        std::memcpy(static_cast<unsigned char*>(dst) + offset, data, bytesToCopy);
    };

    switch (index) {
    case 0:
        copyBytes(mesh.vertices, static_cast<std::size_t>(mesh.vertexCount) * 3u * sizeof(float));
        break;
    case 1:
        copyBytes(mesh.normals, static_cast<std::size_t>(mesh.vertexCount) * 3u * sizeof(float));
        break;
    case 2:
        copyBytes(mesh.texcoords, static_cast<std::size_t>(mesh.vertexCount) * 2u * sizeof(float));
        break;
    case 6:
        copyBytes(mesh.indices, static_cast<std::size_t>(mesh.triangleCount) * 3u * sizeof(unsigned short));
        break;
    default:
        break;
    }
}

void QuarkVkRenderer::UnloadMesh(Mesh& mesh) {
    TraceLog(LogLevel::Trace, "MESH", TextFormat("[Vulkan] Unloaded mesh (%d vertices, %d triangles)", mesh.vertexCount, mesh.triangleCount));
    FreeMeshCpuData(mesh);
    mesh.vaoId = 0;
    mesh.vboId = 0;
    mesh.eboId = 0;
}

void QuarkVkRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform) {
    if (!mesh.vertices || mesh.vertexCount <= 0) {
        return;
    }

    const MaterialMap* albedoMap = (material.maps != nullptr) ? &material.maps[MATERIAL_MAP_ALBEDO] : nullptr;
    Color baseColor = WHITE;
    if (albedoMap) {
        baseColor = albedoMap->color;
    }

    const Mat4 finalTransform = m_currentMatrix * transform;
    auto& vertices = GetActive3DTriangleVertices();
    auto* drawItems = &m_main3DBatch.drawItems;
    if (m_activeRenderTargetId != 0) {
        auto renderTargetIt = m_renderTargets.find(m_activeRenderTargetId);
        if (renderTargetIt != m_renderTargets.end()) {
            drawItems = &renderTargetIt->second.drawItems3D;
        }
    }
    const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
    const VkDescriptorSet materialDescriptorSet = CreateMaterialDescriptorSet(material);
    const auto finishDraw = [&]() {
        const uint32_t vertexCount = static_cast<uint32_t>(vertices.size()) - firstVertex;
        if (vertexCount > 0) {
            drawItems->push_back({ materialDescriptorSet, m_vkShaderCompiler.CurrentProgramId(), firstVertex, vertexCount });
        }
    };

    auto pushVertex = [&](int vertexIndex, Color color) {
        const Vec4 world = finalTransform * Vec4{
            mesh.vertices[vertexIndex * 3 + 0],
            mesh.vertices[vertexIndex * 3 + 1],
            mesh.vertices[vertexIndex * 3 + 2],
            1.0f
        };
        const Vec4 clip = m_projectionMatrix * (m_viewMatrix * world);
        Vec4 normal = finalTransform * Vec4{
            mesh.normals ? mesh.normals[vertexIndex * 3 + 0] : 0.0f,
            mesh.normals ? mesh.normals[vertexIndex * 3 + 1] : 1.0f,
            mesh.normals ? mesh.normals[vertexIndex * 3 + 2] : 0.0f,
            0.0f
        };
        const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (normalLength > 0.0f) {
            normal.x /= normalLength;
            normal.y /= normalLength;
            normal.z /= normalLength;
        }
        vertices.push_back(Vk3DVertex{
            clip.x,
            clip.y,
            clip.z,
            clip.w,
            mesh.texcoords ? mesh.texcoords[vertexIndex * 2 + 0] : 0.0f,
            mesh.texcoords ? mesh.texcoords[vertexIndex * 2 + 1] : 0.0f,
            NormalizeColorComponent(color.r),
            NormalizeColorComponent(color.g),
            NormalizeColorComponent(color.b),
            NormalizeColorComponent(color.a),
            normal.x,
            normal.y,
            normal.z,
            0.0f,
            world.x, world.y, world.z, 1.0f
        });
    };

    if (vertices.empty() && m_activeRenderTargetId == 0) {
        m_main3DBatch.shaderProgramId = m_vkShaderCompiler.CurrentProgramId();
    }

    auto pushTriangle = [&](int i0, int i1, int i2) {
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount) {
            return;
        }

        const Color c0 = MultiplyColor(baseColor, ReadVertexColor(mesh, i0, WHITE));
        const Color c1 = MultiplyColor(baseColor, ReadVertexColor(mesh, i1, WHITE));
        const Color c2 = MultiplyColor(baseColor, ReadVertexColor(mesh, i2, WHITE));

        pushVertex(i0, c0);
        pushVertex(i1, c1);
        pushVertex(i2, c2);
    };

    if (mesh.indices && mesh.triangleCount > 0) {
        for (int tri = 0; tri < mesh.triangleCount; ++tri) {
            const int i0 = static_cast<int>(mesh.indices[tri * 3 + 0]);
            const int i1 = static_cast<int>(mesh.indices[tri * 3 + 1]);
            const int i2 = static_cast<int>(mesh.indices[tri * 3 + 2]);
            pushTriangle(i0, i1, i2);
        }
        finishDraw();
        return;
    }

    for (int i = 0; i + 2 < mesh.vertexCount; i += 3) {
        pushTriangle(i, i + 1, i + 2);
    }

    finishDraw();
}

void QuarkVkRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* transforms, int instances) {
    if (!transforms || instances <= 0) {
        return;
    }

    for (int i = 0; i < instances; ++i) {
        DrawMesh(mesh, material, transforms[i]);
    }
}

}; // namespace qc