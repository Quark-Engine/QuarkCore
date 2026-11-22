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

namespace qci
{
    namespace
    {
        static float NormalizeColorComponent(std::uint8_t value)
        {
            return static_cast<float>(value) / 255.0f;
        }

        static Color MultiplyColor(Color lhs, Color rhs)
        {
            return Color{
                static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.r) * rhs.r) / 255u),
                static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.g) * rhs.g) / 255u),
                static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.b) * rhs.b) / 255u),
                static_cast<std::uint8_t>((static_cast<unsigned int>(lhs.a) * rhs.a) / 255u)};
        }

        static void ensure_mesh_tangents(Mesh& mesh, bool forceRebuild = false)
        {
            if (!mesh.vertices || mesh.vertexCount <= 0)
                return;
            if (mesh.tangents && !forceRebuild)
                return;
            if (!mesh.tangents)
                mesh.tangents = new float[static_cast<size_t>(mesh.vertexCount) * 3u];

            for (int i = 0; i < mesh.vertexCount * 3; ++i)
                mesh.tangents[i] = 0.0f;

            const bool indexed = mesh.indices && mesh.triangleCount > 0;
            const int triangleCount = mesh.triangleCount > 0
                                          ? mesh.triangleCount
                                          : static_cast<int>(mesh.vertexCount / 3);

            for (int pT = 0; pT < triangleCount; ++pT)
            {
                const int ia = indexed ? mesh.indices[pT * 3 + 0] : pT * 3 + 0;
                const int ib = indexed ? mesh.indices[pT * 3 + 1] : pT * 3 + 1;
                const int ic = indexed ? mesh.indices[pT * 3 + 2] : pT * 3 + 2;
                if (ia < 0 || ib < 0 || ic < 0 ||
                    ia >= mesh.vertexCount || ib >= mesh.vertexCount || ic >= mesh.vertexCount)
                    continue;

                const float* pV0 = mesh.vertices + ia * 3;
                const float* pV1 = mesh.vertices + ib * 3;
                const float* pV2 = mesh.vertices + ic * 3;

                const float e1x = pV1[0] - pV0[0], e1y = pV1[1] - pV0[1], e1z = pV1[2] - pV0[2];
                const float e2x = pV2[0] - pV0[0], e2y = pV2[1] - pV0[1], e2z = pV2[2] - pV0[2];

                float tx = 0.0f, ty = 0.0f, tz = 0.0f;
                if (mesh.texcoords)
                {
                    const float u0 = mesh.texcoords[ia * 2 + 0];
                    const float vt0 = mesh.texcoords[ia * 2 + 1];
                    const float u1 = mesh.texcoords[ib * 2 + 0];
                    const float vt1 = mesh.texcoords[ib * 2 + 1];
                    const float u2 = mesh.texcoords[ic * 2 + 0];
                    const float vt2 = mesh.texcoords[ic * 2 + 1];

                    const float du1 = u1 - u0, dv1 = vt1 - vt0;
                    const float du2 = u2 - u0, dv2 = vt2 - vt0;
                    const float determinant = du1 * dv2 - du2 * dv1;
                    if (fabsf(determinant) > 1e-8f)
                    {
                        const float r = 1.0f / determinant;
                        tx = (e1x * dv2 - e2x * dv1) * r;
                        ty = (e1y * dv2 - e2y * dv1) * r;
                        tz = (e1z * dv2 - e2z * dv1) * r;
                    }
                }
                if (tx == 0.0f && ty == 0.0f && tz == 0.0f)
                    tx = 1.0f;

                const int idx[3] = {ia, ib, ic};
                for (int k = 0; k < 3; ++k)
                {
                    float* pDst = mesh.tangents + idx[k] * 3;
                    pDst[0] += tx;
                    pDst[1] += ty;
                    pDst[2] += tz;
                }
            }

            for (int i = 0; i < mesh.vertexCount; ++i)
            {
                float* pT = mesh.tangents + i * 3;
                const float* pN = mesh.normals ? mesh.normals + i * 3 : nullptr;
                if (pN)
                {
                    const float nx = pN[0], ny = pN[1], nz = pN[2];
                    const float nn = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (nn > 1e-9f)
                    {
                        const float inv = 1.0f / nn;
                        const float anx = nx * inv, any = ny * inv, anz = nz * inv;
                        const float d = pT[0] * anx + pT[1] * any + pT[2] * anz;
                        pT[0] -= anx * d;
                        pT[1] -= any * d;
                        pT[2] -= anz * d;
                    }
                }
                const float total = sqrtf(pT[0] * pT[0] + pT[1] * pT[1] + pT[2] * pT[2]);
                if (total > 1e-8f)
                {
                    const float inv = 1.0f / total;
                    pT[0] *= inv;
                    pT[1] *= inv;
                    pT[2] *= inv;
                }
                else
                {
                    pT[0] = 1.0f;
                    pT[1] = 0.0f;
                    pT[2] = 0.0f;
                }
            }
        }

        static void FreeMeshCpuData(Mesh& mesh)
        {
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

        static std::string GetModelDirectory(const char* pFilePath)
        {
            if (!pFilePath)
            {
                return {};
            }

            std::string path = pFilePath;
            const std::size_t slash = path.find_last_of("/\\");
            if (slash == std::string::npos)
            {
                return {};
            }
            return path.substr(0, slash + 1);
        }

        static Material LoadAssimpMaterial(QuarkVkRenderer& renderer, const aiScene& scene,
                                           const char* pFilePath, aiMaterial* pSource)
        {
            Material material{};
            material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
            material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;

            if (!pSource)
            {
                return material;
            }

            aiColor4D baseColor{};
            if (AI_SUCCESS == aiGetMaterialColor(pSource, AI_MATKEY_BASE_COLOR, &baseColor) ||
                AI_SUCCESS == aiGetMaterialColor(pSource, AI_MATKEY_COLOR_DIFFUSE, &baseColor))
            {
                material.maps[MATERIAL_MAP_ALBEDO].color = Color{
                    static_cast<std::uint8_t>(std::clamp(baseColor.r, 0.0f, 1.0f) * 255.0f),
                    static_cast<std::uint8_t>(std::clamp(baseColor.g, 0.0f, 1.0f) * 255.0f),
                    static_cast<std::uint8_t>(std::clamp(baseColor.b, 0.0f, 1.0f) * 255.0f),
                    static_cast<std::uint8_t>(std::clamp(baseColor.a, 0.0f, 1.0f) * 255.0f)};
            }

            const std::array<std::pair<int, aiTextureType>, 7> textureTypes = {{{MATERIAL_MAP_ALBEDO, aiTextureType_BASE_COLOR},
                                                                                {MATERIAL_MAP_ALBEDO, aiTextureType_DIFFUSE},
                                                                                {MATERIAL_MAP_METALNESS, aiTextureType_METALNESS},
                                                                                {MATERIAL_MAP_NORMAL, aiTextureType_NORMALS},
                                                                                {MATERIAL_MAP_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS},
                                                                                {MATERIAL_MAP_OCCLUSION, aiTextureType_AMBIENT_OCCLUSION},
                                                                                {MATERIAL_MAP_EMISSION, aiTextureType_EMISSION_COLOR}}};
            for (const auto& [mapIndex, textureType] : textureTypes)
            {
                if (material.maps[mapIndex].texture.valid)
                    continue;
                aiString texturePath;
                if (AI_SUCCESS != pSource->GetTexture(textureType, 0, &texturePath))
                    continue;

                const std::string textureReference = texturePath.C_Str();
                std::string resolvedPath = GetModelDirectory(pFilePath);
                resolvedPath += textureReference;
                TraceLog(LogLevel::Trace, "MODEL", TextFormat("[Vulkan] Model material map %d: %s", mapIndex, resolvedPath.c_str()));

                ITexture loadedTex{};
                if (!textureReference.empty() && textureReference.front() == '*')
                {
                    Image embeddedImage{};
                    std::vector<unsigned char> vRawPixels;
                    bool ownsImage = false;
                    if (DecodeEmbeddedAssimpTexture(scene, texturePath, embeddedImage, vRawPixels, ownsImage))
                    {
                        loadedTex = renderer.LoadTextureFromImage(embeddedImage);
                        ReleaseEmbeddedAssimpTexture(embeddedImage, ownsImage);
                    }
                    else
                    {
                        TraceLog(LogLevel::Error, "MODEL", TextFormat("[Vulkan] Failed to decode embedded texture: %s", textureReference.c_str()));
                    }
                }
                else
                {
                    loadedTex = renderer.LoadTexture(resolvedPath.c_str());
                }
                if (loadedTex.m_Valid)
                {
                    material.maps[mapIndex].texture.id = loadedTex.m_Id;
                    material.maps[mapIndex].texture.width = loadedTex.m_Width;
                    material.maps[mapIndex].texture.height = loadedTex.m_Height;
                    material.maps[mapIndex].texture.mipmaps = loadedTex.m_Mipmaps;
                    material.maps[mapIndex].texture.format = loadedTex.m_Format;
                    material.maps[mapIndex].texture.valid = loadedTex.m_Valid;
                }
            }

            return material;
        }

        static Vec3 ReadVertexPosition(const aiMesh& mesh, unsigned int index)
        {
            return Vec3{
                mesh.mVertices[index].x,
                mesh.mVertices[index].y,
                mesh.mVertices[index].z};
        }

        static Vec3 ReadVertexNormal(const aiMesh& mesh, unsigned int index)
        {
            if (!mesh.HasNormals())
            {
                return Vec3{0.0f, 0.0f, 0.0f};
            }
            return Vec3{
                mesh.mNormals[index].x,
                mesh.mNormals[index].y,
                mesh.mNormals[index].z};
        }

        static Vec2 ReadVertexTexCoord(const aiMesh& mesh, unsigned int index)
        {
            if (!mesh.HasTextureCoords(0))
            {
                return Vec2{0.0f, 0.0f};
            }
            return Vec2{
                mesh.mTextureCoords[0][index].x,
                mesh.mTextureCoords[0][index].y};
        }

        static Color ReadVertexColor(const Mesh& mesh, int index, Color fallback)
        {
            if (!mesh.colors)
            {
                return fallback;
            }

            return Color{
                mesh.colors[index * 4 + 0],
                mesh.colors[index * 4 + 1],
                mesh.colors[index * 4 + 2],
                mesh.colors[index * 4 + 3]};
        }
    } // anonymous

    Model QuarkVkRenderer::LoadModel(const char* pFilePath)
    {
        TraceLog(LogLevel::Info, "MODEL", TextFormat("[Vulkan] Loading 3D model: %s", pFilePath ? pFilePath : "<null>"));

        Assimp::Importer importer;
        const aiScene* pScene = nullptr;
        try
        {
            pScene = importer.ReadFile(
                pFilePath,
                aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs |
                    aiProcess_PopulateArmatureData);
        }
        catch (const std::exception& error)
        {
            TraceLog(LogLevel::Error, "MODEL", TextFormat("[Vulkan] Exception while loading model %s: %s", pFilePath ? pFilePath : "<null>", error.what()));
            return Model{};
        }

        if (!pScene || (pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !pScene->mRootNode)
        {
            TraceLog(LogLevel::Error, "MODEL", TextFormat("[Vulkan] Failed to load model %s: %s", pFilePath ? pFilePath : "<null>", importer.GetErrorString()));
            return Model{};
        }

        TraceLog(LogLevel::Trace, "MODEL", TextFormat("[Vulkan] Assimp scene parsed: %u meshes, %u materials, %u textures, %u animations", pScene->mNumMeshes, pScene->mNumMaterials, pScene->mNumTextures, pScene->mNumAnimations));

        Model model{};
        model.directory = GetModelDirectory(pFilePath);
        model.meshCount = static_cast<int>(pScene->mNumMeshes);
        model.materialCount = static_cast<int>(pScene->mNumMaterials);
        model.meshes = (model.meshCount > 0) ? new Mesh[model.meshCount]{} : nullptr;
        model.materials = (model.materialCount > 0) ? new Material[model.materialCount]{} : nullptr;
        model.meshMaterial = (model.meshCount > 0) ? new int[model.meshCount]{} : nullptr;

        for (int i = 0; i < model.materialCount; ++i)
        {
            model.materials[i] = LoadAssimpMaterial(*this, *pScene, pFilePath, pScene->mMaterials[i]);
        }

        qcPopulateModelSkeleton(pScene, model);

        int totalVertices = 0;
        int totalTriangles = 0;

        for (int i = 0; i < model.meshCount; ++i)
        {
            const aiMesh* pSourceMesh = pScene->mMeshes[i];
            if (!pSourceMesh)
            {
                continue;
            }

            Mesh& dst = model.meshes[i];
            dst.vertexCount = static_cast<int>(pSourceMesh->mNumVertices);
            dst.triangleCount = static_cast<int>(pSourceMesh->mNumFaces);
            totalVertices += dst.vertexCount;
            totalTriangles += dst.triangleCount;

            dst.vertices = (dst.vertexCount > 0) ? new float[dst.vertexCount * 3]{} : nullptr;
            dst.normals = (dst.vertexCount > 0) ? new float[dst.vertexCount * 3]{} : nullptr;
            dst.texcoords = (dst.vertexCount > 0) ? new float[dst.vertexCount * 2]{} : nullptr;
            dst.indices = (dst.triangleCount > 0) ? new unsigned short[dst.triangleCount * 3]{} : nullptr;
            dst.boneIndices = (dst.vertexCount > 0) ? new unsigned char[static_cast<size_t>(dst.vertexCount) * 4u]{} : nullptr;
            dst.boneWeights = (dst.vertexCount > 0) ? new float[static_cast<size_t>(dst.vertexCount) * 4u]{} : nullptr;

            for (unsigned int boneIndex = 0; boneIndex < pSourceMesh->mNumBones; ++boneIndex)
            {
                const aiBone* pBone = pSourceMesh->mBones[boneIndex];
                const int skeletonBoneIndex = qcFindSkeletonBoneIndex(pScene, model, pBone);
                if (skeletonBoneIndex < 0 || skeletonBoneIndex > 255)
                {
                    TraceLog(LogLevel::Warn, "MODEL",
                             TextFormat("[Vulkan] Mesh bone '%s' is not present in the model skeleton or exceeds the 8-bit bone index range",
                                        pBone->mName.C_Str()));
                    continue;
                }
                qcSetMeshBoneOffset(dst, static_cast<unsigned int>(skeletonBoneIndex),
                                    model.skeleton.boneCount, pBone->mOffsetMatrix);
                for (unsigned int weightIndex = 0; weightIndex < pBone->mNumWeights; ++weightIndex)
                {
                    const aiVertexWeight& weight = pBone->mWeights[weightIndex];
                    const unsigned int vertexIndex = weight.mVertexId;
                    float* pDstWeights = dst.boneWeights + static_cast<size_t>(vertexIndex) * 4u;
                    unsigned char* pDstBones = dst.boneIndices + static_cast<size_t>(vertexIndex) * 4u;
                    for (int slot = 0; slot < 4; ++slot)
                    {
                        if (pDstWeights[slot] <= 0.0f)
                        {
                            pDstBones[slot] = static_cast<unsigned char>(skeletonBoneIndex);
                            pDstWeights[slot] = weight.mWeight;
                            break;
                        }
                    }
                }
            }

            for (int v = 0; v < dst.vertexCount; ++v)
            {
                const Vec3 position = ReadVertexPosition(*pSourceMesh, static_cast<unsigned int>(v));
                const Vec3 normal = ReadVertexNormal(*pSourceMesh, static_cast<unsigned int>(v));
                const Vec2 texCoord = ReadVertexTexCoord(*pSourceMesh, static_cast<unsigned int>(v));

                dst.vertices[v * 3 + 0] = position.x;
                dst.vertices[v * 3 + 1] = position.y;
                dst.vertices[v * 3 + 2] = position.z;

                dst.normals[v * 3 + 0] = normal.x;
                dst.normals[v * 3 + 1] = normal.y;
                dst.normals[v * 3 + 2] = normal.z;

                dst.texcoords[v * 2 + 0] = texCoord.x;
                dst.texcoords[v * 2 + 1] = texCoord.y;
            }

            for (int f = 0; f < dst.triangleCount; ++f)
            {
                const aiFace& face = pSourceMesh->mFaces[f];
                if (face.mNumIndices < 3)
                {
                    continue;
                }

                dst.indices[f * 3 + 0] = static_cast<unsigned short>(face.mIndices[0]);
                dst.indices[f * 3 + 1] = static_cast<unsigned short>(face.mIndices[1]);
                dst.indices[f * 3 + 2] = static_cast<unsigned short>(face.mIndices[2]);
            }

            ensure_mesh_tangents(dst);
            UploadMesh(dst, false);

            model.meshMaterial[i] = static_cast<int>(pSourceMesh->mMaterialIndex);

            const char* pMeshName = pSourceMesh->mName.length > 0 ? pSourceMesh->mName.C_Str() : "unnamed";
            TraceLog(LogLevel::Trace, "MODEL", TextFormat("[Vulkan] Mesh #%d ('%s'): %d vertices, %d triangles, Material: #%d", i, pMeshName, dst.vertexCount, dst.triangleCount, model.meshMaterial[i]));
        }

        TraceLog(LogLevel::Info, "MODEL", TextFormat("[Vulkan] Model loaded successfully: %s (%d meshes, %d materials, %d total vertices, %d total triangles)", pFilePath ? pFilePath : "<null>", model.meshCount, model.materialCount, totalVertices, totalTriangles));

        return model;
    }

    void QuarkVkRenderer::UnloadModel(Model& model)
    {
        const int meshCount = model.meshCount;
        const int materialCount = model.materialCount;
        if (m_Device != VK_NULL_HANDLE && (model.meshes != nullptr || model.materials != nullptr))
        {
            vkDeviceWaitIdle(m_Device);
        }
        if (model.meshes)
        {
            for (int i = 0; i < meshCount; ++i)
            {
                UnloadMesh(model.meshes[i]);
            }
            delete[] model.meshes;
            model.meshes = nullptr;
        }

        if (model.materials)
        {
            for (int i = 0; i < materialCount; ++i)
            {
                Material& material = model.materials[i];
                if (material.maps)
                {
                    std::set<uint32_t> unloadedTextures;
                    for (int mapIndex = 0; mapIndex <= MATERIAL_MAP_BRDF; ++mapIndex)
                    {
                        const Texture2D& mapTexture = material.maps[mapIndex].texture;
                        if (mapTexture.valid && unloadedTextures.insert(mapTexture.id).second)
                        {
                            ITexture texture{mapTexture.id, mapTexture.width, mapTexture.height, mapTexture.mipmaps, mapTexture.format, mapTexture.valid};
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
                                    float rotationX, float rotationY, float rotationZ)
    {
        Mat4 transform = Mat4::translation(position.x, position.y, position.z) * Mat4::rotationY(rotationY) * Mat4::rotationX(rotationX) * Mat4::rotationZ(rotationZ) * Mat4::scale(scale, scale, scale);
        DrawModelEx(model, transform);
    }

    void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform)
    {
        const Mat4 modelTransform = transform * model.transform;

        for (int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = model.meshes[i];
            const Material& material = (model.meshMaterial && i >= 0 && i < model.meshCount &&
                                        model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount)
                                           ? model.materials[model.meshMaterial[i]]
                                           : Material{};
            DrawMesh(mesh, material, modelTransform);
        }
    }

    void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint)
    {
        const Mat4 modelTransform = transform * model.transform;
        std::array<MaterialMap, MATERIAL_MAP_BRDF + 1> adjustedMaps{};

        for (int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = model.meshes[i];

            Material adjustedMaterial{};
            const Material* pSourceMaterial = nullptr;
            if (model.meshMaterial && i >= 0 && i < model.meshCount &&
                model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount)
            {
                pSourceMaterial = &model.materials[model.meshMaterial[i]];
            }

            if (pSourceMaterial && pSourceMaterial->maps)
            {
                std::copy(pSourceMaterial->maps, pSourceMaterial->maps + adjustedMaps.size(), adjustedMaps.begin());
                adjustedMaterial = *pSourceMaterial;
                adjustedMaterial.maps = adjustedMaps.data();
                adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color =
                    MultiplyColor(adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color, tint);
            }
            else
            {
                adjustedMaterial.maps = adjustedMaps.data();
                adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color = tint;
            }

            DrawMesh(mesh, adjustedMaterial, modelTransform);
        }
    }

    void QuarkVkRenderer::UploadMesh(Mesh& mesh, bool dynamic)
    {
        mesh.vaoId = 0;
        mesh.vboId = 0;
        mesh.eboId = 0;

        if (mesh.vertexCount < 0)
            mesh.vertexCount = 0;
        if (mesh.triangleCount < 0)
            mesh.triangleCount = 0;
        ensure_mesh_tangents(mesh);

        if (m_GpuMeshes.find(mesh.vertices) != m_GpuMeshes.end())
        {
            if (m_Device != VK_NULL_HANDLE)
                vkDeviceWaitIdle(m_Device);
            DestroyStaticGpuMesh(mesh);
        }
        if (dynamic)
        {
            m_DynamicMeshes.insert(mesh.vertices);
        }
        else
        {
            m_DynamicMeshes.erase(mesh.vertices);
            if (mesh.vertices && !UploadStaticGpuMesh(mesh))
            {
                TraceLog(LogLevel::Warn, "MESH", "[Vulkan] Static mesh GPU upload failed; CPU transform path will be used.");
            }
        }

        TraceLog(LogLevel::Trace, "MESH", TextFormat("[Vulkan] Uploaded mesh (%d vertices, %d triangles)", mesh.vertexCount, mesh.triangleCount));
    }

    void QuarkVkRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* pData, int dataSize, int offset)
    {
        if (!pData || dataSize <= 0 || offset < 0)
        {
            return;
        }

        const bool hadStaticGpuMesh = m_GpuMeshes.find(mesh.vertices) != m_GpuMeshes.end();
        if (hadStaticGpuMesh && m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
            DestroyStaticGpuMesh(mesh);
        }

        auto copyBytes = [&](void* dst, std::size_t dstBytes)
        {
            if (!dst || static_cast<std::size_t>(offset) >= dstBytes)
            {
                return;
            }
            const std::size_t bytesToCopy = std::min<std::size_t>(
                static_cast<std::size_t>(dataSize),
                dstBytes - static_cast<std::size_t>(offset));
            std::memcpy(static_cast<unsigned char*>(dst) + offset, pData, bytesToCopy);
        };

        switch (index)
        {
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

        if (index == 0 || index == 1 || index == 2 || index == 6)
        {
            ensure_mesh_tangents(mesh, true);
            if (hadStaticGpuMesh && m_DynamicMeshes.find(mesh.vertices) == m_DynamicMeshes.end() &&
                !UploadStaticGpuMesh(mesh))
            {
                TraceLog(LogLevel::Warn, "MESH", "[Vulkan] Updated static mesh GPU upload failed; CPU transform path will be used.");
            }
        }
    }

    void QuarkVkRenderer::UnloadMesh(Mesh& mesh)
    {
        if (m_GpuMeshes.find(mesh.vertices) != m_GpuMeshes.end() && m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
        }
        DestroyStaticGpuMesh(mesh);
        m_DynamicMeshes.erase(mesh.vertices);
        TraceLog(LogLevel::Trace, "MESH", TextFormat("[Vulkan] Unloaded mesh (%d vertices, %d triangles)", mesh.vertexCount, mesh.triangleCount));
        FreeMeshCpuData(mesh);
        mesh.vaoId = 0;
        mesh.vboId = 0;
        mesh.eboId = 0;
    }

    bool QuarkVkRenderer::UploadStaticGpuMesh(const Mesh& mesh)
    {
        if (!mesh.vertices || mesh.vertexCount <= 0 || m_Device == VK_NULL_HANDLE ||
            !m_VkCommandContext.IsInitialized() || m_GraphicsQueue == VK_NULL_HANDLE)
        {
            return false;
        }

        std::vector<VkGpuMeshVertex> vVertices(static_cast<size_t>(mesh.vertexCount));
        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            const Color color = ReadVertexColor(mesh, i, WHITE);
            VkGpuMeshVertex& vertex = vVertices[static_cast<size_t>(i)];
            vertex.m_X = mesh.vertices[i * 3 + 0];
            vertex.m_Y = mesh.vertices[i * 3 + 1];
            vertex.m_Z = mesh.vertices[i * 3 + 2];
            vertex.m_W = 1.0f;
            vertex.m_U = mesh.texcoords ? mesh.texcoords[i * 2 + 0] : 0.0f;
            vertex.m_V = mesh.texcoords ? mesh.texcoords[i * 2 + 1] : 0.0f;
            vertex.m_R = NormalizeColorComponent(color.r);
            vertex.m_G = NormalizeColorComponent(color.g);
            vertex.m_B = NormalizeColorComponent(color.b);
            vertex.m_A = NormalizeColorComponent(color.a);
            vertex.m_Nx = mesh.normals ? mesh.normals[i * 3 + 0] : 0.0f;
            vertex.m_Ny = mesh.normals ? mesh.normals[i * 3 + 1] : 1.0f;
            vertex.m_Nz = mesh.normals ? mesh.normals[i * 3 + 2] : 0.0f;
            vertex.m_Nw = 0.0f;
            vertex.m_Wx = 0.0f;
            vertex.m_Wy = 0.0f;
            vertex.m_Wz = 0.0f;
            vertex.m_Ww = 1.0f;
            vertex.m_Tx = mesh.tangents ? mesh.tangents[i * 3 + 0] : 0.0f;
            vertex.m_Ty = mesh.tangents ? mesh.tangents[i * 3 + 1] : 0.0f;
            vertex.m_Tz = mesh.tangents ? mesh.tangents[i * 3 + 2] : 0.0f;
            vertex.m_Tw = mesh.tangents ? 1.0f : 0.0f;
        }

        std::vector<uint32_t> vIndices;
        const size_t requestedIndexCount = mesh.indices && mesh.triangleCount > 0
                                               ? static_cast<size_t>(mesh.triangleCount) * 3u
                                               : static_cast<size_t>(mesh.vertexCount / 3) * 3u;
        vIndices.reserve(requestedIndexCount);
        if (mesh.indices && mesh.triangleCount > 0)
        {
            for (size_t i = 0; i + 2 < requestedIndexCount; i += 3)
            {
                const uint32_t i0 = mesh.indices[i + 0];
                const uint32_t i1 = mesh.indices[i + 1];
                const uint32_t i2 = mesh.indices[i + 2];
                if (i0 >= static_cast<uint32_t>(mesh.vertexCount) ||
                    i1 >= static_cast<uint32_t>(mesh.vertexCount) ||
                    i2 >= static_cast<uint32_t>(mesh.vertexCount))
                {
                    continue;
                }
                vIndices.push_back(i0);
                vIndices.push_back(i1);
                vIndices.push_back(i2);
            }
        }
        else
        {
            for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(mesh.vertexCount); i += 3)
            {
                vIndices.push_back(i + 0);
                vIndices.push_back(i + 1);
                vIndices.push_back(i + 2);
            }
        }
        if (vIndices.empty())
            return false;

        VkGpuMeshData gpuMesh{};
        VkBuffer vertexStaging = VK_NULL_HANDLE;
        VmaAllocation vertexStagingAllocation = VK_NULL_HANDLE;
        VkBuffer indexStaging = VK_NULL_HANDLE;
        VmaAllocation indexStagingAllocation = VK_NULL_HANDLE;
        VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
        auto cleanupStaging = [&]()
        {
            if (vertexStaging != VK_NULL_HANDLE)
            {
                m_GpuAllocator.DestroyBuffer(vertexStaging, vertexStagingAllocation);
                vertexStaging = VK_NULL_HANDLE;
            }
            if (indexStaging != VK_NULL_HANDLE)
            {
                m_GpuAllocator.DestroyBuffer(indexStaging, indexStagingAllocation);
                indexStaging = VK_NULL_HANDLE;
            }
        };
        auto cleanupGpuMesh = [&]()
        {
            if (gpuMesh.m_VertexBuffer != VK_NULL_HANDLE)
                m_GpuAllocator.DestroyBuffer(gpuMesh.m_VertexBuffer, gpuMesh.m_VertexAllocation);
            if (gpuMesh.m_IndexBuffer != VK_NULL_HANDLE)
                m_GpuAllocator.DestroyBuffer(gpuMesh.m_IndexBuffer, gpuMesh.m_IndexAllocation);
        };

        const VkDeviceSize vertexBytes = vVertices.size() * sizeof(VkGpuMeshVertex);
        const VkDeviceSize indexBytes = vIndices.size() * sizeof(uint32_t);
        const VmaAllocationCreateFlags stagingFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (!m_GpuAllocator.CreateBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VMA_MEMORY_USAGE_AUTO, vertexStaging, vertexStagingAllocation, stagingFlags) ||
            !m_GpuAllocator.CreateBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VMA_MEMORY_USAGE_AUTO, indexStaging, indexStagingAllocation, stagingFlags) ||
            !m_GpuAllocator.CreateBuffer(vertexBytes,
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_GPU_ONLY, gpuMesh.m_VertexBuffer, gpuMesh.m_VertexAllocation) ||
            !m_GpuAllocator.CreateBuffer(indexBytes,
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_GPU_ONLY, gpuMesh.m_IndexBuffer, gpuMesh.m_IndexAllocation))
        {
            cleanupStaging();
            cleanupGpuMesh();
            return false;
        }

        VmaAllocationInfo vertexStagingInfo{};
        VmaAllocationInfo indexStagingInfo{};
        vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), vertexStagingAllocation, &vertexStagingInfo);
        vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), indexStagingAllocation, &indexStagingInfo);
        if (!vertexStagingInfo.pMappedData || !indexStagingInfo.pMappedData)
        {
            cleanupStaging();
            cleanupGpuMesh();
            return false;
        }
        std::memcpy(vertexStagingInfo.pMappedData, vVertices.data(), static_cast<size_t>(vertexBytes));
        std::memcpy(indexStagingInfo.pMappedData, vIndices.data(), static_cast<size_t>(indexBytes));
        vmaFlushAllocation(m_GpuAllocator.GetAllocator(), vertexStagingAllocation, 0, vertexBytes);
        vmaFlushAllocation(m_GpuAllocator.GetAllocator(), indexStagingAllocation, 0, indexBytes);

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = m_VkCommandContext.Pool();
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_Device, &allocateInfo, &uploadCommandBuffer) != VK_SUCCESS)
        {
            cleanupStaging();
            cleanupGpuMesh();
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        bool uploadSucceeded = vkBeginCommandBuffer(uploadCommandBuffer, &beginInfo) == VK_SUCCESS;
        if (uploadSucceeded)
        {
            VkBufferCopy vertexCopy{};
            vertexCopy.size = vertexBytes;
            vkCmdCopyBuffer(uploadCommandBuffer, vertexStaging, gpuMesh.m_VertexBuffer, 1, &vertexCopy);
            VkBufferCopy indexCopy{};
            indexCopy.size = indexBytes;
            vkCmdCopyBuffer(uploadCommandBuffer, indexStaging, gpuMesh.m_IndexBuffer, 1, &indexCopy);

            std::array<VkBufferMemoryBarrier, 2> barriers{};
            barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].buffer = gpuMesh.m_VertexBuffer;
            barriers[0].size = VK_WHOLE_SIZE;
            barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].buffer = gpuMesh.m_IndexBuffer;
            barriers[1].size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr,
                                 static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
            uploadSucceeded = vkEndCommandBuffer(uploadCommandBuffer) == VK_SUCCESS;
        }

        if (uploadSucceeded)
        {
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &uploadCommandBuffer;
            uploadSucceeded = vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS &&
                              vkQueueWaitIdle(m_GraphicsQueue) == VK_SUCCESS;
        }
        vkFreeCommandBuffers(m_Device, m_VkCommandContext.Pool(), 1, &uploadCommandBuffer);
        cleanupStaging();
        if (!uploadSucceeded)
        {
            cleanupGpuMesh();
            return false;
        }

        gpuMesh.m_IndexCount = static_cast<uint32_t>(vIndices.size());
        m_GpuMeshes[mesh.vertices] = gpuMesh;
        return true;
    }

    void QuarkVkRenderer::DestroyStaticGpuMesh(const Mesh& mesh)
    {
        const auto it = m_GpuMeshes.find(mesh.vertices);
        if (it == m_GpuMeshes.end())
            return;
        if (it->second.m_VertexBuffer != VK_NULL_HANDLE)
            m_GpuAllocator.DestroyBuffer(it->second.m_VertexBuffer, it->second.m_VertexAllocation);
        if (it->second.m_IndexBuffer != VK_NULL_HANDLE)
            m_GpuAllocator.DestroyBuffer(it->second.m_IndexBuffer, it->second.m_IndexAllocation);
        m_GpuMeshes.erase(it);
    }

    void QuarkVkRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform)
    {
        if (!mesh.vertices || mesh.vertexCount <= 0)
        {
            return;
        }
        const MaterialMap* pAlbedoMap = (material.maps != nullptr) ? &material.maps[MATERIAL_MAP_ALBEDO] : nullptr;
        Color baseColor = WHITE;
        if (pAlbedoMap)
        {
            baseColor = pAlbedoMap->color;
        }

        const Mat4 finalTransform = m_CurrentMatrix * transform;
        const Mat4 modelViewProjection = m_ProjectionMatrix * m_ViewMatrix * finalTransform;
        auto& vertices = GetActive3DTriangleVertices();
        auto& indices = GetActive3DTriangleIndices();
        auto* pDrawItems = &m_Main3DBatch.m_vDrawItems;
        if (m_ActiveRenderTargetId != 0)
        {
            auto renderTargetIt = m_RenderTargets.find(m_ActiveRenderTargetId);
            if (renderTargetIt != m_RenderTargets.end())
            {
                pDrawItems = &renderTargetIt->second.m_vDrawItems3D;
            }
        }
        const bool startsMain3DGeometry = vertices.empty() && m_ActiveRenderTargetId == 0;
        const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
        const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
        const VkDescriptorSet materialDescriptorSet = CreateMaterialDescriptorSet(material);
        const uint32_t shaderProgramId = m_VkShaderCompiler.CurrentProgramId();
        const VkShaderProgramData* pShaderProgram = m_VkShaderCompiler.GetProgram(shaderProgramId);
        const uint32_t drawShaderProgramId = shaderProgramId;
        auto gpuMeshIt = m_GpuMeshes.find(mesh.vertices);
        if (pShaderProgram && pShaderProgram->m_SupportsGpuMeshTransform &&
            gpuMeshIt == m_GpuMeshes.end() && UploadStaticGpuMesh(mesh))
        {
            gpuMeshIt = m_GpuMeshes.find(mesh.vertices);
        }
        if (pShaderProgram && pShaderProgram->m_SupportsGpuMeshTransform &&
            gpuMeshIt != m_GpuMeshes.end() && gpuMeshIt->second.m_IndexCount > 0 &&
            materialDescriptorSet != VK_NULL_HANDLE)
        {
            if (startsMain3DGeometry)
            {
                m_Main3DBatch.m_ShaderProgramId = shaderProgramId;
            }
            Vk3DDrawItem drawItem{};
            drawItem.m_DescriptorSet = materialDescriptorSet;
            drawItem.m_ShaderProgramId = shaderProgramId;
            drawItem.m_FirstVertex = firstVertex;
            drawItem.m_IndexCount = gpuMeshIt->second.m_IndexCount;
            drawItem.m_Indexed = true;
            drawItem.m_GpuMesh = true;
            drawItem.m_BackfaceCulling = m_VkPipeline.BackfaceCulling();
            drawItem.m_GpuVertexBuffer = gpuMeshIt->second.m_VertexBuffer;
            drawItem.m_GpuIndexBuffer = gpuMeshIt->second.m_IndexBuffer;
            drawItem.m_ModelViewProjection = modelViewProjection;
            drawItem.m_Model = finalTransform;
            drawItem.m_aMaterialColor = {
                NormalizeColorComponent(baseColor.r),
                NormalizeColorComponent(baseColor.g),
                NormalizeColorComponent(baseColor.b),
                NormalizeColorComponent(baseColor.a)};
            pDrawItems->push_back(drawItem);
            return;
        }

        auto transformVertex = [&](int vertexIndex, Color color)
        {
            const Vec4 localPosition{
                mesh.vertices[vertexIndex * 3 + 0],
                mesh.vertices[vertexIndex * 3 + 1],
                mesh.vertices[vertexIndex * 3 + 2],
                1.0f};
            const Vec4 world = finalTransform * localPosition;
            const Vec4 clip = modelViewProjection * localPosition;
            Vec4 normal = finalTransform * Vec4{
                                               mesh.normals ? mesh.normals[vertexIndex * 3 + 0] : 0.0f,
                                               mesh.normals ? mesh.normals[vertexIndex * 3 + 1] : 1.0f,
                                               mesh.normals ? mesh.normals[vertexIndex * 3 + 2] : 0.0f,
                                               0.0f};
            const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (normalLength > 0.0f)
            {
                normal.x /= normalLength;
                normal.y /= normalLength;
                normal.z /= normalLength;
            }
            Vec4 tangent{0.0f, 0.0f, 0.0f, 0.0f};
            if (mesh.tangents)
            {
                tangent = finalTransform * Vec4{
                                               mesh.tangents[vertexIndex * 3 + 0],
                                               mesh.tangents[vertexIndex * 3 + 1],
                                               mesh.tangents[vertexIndex * 3 + 2],
                                               0.0f};
                const float tangentLength = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
                if (tangentLength > 0.0f)
                {
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
                tangent.w};
        };

        const size_t meshVertexCount = static_cast<size_t>(mesh.vertexCount);
        vertices.resize(firstVertex + meshVertexCount);
        Vk3DVertex* pDstVertices = vertices.data() + firstVertex;
        m_ThreadPool.ParallelFor(meshVertexCount, 512u, [&](uint32_t, size_t begin, size_t end)
                                 {
                                     for (size_t vIdx = begin; vIdx < end; ++vIdx)
                                     {
                                         const int vertexIndex = static_cast<int>(vIdx);
                                         const Color vertexColor = MultiplyColor(baseColor,
                                                                                 ReadVertexColor(mesh, vertexIndex, WHITE));
                                         pDstVertices[vIdx] = transformVertex(vertexIndex, vertexColor);
                                     }
                                 });

        if (mesh.indices && mesh.triangleCount > 0)
        {
            for (int tri = 0; tri < mesh.triangleCount; ++tri)
            {
                const int i0 = static_cast<int>(mesh.indices[tri * 3 + 0]);
                const int i1 = static_cast<int>(mesh.indices[tri * 3 + 1]);
                const int i2 = static_cast<int>(mesh.indices[tri * 3 + 2]);
                if (i0 < 0 || i1 < 0 || i2 < 0 ||
                    i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount)
                {
                    continue;
                }
                indices.push_back(static_cast<uint32_t>(i0));
                indices.push_back(static_cast<uint32_t>(i1));
                indices.push_back(static_cast<uint32_t>(i2));
            }
        }
        else
        {
            for (int index = 0; index + 2 < mesh.vertexCount; index += 3)
            {
                indices.push_back(static_cast<uint32_t>(index));
                indices.push_back(static_cast<uint32_t>(index + 1));
                indices.push_back(static_cast<uint32_t>(index + 2));
            }
        }

        const uint32_t indexCount = static_cast<uint32_t>(indices.size()) - firstIndex;
        if (indexCount == 0)
        {
            vertices.resize(firstVertex);
            indices.resize(firstIndex);
            return;
        }
        if (startsMain3DGeometry)
        {
            m_Main3DBatch.m_ShaderProgramId = drawShaderProgramId;
        }
        pDrawItems->push_back({materialDescriptorSet, drawShaderProgramId,
                              firstVertex, static_cast<uint32_t>(mesh.vertexCount), firstIndex, indexCount, true, false, false, m_VkPipeline.BackfaceCulling()});
    }

    void QuarkVkRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* pTransforms, int instances)
    {
        if (!pTransforms || instances <= 0 || !mesh.vertices || mesh.vertexCount <= 0)
        {
            return;
        }
        if (instances == 1)
        {
            DrawMesh(mesh, material, pTransforms[0]);
            return;
        }

        const uint32_t shaderProgramId = m_VkShaderCompiler.CurrentProgramId();
        const VkShaderProgramData* pShaderProgram = m_VkShaderCompiler.GetProgram(shaderProgramId);
        if (pShaderProgram && pShaderProgram->m_SupportsGpuMeshTransform)
        {
            auto gpuMeshIt = m_GpuMeshes.find(mesh.vertices);
            if (gpuMeshIt == m_GpuMeshes.end() && UploadStaticGpuMesh(mesh))
            {
                gpuMeshIt = m_GpuMeshes.find(mesh.vertices);
            }
            const VkDescriptorSet materialDescriptorSet = CreateMaterialDescriptorSet(material);
            if (gpuMeshIt != m_GpuMeshes.end() && gpuMeshIt->second.m_IndexCount > 0 &&
                materialDescriptorSet != VK_NULL_HANDLE)
            {
                const MaterialMap* pAlbedoMap = (material.maps != nullptr) ? &material.maps[MATERIAL_MAP_ALBEDO] : nullptr;
                const Color baseColor = pAlbedoMap ? pAlbedoMap->color : WHITE;
                const std::array<float, 4> normalizedColor = {
                    NormalizeColorComponent(baseColor.r),
                    NormalizeColorComponent(baseColor.g),
                    NormalizeColorComponent(baseColor.b),
                    NormalizeColorComponent(baseColor.a)};

                auto& vertices = GetActive3DTriangleVertices();
                auto* pDrawItems = &m_Main3DBatch.m_vDrawItems;
                if (m_ActiveRenderTargetId != 0)
                {
                    auto renderTargetIt = m_RenderTargets.find(m_ActiveRenderTargetId);
                    if (renderTargetIt != m_RenderTargets.end())
                    {
                        pDrawItems = &renderTargetIt->second.m_vDrawItems3D;
                    }
                }
                const bool startsMain3DGeometry = vertices.empty() && m_ActiveRenderTargetId == 0;
                if (startsMain3DGeometry)
                {
                    m_Main3DBatch.m_ShaderProgramId = shaderProgramId;
                }
                const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
                const Mat4 viewProj = m_ProjectionMatrix * m_ViewMatrix;

                Vk3DDrawItem baseItem{};
                baseItem.m_DescriptorSet = materialDescriptorSet;
                baseItem.m_ShaderProgramId = shaderProgramId;
                baseItem.m_FirstVertex = firstVertex;
                baseItem.m_IndexCount = gpuMeshIt->second.m_IndexCount;
                baseItem.m_Indexed = true;
                baseItem.m_GpuMesh = true;
                baseItem.m_BackfaceCulling = m_VkPipeline.BackfaceCulling();
                baseItem.m_GpuVertexBuffer = gpuMeshIt->second.m_VertexBuffer;
                baseItem.m_GpuIndexBuffer = gpuMeshIt->second.m_IndexBuffer;
                baseItem.m_aMaterialColor = normalizedColor;

                if (m_3DInstanceBuffer != VK_NULL_HANDLE &&
                    m_vFrameInstanceTransforms.size() + static_cast<size_t>(instances) <= VK_MAX_INSTANCES_PER_FRAME)
                {
                    const uint32_t localStart = static_cast<uint32_t>(m_vFrameInstanceTransforms.size());
                    m_vFrameInstanceTransforms.reserve(m_vFrameInstanceTransforms.size() + static_cast<size_t>(instances));
                    for (int i = 0; i < instances; ++i)
                    {
                        const Mat4 finalTransform = m_CurrentMatrix * pTransforms[i];
                        const Mat4 mvp = viewProj * finalTransform;
                        VkInstanceTransformData instData{};
                        std::memcpy(instData.m_aModelViewProjection, mvp.m, sizeof(instData.m_aModelViewProjection));
                        std::memcpy(instData.m_aModel, finalTransform.m, sizeof(instData.m_aModel));
                        m_vFrameInstanceTransforms.push_back(instData);
                    }
                    baseItem.m_UseInstanceBuffer = true;
                    baseItem.m_InstanceCount = static_cast<uint32_t>(instances);
                    baseItem.m_FirstInstance = static_cast<uint32_t>(m_CurrentFrame * VK_MAX_INSTANCES_PER_FRAME) + localStart;
                    pDrawItems->push_back(baseItem);
                    return;
                }

                pDrawItems->reserve(pDrawItems->size() + static_cast<size_t>(instances));
                for (int i = 0; i < instances; ++i)
                {
                    const Mat4 finalTransform = m_CurrentMatrix * pTransforms[i];
                    baseItem.m_Model = finalTransform;
                    baseItem.m_ModelViewProjection = viewProj * finalTransform;
                    pDrawItems->push_back(baseItem);
                }
                return;
            }
        }

        for (int i = 0; i < instances; ++i)
        {
            DrawMesh(mesh, material, pTransforms[i]);
        }
    }
} // qci
