#ifndef __QUARK_GL_3D__
#define __QUARK_GL_3D__

#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"

#include <glad/glad.h>

#include <vector>

namespace qci
{
    struct Model3DState
    {
        bool m_Initialized = false;
        GLuint m_Shader3D = 0;
        GLint m_ModelLoc = -1, viewLoc = -1, projLoc = -1;
        GLint m_SamplerLoc = -1, lightPosLoc = -1, colorLoc = -1;
        GLuint m_WhiteTexture = 0;
        GLuint m_BlackTexture = 0;
        GLuint m_FlatNormalTexture = 0;

        GLuint m_PlaneVAO = 0, planeVBO = 0, planeEBO = 0;
        int m_PlaneIndexCount = 0;
        GLuint m_CubeVAO = 0, cubeVBO = 0, cubeEBO = 0;
        int m_CubeIndexCount = 0;
        GLuint m_SphereVAO = 0, sphereVBO = 0, sphereEBO = 0;
        int m_SphereIndexCount = 0;

        GLuint m_LineVAO = 0, lineVBO = 0;
        GLuint m_TriVAO = 0, triVBO = 0;
        GLuint m_InstanceVBO = 0;
        size_t m_InstanceVBOCapacity = 0;
        std::vector<Vertex3D> m_vLineVertices;
        std::vector<Vertex3D> m_vTriVertices;
        Color m_CurrentLineColor{255, 255, 255, 255};
        Color m_CurrentTriColor{255, 255, 255, 255};
        Vec3 m_LightPosition{5.f, 5.f, 5.f};
        Mat4 m_ViewMatrix = Mat4::identity();
        Mat4 m_ProjectionMatrix = Mat4::identity();
    };

    class QuarkGL3D
    {
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
} // qci

#endif // __QUARK_GL_3D__
