#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace qc {

void QuarkVkRenderer::BeginMode3D(const Camera3D& camera) {
    m_viewMatrix = Mat4::lookAt(camera.position, camera.target, camera.up);

    if (camera.projection == CAMERA_PERSPECTIVE) {
        m_projectionMatrix = Mat4::perspective(
            camera.fovy * 3.14159265f / 180.0f,
            static_cast<float>(m_width) / static_cast<float>(m_height),
            0.1f, 1000.0f);
    } else {
        m_projectionMatrix = Mat4::identity();
    }
}
void QuarkVkRenderer::EndMode3D() {}

void QuarkVkRenderer::Set3DView(const Mat4& view, const Mat4& projection) {
    m_viewMatrix       = view;
    m_projectionMatrix = projection;
}

void QuarkVkRenderer::DrawLine3D(Vec3 s, Vec3 e, Color c) {
    (void)s; (void)e; (void)c; 
}

void QuarkVkRenderer::DrawPlane(Vec3 c, Vec2 s, Color col) {
    (void)c; (void)s; (void)col; 

}

void QuarkVkRenderer::DrawCube(Vec3 p, float w, float h, float l, Color c) {
    (void)p; (void)w; (void)h; (void)l; (void)c;

}

void QuarkVkRenderer::DrawCubeV(Vec3 p, Vec3 s, Color c) {
    DrawCube(p, s.x, s.y, s.z, c);
}

void QuarkVkRenderer::DrawCubeWires(Vec3 p, float w, float h, float l, Color c) {
    (void)p; (void)w; (void)h; (void)l; (void)c;

}

void QuarkVkRenderer::DrawCubeWiresV(Vec3 p, Vec3 s, Color c) {
    DrawCubeWires(p, s.x, s.y, s.z, c);
}

void QuarkVkRenderer::DrawSphere(Vec3 p, float r, Color c) {
    (void)p; (void)r; (void)c;
}

void QuarkVkRenderer::DrawSphereEx(Vec3 p, float r, int ri, int sl, Color c) {
    (void)p; (void)r; (void)ri; (void)sl; (void)c;

}

void QuarkVkRenderer::DrawSphereWires(Vec3 p, float r, int ri, int sl, Color c) {
    (void)p; (void)r; (void)ri; (void)sl; (void)c;

}

void QuarkVkRenderer::DrawCylinder(Vec3 p, float rt, float rb, float h, int sl, Color c) {
    (void)p; (void)rt; (void)rb; (void)h; (void)sl; (void)c;

}

void QuarkVkRenderer::DrawCylinderEx(Vec3 s, Vec3 e, float sr, float er, int si, Color c) {
    (void)s; (void)e; (void)sr; (void)er; (void)si; (void)c;
}

void QuarkVkRenderer::DrawCylinderWires(Vec3 p, float rt, float rb, float h, int sl, Color c) {
    (void)p; (void)rt; (void)rb; (void)h; (void)sl; (void)c;
}

void QuarkVkRenderer::DrawCylinderWiresEx(Vec3 s, Vec3 e, float sr, float er, int si, Color c) {
    (void)s; (void)e; (void)sr; (void)er; (void)si; (void)c;
}

void QuarkVkRenderer::DrawGrid(int sl, float sp) {
    (void)sl; (void)sp;
}

}; // namespace qc