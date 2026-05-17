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
struct Camera3D;
struct Mesh;
struct Model;

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
    Vec3 position{0.0f, 0.0f, 0.0f};     // Ray starting point
    Vec3 direction{0.0f, 0.0f, 1.0f};  // Ray direction (normalized)
};

/**
 * @brief Image data.
 */
struct Image {
    int width = 0;
    int height = 0;
    int channels = 4;
    unsigned char* data = nullptr;
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

/**
 * @brief Set material map color.
 *
 * @param material Material to modify.
 * @param mapIndex Index of material map to color (e.g. MATERIAL_MAP_ALBEDO).
 * @param color Color to set.
 */
QCAPI void SetMaterialColor(Material& material, int mapIndex, Color color);

QCAPI void UploadMesh(Mesh* mesh, bool dynamic);
QCAPI void UpdateMeshBuffer(Mesh mesh, int index, const void* data, int dataSize, int offset);
QCAPI void UnloadMesh(Mesh mesh);
QCAPI void DrawMesh(Mesh mesh, Material material, Matrix transform);
QCAPI void DrawMeshInstanced(Mesh mesh, Material material, const Matrix* transforms, int instances);
QCAPI BoundingBox GetMeshBoundingBox(Mesh mesh);
QCAPI BoundingBox GetModelBoundingBox(Model model);
QCAPI void GenMeshTangents(Mesh* mesh);
QCAPI bool ExportMesh(Mesh mesh, const char* fileName);
QCAPI bool ExportMeshAsCode(Mesh mesh, const char* fileName);

QCAPI Mesh GenMeshPoly(int sides, float radius);
QCAPI Mesh GenMeshPlane(float width, float length, int resX, int resZ);
QCAPI Mesh GenMeshCube(float width, float height, float length);
QCAPI Mesh GenMeshSphere(float radius, int rings, int slices);
QCAPI Mesh GenMeshHemiSphere(float radius, int rings, int slices);
QCAPI Mesh GenMeshCylinder(float radius, float height, int slices);
QCAPI Mesh GenMeshCone(float radius, float height, int slices);
QCAPI Mesh GenMeshTorus(float radius, float size, int radSeg, int sides);
QCAPI Mesh GenMeshKnot(float radius, float size, int radSeg, int sides);
QCAPI Mesh GenMeshHeightmap(Image heightmap, Vec3 size);
QCAPI Mesh GenMeshCubicmap(Image cubicmap, Vec3 cubeSize);

// Backward compatibility
#define MATERIAL_MAP_DIFFUSE      MATERIAL_MAP_ALBEDO
#define MATERIAL_MAP_SPECULAR     MATERIAL_MAP_METALNESS
#define MAX_MESH_VERTEX_BUFFERS   9

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

    unsigned int vaoId = 0;         // OpenGL Vertex Array Object ID
    unsigned int vboId = 0;         // OpenGL Vertex Buffer Object ID
    unsigned int eboId = 0;         // OpenGL Element Buffer Object ID
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
QCAPI Model LoadModel(const char* filePath);

/** * @brief Create a model from a single mesh.
 *
 * @param name Name identifier for the model.
 * @param mesh Mesh to create model from.
 * @return Loaded model.
 */
QCAPI Model LoadModelFromMesh(const char* name, Mesh mesh);
QCAPI Model LoadModelFromMesh(Mesh mesh);
QCAPI Material LoadMaterialDefault();

/** * @brief Unload a model and free its resources.
 *
 * @param model Model to unload.
 */
QCAPI void UnloadModel(Model& model);

/**
 * @brief Draw a model (with texture if set).
 *
 * @param model Model to draw.
 * @param position Position in world space.
 * @param scale Scale factor.
 * @param tint Color tint.
 */
QCAPI void DrawModel(Model model, Vec3 position, float scale, Color tint);

/**
 * @brief Draw a model with extended parameters.
 *
 * @param model Model to draw.
 * @param position Position in world space.
 * @param rotationAxis Axis to rotate around.
 * @param rotationAngle Rotation angle in radians.
 * @param scale Scale vector.
 * @param tint Color tint.
 */
QCAPI void DrawModelEx(Model model, Vec3 position, Vec3 rotationAxis,
               float rotationAngle, Vec3 scale, Color tint);

/**
 * @brief Draw a model wires (with texture if set).
 *
 * @param model Model to draw.
 * @param position Position in world space.
 * @param scale Scale factor.
 * @param tint Color tint.
 */
QCAPI void DrawModelWires(Model model, Vec3 position, float scale, Color tint);

/**
 * @brief Draw a model wires with extended parameters.
 *
 * @param model Model to draw.
 * @param position Position in world space.
 * @param rotationAxis Axis to rotate around.
 * @param rotationAngle Rotation angle in radians.
 * @param scale Scale vector.
 * @param tint Color tint.
 */
QCAPI void DrawModelWiresEx(Model model, Vec3 position, Vec3 rotationAxis,
               float rotationAngle, Vec3 scale, Color tint);

/**
 * @brief Draw a model with extended parameters.
 *
 * @param model Model to draw.
 * @param transform Model transformation matrix.
 */
QCAPI void DrawModelEx(Model model, const Mat4& transform);

/**
 * @brief Draw a bounding box.
 */
QCAPI void DrawBoundingBox(BoundingBox box, Color color);

/**
 * @brief Draw a billboard texture.
 */
QCAPI void DrawBillboard(const Camera3D& camera, Texture2D texture, Vec3 position,
               float scale, Color tint);

/**
 * @brief Draw a billboard texture with source rectangle.
 */
QCAPI void DrawBillboardRec(const Camera3D& camera, Texture2D texture, Rectangle source,
               Vec3 position, Vec2 size, Color tint);

/**
 * @brief Draw a billboard with advanced parameters.
 */
QCAPI void DrawBillboardPro(const Camera3D& camera, Texture2D texture, Rectangle source,
               Vec3 position, Vec3 up, Vec2 size, Vec2 origin, float rotation, Color tint);

/**
 * @brief Set the view and projection matrices for 3D rendering.
 *
 * @param view View matrix.
 * @param projection Projection matrix.
 */
QCAPI void Set3DView(const Mat4& view, const Mat4& projection);

/**
 * @brief Draw a plane.
 * @param center Center position.
 * @param size Plane size.
 * @param color Plane color.
 */
QCAPI void DrawPlane(Vec3 center, Vec2 size, Color color);

/**
 * @brief Draw a cube.
 * @param position Center position.
 * @param size Cube size.
 * @param color Cube color.
 */
QCAPI void DrawCubeV(Vec3 position, Vec3 size, Color color);

/**
 * @brief Draw a cube.
 * @param position Center position.
 * @param width Width.
 * @param height Height.
 * @param length Length.
 * @param color Cube color.
 */
QCAPI void DrawCube(Vec3 position, float width, float height, float length, Color color);

/**
 * @brief Draw a sphere.
 * @param centerPos Center position.
 * @param radius Sphere radius.
 * @param color Sphere color.
 */
QCAPI void DrawSphere(Vec3 centerPos, float radius, Color color);

/**
 * @brief Draw a sphere with extended parameters.
 * @param centerPos Center position.
 * @param radius Sphere radius.
 * @param rings Number of rings.
 * @param slices Number of slices.
 * @param color Sphere color.
 */
QCAPI void DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color);

/**
 * @brief Draw a sphere wireframe.
 * @param centerPos Center position.
 * @param radius Sphere radius.
 * @param rings Number of rings.
 * @param slices Number of slices.
 * @param color Sphere color.
 */
QCAPI void DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color);

/**
 * @brief Draw a line in 3D space.
 * @param startPos Start position.
 * @param endPos End position.
 * @param color Line color.
 */
QCAPI void DrawLine3D(Vec3 startPos, Vec3 endPos, Color color);

/**
 * @brief Draw a grid.
 * @param slices Number of slices.
 * @param spacing Spacing between slices.
 */
QCAPI void DrawGrid(int slices, float spacing);

/**
 * @brief Draw a cube outline (wireframe).
 * @param position Center position.
 * @param width Width.
 * @param height Height.
 * @param length Length.
 * @param color Wire color.
 */
QCAPI void DrawCubeWires(Vec3 position, float width, float height, float length, Color color);

/**
 * @brief Draw a cube wireframe.
 * @param position Center position.
 * @param size Cube size.
 * @param color Cube color.
 */
QCAPI void DrawCubeWiresV(Vec3 position, Vec3 size, Color color);

/**
 * @brief Draw a cylinder.
 * @param position Center position.
 * @param radiusTop Top radius.
 * @param radiusBottom Bottom radius.
 * @param height Cylinder height.
 * @param slices Number of slices.
 * @param color Cylinder color.
 */
QCAPI void DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color);

/**
 * @brief Draw a cylinder with start and end positions.
 * @param startPos Start position.
 * @param endPos End position.
 * @param startRadius Start radius.
 * @param endRadius End radius.
 * @param sides Number of sides.
 * @param color Cylinder color.
 */
QCAPI void DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides, Color color);

/**
 * @brief Draw a cylinder wireframe.
 * @param position Center position.
 * @param radiusTop Top radius.
 * @param radiusBottom Bottom radius.
 * @param height Cylinder height.
 * @param slices Number of slices.
 * @param color Cylinder color.
 */
QCAPI void DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color);

/**
 * @brief Draw a cylinder wireframe with start and end positions.
 * @param startPos Start position.
 * @param endPos End position.
 * @param startRadius Start radius.
 * @param endRadius End radius.
 * @param slices Number of slices.
 * @param color Cylinder color.
 */
QCAPI void DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int slices, Color color);

}  // namespace qc
