/*
    ========================================================
    
        Quark 3D Module
        By Quark Engine Development Team

    --------------------------------------------------------

    This file contains:
        * ASSIMP-based model loading
        * 3D rendering with OpenGL
        * Mesh and material management

    ========================================================
*/

#pragma once

#include "QuarkMath.hpp"

#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <memory>

namespace qc {

// Forward declarations
struct Shader;

/**
 * @brief Vertex data for 3D meshes.
 */
struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 texCoord;
};

/**
 * @brief Ray for raycasting operations.
 */
struct Ray {
    Vec3 origin{0.0f, 0.0f, 0.0f};     // Ray starting point
    Vec3 direction{0.0f, 0.0f, 1.0f};  // Ray direction (normalized)
};

/**
 * @brief Material map index.
 */
enum MaterialMapIndex {
    MATERIAL_MAP_ALBEDO = 0,        // Albedo material (same as: MATERIAL_MAP_DIFFUSE)
    MATERIAL_MAP_METALNESS,         // Metalness material (same as: MATERIAL_MAP_SPECULAR)
    MATERIAL_MAP_NORMAL,            // Normal material
    MATERIAL_MAP_ROUGHNESS,         // Roughness material
    MATERIAL_MAP_OCCLUSION,         // Ambient occlusion material
    MATERIAL_MAP_EMISSION,          // Emission material
    MATERIAL_MAP_HEIGHT,            // Heightmap material
    MATERIAL_MAP_CUBEMAP,           // Cubemap material (NOTE: Uses GL_TEXTURE_CUBE_MAP)
    MATERIAL_MAP_IRRADIANCE,        // Irradiance material (NOTE: Uses GL_TEXTURE_CUBE_MAP)
    MATERIAL_MAP_PREFILTER,         // Prefilter material (NOTE: Uses GL_TEXTURE_CUBE_MAP)
    MATERIAL_MAP_BRDF               // Brdf material
};

/**
 * @brief Material map.
 */
struct MaterialMap {
    Texture2D texture;      // Material map texture
    Color color;            // Material map color
    float value = 0.0f;     // Material map value
};

/**
 * @brief Material properties.
 */
struct Material {
    Shader* shader = nullptr;       // Material shader
    MaterialMap* maps = nullptr;    // Material maps array (MAX_MATERIAL_MAPS)
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // Material generic parameters (if required)
};

// Backward compatibility
#define MATERIAL_MAP_DIFFUSE      MATERIAL_MAP_ALBEDO
#define MATERIAL_MAP_SPECULAR     MATERIAL_MAP_METALNESS

/**
 * @brief Bone information.
 */
struct BoneInfo {
    char name[32] = {0};
    int parent = -1;
};

/**
 * @brief Model animation pose data.
 */
struct ModelAnimPose {
    Matrix* transform = nullptr;
};

/**
 * @brief Skeleton with bone hierarchy and bind pose.
 */
struct ModelSkeleton {
    int boneCount = 0;
    BoneInfo* bones = nullptr;
    ModelAnimPose bindPose;
};

/**
 * @brief Mesh structure containing vertices and indices.
 */
struct Mesh {
    int vertexCount = 0;        // Number of vertices stored in arrays
    int triangleCount = 0;      // Number of triangles stored (indexed or not)

    float* vertices = nullptr;  // Vertex position (XYZ - 3 components per vertex)
    float* texcoords = nullptr; // Vertex texture coordinates (UV - 2 components per vertex)
    float* texcoords2 = nullptr;
    float* normals = nullptr;
    float* tangents = nullptr;
    unsigned char* colors = nullptr;
    unsigned short* indices = nullptr;

    int boneCount = 0;          // Number of bones (MAX: 256 bones)
    unsigned char* boneIndices = nullptr;
    float* boneWeights = nullptr;

    float* animVertices = nullptr;
    float* animNormals = nullptr;

    unsigned int vaoId = 0;
    unsigned int* vboId = nullptr;
};

/**
 * @brief 3D Model loaded from file.
 */
struct Model {
    Matrix transform = Matrix(); // Local transform matrix

    int meshCount = 0;
    int materialCount = 0;
    Mesh* meshes = nullptr;
    Material* materials = nullptr;
    int* meshMaterial = nullptr;

    ModelSkeleton skeleton;

    ModelAnimPose currentPose;
    Matrix* boneMatrices = nullptr;

    std::string directory;
    unsigned int id = 0;
};

/**
 * @brief Load a 3D model from file using ASSIMP.
 *
 * Supported formats: obj, fbx, gltf, glb, dae, blend, 3ds, ase, ply, and more.
 *
 * @param filePath Path to the model file.
 * @return Loaded model.
 * @return Empty model on failure.
 */
Model LoadModel(const char* filePath);

/**
 * @brief Unload a model and free its resources.
 *
 * @param model Model to unload.
 */
void UnloadModel(Model& model);

/**
 * @brief Draw a model with transformation.
 *
 * @param model Model to draw.
 * @param position Position in world space.
 * @param scale Scale factor.
 * @param rotationX Rotation around X axis (radians).
 * @param rotationY Rotation around Y axis (radians).
 * @param rotationZ Rotation around Z axis (radians).
 */
void DrawModel(const Model& model, const Vec3& position, float scale,
               float rotationX, float rotationY, float rotationZ);

/**
 * @brief Draw a model with a custom transformation matrix.
 *
 * @param model Model to draw.
 * @param transform Transformation matrix.
 */
void DrawModelEx(const Model& model, const Mat4& transform);

/**
 * @brief Set the view and projection matrices for 3D rendering.
 *
 * @param view View matrix.
 * @param projection Projection matrix.
 */
void Set3DView(const Mat4& view, const Mat4& projection);

/**
 * @brief Draw a plane.
 * @param center Center position.
 * @param size Plane size.
 * @param color Plane color.
 */
void DrawPlane(Vec3 center, Vec2 size, Color color);

/**
 * @brief Draw a cube.
 * @param position Center position.
 * @param width Width.
 * @param height Height.
 * @param length Length.
 * @param color Cube color.
 */
void DrawCube(Vec3 position, float width, float height, float length, Color color);

/**
 * @brief Draw a sphere.
 * @param centerPos Center position.
 * @param radius Sphere radius.
 * @param color Sphere color.
 */
void DrawSphere(Vec3 centerPos, float radius, Color color);

/**
 * @brief Draw a line in 3D space.
 * @param startPos Start position.
 * @param endPos End position.
 * @param color Line color.
 */
void DrawLine3D(Vec3 startPos, Vec3 endPos, Color color);

/**
 * @brief Draw a grid.
 * @param slices Number of slices.
 * @param spacing Spacing between slices.
 */
void DrawGrid(int slices, float spacing);

/**
 * @brief Draw a cube outline (wireframe).
 * @param position Center position.
 * @param width Width.
 * @param height Height.
 * @param length Length.
 * @param color Wire color.
 */
void DrawCubeWires(Vec3 position, float width, float height, float length, Color color);

}  // namespace qc
