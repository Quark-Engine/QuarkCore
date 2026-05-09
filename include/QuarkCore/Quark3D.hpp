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
 * @brief Material properties.
 */
struct Material {
    std::string name;
    unsigned int diffuseMap = 0;  // OpenGL texture ID
    Vec3 diffuse{1.0f, 1.0f, 1.0f};
    Vec3 specular{1.0f, 1.0f, 1.0f};
    Vec3 ambient{0.1f, 0.1f, 0.1f};
    float shininess = 32.0f;
};

/**
 * @brief Mesh structure containing vertices and indices.
 */
struct Mesh {
    std::vector<Vertex3D> vertices;
    std::vector<unsigned int> indices;
    unsigned int materialIndex = 0;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
};

/**
 * @brief 3D Model loaded from file.
 */
struct Model {
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
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
