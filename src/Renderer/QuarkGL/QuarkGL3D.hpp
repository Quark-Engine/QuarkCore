#ifndef __QUARK_GL_3D__
#define __QUARK_GL_3D__

#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"

#include <glad/glad.h>

#include <vector>

namespace qc {

struct Model3DState {
    bool initialized = false;
    GLuint shader3D = 0;
    GLint modelLoc = -1, viewLoc = -1, projLoc = -1;
    GLint samplerLoc = -1, lightPosLoc = -1, colorLoc = -1;
    GLuint whiteTexture = 0;
    GLuint blackTexture = 0;
    GLuint flatNormalTexture = 0;

    GLuint planeVAO = 0, planeVBO = 0, planeEBO = 0; int planeIndexCount = 0;
    GLuint cubeVAO = 0, cubeVBO = 0, cubeEBO = 0; int cubeIndexCount = 0;
    GLuint sphereVAO = 0, sphereVBO = 0, sphereEBO = 0; int sphereIndexCount = 0;

    GLuint lineVAO = 0, lineVBO = 0;
    GLuint triVAO = 0, triVBO = 0;
    std::vector<Vertex3D> lineVertices;
    std::vector<Vertex3D> triVertices;
    Color currentLineColor{255, 255, 255, 255};
    Color currentTriColor{255, 255, 255, 255};
    Vec3 lightPosition{5.f, 5.f, 5.f};
    Mat4 viewMatrix = Mat4::identity();
    Mat4 projectionMatrix = Mat4::identity();
};

class QuarkGL3D {
public:
    static void Init3DState(Model3DState& state);
    static void Init3DGeometry(Model3DState& state);
    static GLuint Compile3DShader();
    static void Set3DView(Model3DState& state, const Mat4& view, const Mat4& projection);
    static Vec3 TransformPoint(const Mat4& matrix, const Vec3& point);
    static Mat4 ApplyCurrentMatrix(const Mat4& current, const Mat4& transform);

    static void ApplyDrawState(Model3DState& state, const Mat4& model, Color color, GLuint textureId = 0);
    static void FlushLines3D(Model3DState& state);
    static void FlushTriangles3D(Model3DState& state);
    static void DrawTriangle3DImpl(Model3DState& state, const Mat4& currentMatrix, Vertex3D v1, Vertex3D v2, Vertex3D v3, Color color);
    static void DrawLine3D(Model3DState& state, const Mat4& currentMatrix, Vec3 start, Vec3 end, Color color);
};

} // namespace qc

#endif // __QUARK_GL_3D__
