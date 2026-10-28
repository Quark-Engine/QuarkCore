#include "QuarkVkRenderer.hpp"

#include "../QuarkAssimpTexture.hpp"
#include "../../QuarkModelAnim.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <set>
#include <string>
#include <vector>
namespace qci {
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

static void ensure_mesh_tangents(Mesh& mesh, bool forceRebuild = false) {
    if (!mesh.vertices || mesh.vertexCount <= 0) return;
    if (mesh.tangents && !forceRebuild) return;
    if (!mesh.tangents)
        mesh.tangents = new float[static_cast<size_t>(mesh.vertexCount) * 3u];

    for (int i = 0; i < mesh.vertexCount * 3; ++i) mesh.tangents[i] = 0.0f;

    const bool indexed = mesh.indices && mesh.triangleCount > 0;
    const int triangleCount = mesh.triangleCount > 0
        ? mesh.triangleCount
        : static_cast<int>(mesh.vertexCount / 3);

    for (int t = 0; t < triangleCount; ++t) {
        const int ia = indexed ? mesh.indices[t * 3 + 0] : t * 3 + 0;
        const int ib = indexed ? mesh.indices[t * 3 + 1] : t * 3 + 1;
        const int ic = indexed ? mesh.indices[t * 3 + 2] : t * 3 + 2;
        if (ia < 0 || ib < 0 || ic < 0 ||
            ia >= mesh.vertexCount || ib >= mesh.vertexCount || ic >= mesh.vertexCount)
            continue;

        const float* v0 = mesh.vertices + ia * 3;
        const float* v1 = mesh.vertices + ib * 3;
        const float* v2 = mesh.vertices + ic * 3;

        const float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
        const float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];

        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        if (mesh.texcoords) {
            const float u0 = mesh.texcoords[ia * 2 + 0];
            const float vt0 = mesh.texcoords[ia * 2 + 1];
            const float u1 = mesh.texcoords[ib * 2 + 0];
            const float vt1 = mesh.texcoords[ib * 2 + 1];
            const float u2 = mesh.texcoords[ic * 2 + 0];
            const float vt2 = mesh.texcoords[ic * 2 + 1];

            const float du1 = u1 - u0, dv1 = vt1 - vt0;
            const float du2 = u2 - u0, dv2 = vt2 - vt0;
            const float determinant = du1 * dv2 - du2 * dv1;
            if (fabsf(determinant) > 1e-8f) {
                const float r = 1.0f / determinant;
                tx = (e1x * dv2 - e2x * dv1) * r;
                ty = (e1y * dv2 - e2y * dv1) * r;
                tz = (e1z * dv2 - e2z * dv1) * r;
            }
        }
        if (tx == 0.0f && ty == 0.0f && tz == 0.0f) tx = 1.0f;

        const int idx[3] = { ia, ib, ic };
        for (int k = 0; k < 3; ++k) {
            float* dst = mesh.tangents + idx[k] * 3;
            dst[0] += tx;
            dst[1] += ty;
            dst[2] += tz;
        }
    }

    for (int i = 0; i < mesh.vertexCount; ++i) {
        float* t = mesh.tangents + i * 3;
        const float* n = mesh.normals ? mesh.normals + i * 3 : nullptr;
        if (n) {
            const float nx = n[0], ny = n[1], nz = n[2];
            const float nn = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nn > 1e-9f) {
                const float inv = 1.0f / nn;
                const float anx = nx * inv, any = ny * inv, anz = nz * inv;
                const float d = t[0] * anx + t[1] * any + t[2] * anz;
                t[0] -= anx * d;
                t[1] -= any * d;
                t[2] -= anz * d;
            }
        }
        const float total = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (total > 1e-8f) {
            const float inv = 1.0f / total;
            t[0] *= inv;
            t[1] *= inv;
            t[2] *= inv;
        } else {
            t[0] = 1.0f;
            t[1] = 0.0f;
            t[2] = 0.0f;
        }
    }
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
    delete[] mesh.boneOffsets;
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
    mesh.boneOffsets = nullptr;
    mesh.boneOffsetCount = 0;
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

static Material LoadAssimpMaterial(QuarkVkRenderer& renderer, const aiScene& scene,
                                   const char* filePath, aiMaterial* source) {
    Material material{};
    material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
    material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;

    if (!source) {
        return material;
    }

    aiColor4D baseColor{};
    if (AI_SUCCESS == aiGetMaterialColor(source, AI_MATKEY_BASE_COLOR, &baseColor) ||
        AI_SUCCESS == aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &baseColor)) {
        material.maps[MATERIAL_MAP_ALBEDO].color = Color{
            static_cast<std::uint8_t>(std::clamp(baseColor.r, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(baseColor.g, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(baseColor.b, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(baseColor.a, 0.0f, 1.0f) * 255.0f)
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

        const std::string textureReference = texturePath.C_Str();
        std::string resolvedPath = GetModelDirectory(filePath);
        resolvedPath += textureReference;
        TraceLog(LogLevel::Trace, "MODEL", TextFormat("[Vulkan] Model material map %d: %s", mapIndex, resolvedPath.c_str()));

        ITexture loadedTex{};
        if (!textureReference.empty() && textureReference.front() == '*') {
            Image embeddedImage{};
            std::vector<unsigned char> rawPixels;
            bool ownsImage = false;
            if (DecodeEmbeddedAssimpTexture(scene, texturePath, embeddedImage, rawPixels, ownsImage)) {
                loadedTex = renderer.LoadTextureFromImage(embeddedImage);
                ReleaseEmbeddedAssimpTexture(embeddedImage, ownsImage);
            } else {
                TraceLog(LogLevel::Error, "MODEL", TextFormat("[Vulkan] Failed to decode embedded texture: %s", textureReference.c_str()));
            }
        } else {
            loadedTex = renderer.LoadTexture(resolvedPath.c_str());
        }
        if (loadedTex.valid) {
            material.maps[mapIndex].texture.id = loadedTex.id;
            material.maps[mapIndex].texture.width = loadedTex.width;
            material.maps[mapIndex].texture.height = loadedTex.height;
            material.maps[mapIndex].texture.mipmaps = loadedTex.mipmaps;
            material.maps[mapIndex].texture.format = loadedTex.format;
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
    const aiScene* scene = nullptr;
    try {
        scene = importer.ReadFile(
            filePath,
            aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs |
                aiProcess_PopulateArmatureData
        );
    } catch (const std::exception& error) {
        TraceLog(LogLevel::Error, "MODEL", TextFormat("[Vulkan] Exception while loading model %s: %s",
            filePath ? filePath : "<null>", error.what()));
        return Model{};
    }

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
        model.materials[i] = LoadAssimpMaterial(*this, *scene, filePath, scene->mMaterials[i]);
    }

    qcPopulateModelSkeleton(scene, model);

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
        dst.boneIndices = (dst.vertexCount > 0) ? new unsigned char[static_cast<size_t>(dst.vertexCount) * 4u]{} : nullptr;
        dst.boneWeights = (dst.vertexCount > 0) ? new float[static_cast<size_t>(dst.vertexCount) * 4u]{} : nullptr;

        for (unsigned int boneIndex = 0; boneIndex < sourceMesh->mNumBones; ++boneIndex) {
            const aiBone* bone = sourceMesh->mBones[boneIndex];
            const int skeletonBoneIndex = qcFindSkeletonBoneIndex(scene, model, bone);
            if (skeletonBoneIndex < 0 || skeletonBoneIndex > 255) {
                TraceLog(LogLevel::Warn, "MODEL",
                         TextFormat("[Vulkan] Mesh bone '%s' is not present in the model skeleton or exceeds the 8-bit bone index range",
                                    bone->mName.C_Str()));
                continue;
            }
            qcSetMeshBoneOffset(dst, static_cast<unsigned int>(skeletonBoneIndex),
                                model.skeleton.boneCount, bone->mOffsetMatrix);
            for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];
                const unsigned int vertexIndex = weight.mVertexId;
                float* dstWeights = dst.boneWeights + static_cast<size_t>(vertexIndex) * 4u;
                unsigned char* dstBones = dst.boneIndices + static_cast<size_t>(vertexIndex) * 4u;
                for (int slot = 0; slot < 4; ++slot) {
                    if (dstWeights[slot] <= 0.0f) {
                        dstBones[slot] = static_cast<unsigned char>(skeletonBoneIndex);
                        dstWeights[slot] = weight.mWeight;
                        break;
                    }
                }
            }
        }

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

        ensure_mesh_tangents(dst);
        UploadMesh(dst, false);

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
    const int meshCount = model.meshCount;
    const int materialCount = model.materialCount;
    if (m_device != VK_NULL_HANDLE && (model.meshes != nullptr || model.materials != nullptr)) {
        vkDeviceWaitIdle(m_device);
    }
    if (model.meshes) {
        for (int i = 0; i < meshCount; ++i) {
            UnloadMesh(model.meshes[i]);
        }
        delete[] model.meshes;
        model.meshes = nullptr;
    }

    if (model.materials) {
        for (int i = 0; i < materialCount; ++i) {
            Material& material = model.materials[i];
            if (material.maps) {
                std::set<uint32_t> unloadedTextures;
                for (int mapIndex = 0; mapIndex <= MATERIAL_MAP_BRDF; ++mapIndex) {
                    const Texture2D& mapTexture = material.maps[mapIndex].texture;
                    if (mapTexture.valid && unloadedTextures.insert(mapTexture.id).second) {
                        ITexture texture{ mapTexture.id, mapTexture.width, mapTexture.height, mapTexture.mipmaps, mapTexture.format, mapTexture.valid };
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

    qcFreeModelSkeleton(model);

    TraceLog(LogLevel::Info, "MODEL", TextFormat("[Vulkan] Model unloaded (%d meshes, %d materials)", meshCount, materialCount));
    model = {};
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
    mesh.vaoId = 0;
    mesh.vboId = 0;
    mesh.eboId = 0;

    if (mesh.vertexCount < 0) mesh.vertexCount = 0;
    if (mesh.triangleCount < 0) mesh.triangleCount = 0;
    ensure_mesh_tangents(mesh);

    if (m_gpuMeshes.find(mesh.vertices) != m_gpuMeshes.end()) {
        if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
        DestroyStaticGpuMesh(mesh);
    }
    if (dynamic) {
        m_dynamicMeshes.insert(mesh.vertices);
    } else {
        m_dynamicMeshes.erase(mesh.vertices);
        if (mesh.vertices && !UploadStaticGpuMesh(mesh)) {
            TraceLog(LogLevel::Warn, "MESH", "[Vulkan] Static mesh GPU upload failed; CPU transform path will be used.");
        }
    }

    TraceLog(LogLevel::Trace, "MESH", TextFormat("[Vulkan] Uploaded mesh (%d vertices, %d triangles)", mesh.vertexCount, mesh.triangleCount));
}

void QuarkVkRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* data, int dataSize, int offset) {
    if (!data || dataSize <= 0 || offset < 0) {
        return;
    }

    const bool hadStaticGpuMesh = m_gpuMeshes.find(mesh.vertices) != m_gpuMeshes.end();
    if (hadStaticGpuMesh && m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        DestroyStaticGpuMesh(mesh);
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

    if (index == 0 || index == 1 || index == 2 || index == 6) {
        ensure_mesh_tangents(mesh, true);
        if (hadStaticGpuMesh && m_dynamicMeshes.find(mesh.vertices) == m_dynamicMeshes.end() &&
            !UploadStaticGpuMesh(mesh)) {
            TraceLog(LogLevel::Warn, "MESH", "[Vulkan] Updated static mesh GPU upload failed; CPU transform path will be used.");
        }
    }
}

void QuarkVkRenderer::UnloadMesh(Mesh& mesh) {
    if (m_gpuMeshes.find(mesh.vertices) != m_gpuMeshes.end() && m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    DestroyStaticGpuMesh(mesh);
    m_dynamicMeshes.erase(mesh.vertices);
    TraceLog(LogLevel::Trace, "MESH", TextFormat("[Vulkan] Unloaded mesh (%d vertices, %d triangles)", mesh.vertexCount, mesh.triangleCount));
    FreeMeshCpuData(mesh);
    mesh.vaoId = 0;
    mesh.vboId = 0;
    mesh.eboId = 0;
}

bool QuarkVkRenderer::UploadStaticGpuMesh(const Mesh& mesh) {
    if (!mesh.vertices || mesh.vertexCount <= 0 || m_device == VK_NULL_HANDLE ||
        !m_vkCommandContext.IsInitialized() || m_graphicsQueue == VK_NULL_HANDLE) {
        return false;
    }

    std::vector<VkGpuMeshVertex> vertices(static_cast<size_t>(mesh.vertexCount));
    for (int i = 0; i < mesh.vertexCount; ++i) {
        const Color color = ReadVertexColor(mesh, i, WHITE);
        VkGpuMeshVertex& vertex = vertices[static_cast<size_t>(i)];
        vertex.x = mesh.vertices[i * 3 + 0];
        vertex.y = mesh.vertices[i * 3 + 1];
        vertex.z = mesh.vertices[i * 3 + 2];
        vertex.w = 1.0f;
        vertex.u = mesh.texcoords ? mesh.texcoords[i * 2 + 0] : 0.0f;
        vertex.v = mesh.texcoords ? mesh.texcoords[i * 2 + 1] : 0.0f;
        vertex.r = NormalizeColorComponent(color.r);
        vertex.g = NormalizeColorComponent(color.g);
        vertex.b = NormalizeColorComponent(color.b);
        vertex.a = NormalizeColorComponent(color.a);
        vertex.nx = mesh.normals ? mesh.normals[i * 3 + 0] : 0.0f;
        vertex.ny = mesh.normals ? mesh.normals[i * 3 + 1] : 1.0f;
        vertex.nz = mesh.normals ? mesh.normals[i * 3 + 2] : 0.0f;
        vertex.nw = 0.0f;
        vertex.wx = 0.0f;
        vertex.wy = 0.0f;
        vertex.wz = 0.0f;
        vertex.ww = 1.0f;
        vertex.tx = mesh.tangents ? mesh.tangents[i * 3 + 0] : 0.0f;
        vertex.ty = mesh.tangents ? mesh.tangents[i * 3 + 1] : 0.0f;
        vertex.tz = mesh.tangents ? mesh.tangents[i * 3 + 2] : 0.0f;
        vertex.tw = mesh.tangents ? 1.0f : 0.0f;
    }

    std::vector<uint32_t> indices;
    const size_t requestedIndexCount = mesh.indices && mesh.triangleCount > 0
        ? static_cast<size_t>(mesh.triangleCount) * 3u
        : static_cast<size_t>(mesh.vertexCount / 3) * 3u;
    indices.reserve(requestedIndexCount);
    if (mesh.indices && mesh.triangleCount > 0) {
        for (size_t i = 0; i + 2 < requestedIndexCount; i += 3) {
            const uint32_t i0 = mesh.indices[i + 0];
            const uint32_t i1 = mesh.indices[i + 1];
            const uint32_t i2 = mesh.indices[i + 2];
            if (i0 >= static_cast<uint32_t>(mesh.vertexCount) ||
                i1 >= static_cast<uint32_t>(mesh.vertexCount) ||
                i2 >= static_cast<uint32_t>(mesh.vertexCount)) {
                continue;
            }
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
        }
    } else {
        for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(mesh.vertexCount); i += 3) {
            indices.push_back(i + 0);
            indices.push_back(i + 1);
            indices.push_back(i + 2);
        }
    }
    if (indices.empty()) return false;

    VkGpuMeshData gpuMesh{};
    VkBuffer vertexStaging = VK_NULL_HANDLE;
    VmaAllocation vertexStagingAllocation = VK_NULL_HANDLE;
    VkBuffer indexStaging = VK_NULL_HANDLE;
    VmaAllocation indexStagingAllocation = VK_NULL_HANDLE;
    VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
    auto cleanupStaging = [&]() {
        if (vertexStaging != VK_NULL_HANDLE) {
            m_gpuAllocator.DestroyBuffer(vertexStaging, vertexStagingAllocation);
            vertexStaging = VK_NULL_HANDLE;
        }
        if (indexStaging != VK_NULL_HANDLE) {
            m_gpuAllocator.DestroyBuffer(indexStaging, indexStagingAllocation);
            indexStaging = VK_NULL_HANDLE;
        }
    };
    auto cleanupGpuMesh = [&]() {
        if (gpuMesh.vertexBuffer != VK_NULL_HANDLE)
            m_gpuAllocator.DestroyBuffer(gpuMesh.vertexBuffer, gpuMesh.vertexAllocation);
        if (gpuMesh.indexBuffer != VK_NULL_HANDLE)
            m_gpuAllocator.DestroyBuffer(gpuMesh.indexBuffer, gpuMesh.indexAllocation);
    };

    const VkDeviceSize vertexBytes = vertices.size() * sizeof(VkGpuMeshVertex);
    const VkDeviceSize indexBytes = indices.size() * sizeof(uint32_t);
    const VmaAllocationCreateFlags stagingFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (!m_gpuAllocator.CreateBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VMA_MEMORY_USAGE_AUTO, vertexStaging, vertexStagingAllocation, stagingFlags) ||
        !m_gpuAllocator.CreateBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VMA_MEMORY_USAGE_AUTO, indexStaging, indexStagingAllocation, stagingFlags) ||
        !m_gpuAllocator.CreateBuffer(vertexBytes,
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                     VMA_MEMORY_USAGE_GPU_ONLY, gpuMesh.vertexBuffer, gpuMesh.vertexAllocation) ||
        !m_gpuAllocator.CreateBuffer(indexBytes,
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                     VMA_MEMORY_USAGE_GPU_ONLY, gpuMesh.indexBuffer, gpuMesh.indexAllocation)) {
        cleanupStaging();
        cleanupGpuMesh();
        return false;
    }

    VmaAllocationInfo vertexStagingInfo{};
    VmaAllocationInfo indexStagingInfo{};
    vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), vertexStagingAllocation, &vertexStagingInfo);
    vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), indexStagingAllocation, &indexStagingInfo);
    if (!vertexStagingInfo.pMappedData || !indexStagingInfo.pMappedData) {
        cleanupStaging();
        cleanupGpuMesh();
        return false;
    }
    std::memcpy(vertexStagingInfo.pMappedData, vertices.data(), static_cast<size_t>(vertexBytes));
    std::memcpy(indexStagingInfo.pMappedData, indices.data(), static_cast<size_t>(indexBytes));
    vmaFlushAllocation(m_gpuAllocator.GetAllocator(), vertexStagingAllocation, 0, vertexBytes);
    vmaFlushAllocation(m_gpuAllocator.GetAllocator(), indexStagingAllocation, 0, indexBytes);

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_vkCommandContext.Pool();
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &allocateInfo, &uploadCommandBuffer) != VK_SUCCESS) {
        cleanupStaging();
        cleanupGpuMesh();
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool uploadSucceeded = vkBeginCommandBuffer(uploadCommandBuffer, &beginInfo) == VK_SUCCESS;
    if (uploadSucceeded) {
        VkBufferCopy vertexCopy{};
        vertexCopy.size = vertexBytes;
        vkCmdCopyBuffer(uploadCommandBuffer, vertexStaging, gpuMesh.vertexBuffer, 1, &vertexCopy);
        VkBufferCopy indexCopy{};
        indexCopy.size = indexBytes;
        vkCmdCopyBuffer(uploadCommandBuffer, indexStaging, gpuMesh.indexBuffer, 1, &indexCopy);

        std::array<VkBufferMemoryBarrier, 2> barriers{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = gpuMesh.vertexBuffer;
        barriers[0].size = VK_WHOLE_SIZE;
        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = gpuMesh.indexBuffer;
        barriers[1].size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr,
                             static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
        uploadSucceeded = vkEndCommandBuffer(uploadCommandBuffer) == VK_SUCCESS;
    }

    if (uploadSucceeded) {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadCommandBuffer;
        uploadSucceeded = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS &&
                          vkQueueWaitIdle(m_graphicsQueue) == VK_SUCCESS;
    }
    vkFreeCommandBuffers(m_device, m_vkCommandContext.Pool(), 1, &uploadCommandBuffer);
    cleanupStaging();
    if (!uploadSucceeded) {
        cleanupGpuMesh();
        return false;
    }

    gpuMesh.indexCount = static_cast<uint32_t>(indices.size());
    m_gpuMeshes[mesh.vertices] = gpuMesh;
    return true;
}

void QuarkVkRenderer::DestroyStaticGpuMesh(const Mesh& mesh) {
    const auto it = m_gpuMeshes.find(mesh.vertices);
    if (it == m_gpuMeshes.end()) return;
    if (it->second.vertexBuffer != VK_NULL_HANDLE)
        m_gpuAllocator.DestroyBuffer(it->second.vertexBuffer, it->second.vertexAllocation);
    if (it->second.indexBuffer != VK_NULL_HANDLE)
        m_gpuAllocator.DestroyBuffer(it->second.indexBuffer, it->second.indexAllocation);
    m_gpuMeshes.erase(it);
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
    const Mat4 modelViewProjection = m_projectionMatrix * m_viewMatrix * finalTransform;
    auto& vertices = GetActive3DTriangleVertices();
    auto& indices = GetActive3DTriangleIndices();
    auto* drawItems = &m_main3DBatch.drawItems;
    if (m_activeRenderTargetId != 0) {
        auto renderTargetIt = m_renderTargets.find(m_activeRenderTargetId);
        if (renderTargetIt != m_renderTargets.end()) {
            drawItems = &renderTargetIt->second.drawItems3D;
        }
    }
    const bool startsMain3DGeometry = vertices.empty() && m_activeRenderTargetId == 0;
    const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
    const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
    const VkDescriptorSet materialDescriptorSet = CreateMaterialDescriptorSet(material);
    const uint32_t shaderProgramId = m_vkShaderCompiler.CurrentProgramId();
    const VkShaderProgramData* shaderProgram = m_vkShaderCompiler.GetProgram(shaderProgramId);
    const uint32_t drawShaderProgramId = shaderProgramId;
    const auto gpuMeshIt = m_gpuMeshes.find(mesh.vertices);
    if (shaderProgram && shaderProgram->supportsGpuMeshTransform &&
        gpuMeshIt != m_gpuMeshes.end() && gpuMeshIt->second.indexCount > 0 &&
        materialDescriptorSet != VK_NULL_HANDLE) {
        if (startsMain3DGeometry) {
            m_main3DBatch.shaderProgramId = shaderProgramId;
        }
        Vk3DDrawItem drawItem{};
        drawItem.descriptorSet = materialDescriptorSet;
        drawItem.shaderProgramId = shaderProgramId;
        drawItem.firstVertex = firstVertex;
        drawItem.indexCount = gpuMeshIt->second.indexCount;
        drawItem.indexed = true;
        drawItem.gpuMesh = true;
        drawItem.gpuVertexBuffer = gpuMeshIt->second.vertexBuffer;
        drawItem.gpuIndexBuffer = gpuMeshIt->second.indexBuffer;
        drawItem.modelViewProjection = modelViewProjection;
        drawItem.model = finalTransform;
        drawItem.materialColor = {
            NormalizeColorComponent(baseColor.r),
            NormalizeColorComponent(baseColor.g),
            NormalizeColorComponent(baseColor.b),
            NormalizeColorComponent(baseColor.a)
        };
        drawItems->push_back(drawItem);
        return;
    }

    auto transformVertex = [&](int vertexIndex, Color color) {
        const Vec4 localPosition{
            mesh.vertices[vertexIndex * 3 + 0],
            mesh.vertices[vertexIndex * 3 + 1],
            mesh.vertices[vertexIndex * 3 + 2],
            1.0f
        };
        const Vec4 world = finalTransform * localPosition;
        const Vec4 clip = modelViewProjection * localPosition;
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
        Vec4 tangent{0.0f, 0.0f, 0.0f, 0.0f};
        if (mesh.tangents) {
            tangent = finalTransform * Vec4{
                mesh.tangents[vertexIndex * 3 + 0],
                mesh.tangents[vertexIndex * 3 + 1],
                mesh.tangents[vertexIndex * 3 + 2],
                0.0f
            };
            const float tangentLength = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
            if (tangentLength > 0.0f) {
                tangent.x /= tangentLength;
                tangent.y /= tangentLength;
                tangent.z /= tangentLength;
                tangent.w = 1.0f;
            }
        }
        return Vk3DVertex{
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
            world.x, world.y, world.z, 1.0f,
            tangent.x,
            tangent.y,
            tangent.z,
            tangent.w
        };
    };

    for (int vertexIndex = 0; vertexIndex < mesh.vertexCount; ++vertexIndex) {
        const Color vertexColor = MultiplyColor(baseColor,
            ReadVertexColor(mesh, vertexIndex, WHITE));
        vertices.push_back(transformVertex(vertexIndex, vertexColor));
    }

    if (mesh.indices && mesh.triangleCount > 0) {
        for (int tri = 0; tri < mesh.triangleCount; ++tri) {
            const int i0 = static_cast<int>(mesh.indices[tri * 3 + 0]);
            const int i1 = static_cast<int>(mesh.indices[tri * 3 + 1]);
            const int i2 = static_cast<int>(mesh.indices[tri * 3 + 2]);
            if (i0 < 0 || i1 < 0 || i2 < 0 ||
                i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount) {
                continue;
            }
            indices.push_back(static_cast<uint32_t>(i0));
            indices.push_back(static_cast<uint32_t>(i1));
            indices.push_back(static_cast<uint32_t>(i2));
        }
    } else {
        for (int index = 0; index + 2 < mesh.vertexCount; index += 3) {
            indices.push_back(static_cast<uint32_t>(index));
            indices.push_back(static_cast<uint32_t>(index + 1));
            indices.push_back(static_cast<uint32_t>(index + 2));
        }
    }

    const uint32_t indexCount = static_cast<uint32_t>(indices.size()) - firstIndex;
    if (indexCount == 0) {
        vertices.resize(firstVertex);
        indices.resize(firstIndex);
        return;
    }
    if (startsMain3DGeometry) {
        m_main3DBatch.shaderProgramId = drawShaderProgramId;
    }
    drawItems->push_back({ materialDescriptorSet, drawShaderProgramId,
        firstVertex, static_cast<uint32_t>(mesh.vertexCount), firstIndex, indexCount, true });
}

void QuarkVkRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* transforms, int instances) {
    if (!transforms || instances <= 0) {
        return;
    }

    for (int i = 0; i < instances; ++i) {
        DrawMesh(mesh, material, transforms[i]);
    }
}
} // namespace qci
