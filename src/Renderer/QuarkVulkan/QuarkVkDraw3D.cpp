#include "QuarkVkRenderer.hpp"

#include <cmath>

namespace qc {

std::vector<Vk3DVertex>& QuarkVkRenderer::GetActive3DTriangleVertices() {
    if (m_activeRenderTargetId != 0) {
        auto it = m_renderTargets.find(m_activeRenderTargetId);
        if (it != m_renderTargets.end()) {
            return it->second.triangleVertices3D;
        }
    }
    return m_main3DBatch.triangleVertices;
}

const std::vector<Vk3DVertex>& QuarkVkRenderer::GetActive3DTriangleVertices() const {
    if (m_activeRenderTargetId != 0) {
        auto it = m_renderTargets.find(m_activeRenderTargetId);
        if (it != m_renderTargets.end()) {
            return it->second.triangleVertices3D;
        }
    }
    return m_main3DBatch.triangleVertices;
}

std::vector<Vk3DVertex>& QuarkVkRenderer::GetActive3DLineVertices() {
    if (m_activeRenderTargetId != 0) {
        auto it = m_renderTargets.find(m_activeRenderTargetId);
        if (it != m_renderTargets.end()) {
            return it->second.lineVertices3D;
        }
    }
    return m_main3DBatch.lineVertices;
}

const std::vector<Vk3DVertex>& QuarkVkRenderer::GetActive3DLineVertices() const {
    if (m_activeRenderTargetId != 0) {
        auto it = m_renderTargets.find(m_activeRenderTargetId);
        if (it != m_renderTargets.end()) {
            return it->second.lineVertices3D;
        }
    }
    return m_main3DBatch.lineVertices;
}

Vk3DVertex QuarkVkRenderer::Transform3DVertex(Vec3 position, Color color) const {
    const Vec4 clip = m_projectionMatrix * (m_viewMatrix * (m_currentMatrix * Vec4{ position.x, position.y, position.z, 1.0f }));
    return {
        clip.x, clip.y, clip.z, clip.w,
        0.0f, 0.0f,
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        position.x, position.y, position.z, 1.0f
    };
}

void QuarkVkRenderer::AppendTriangle3D(std::vector<Vk3DVertex>& vertices,
                                       Vec3 a, Vec3 b, Vec3 c, Color color) {
    const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
    if (vertices.empty() && m_activeRenderTargetId == 0) {
        m_main3DBatch.shaderProgramId = m_currentShaderProgramId;
    }
    vertices.push_back(Transform3DVertex(a, color));
    vertices.push_back(Transform3DVertex(b, color));
    vertices.push_back(Transform3DVertex(c, color));
    const Vec3 edgeA = b - a;
    const Vec3 edgeB = c - a;
    const Vec3 faceNormal = edgeA.cross(edgeB).normalized();
    for (size_t index = vertices.size() - 3; index < vertices.size(); ++index) {
        vertices[index].nx = faceNormal.x;
        vertices[index].ny = faceNormal.y;
        vertices[index].nz = faceNormal.z;
    }

    auto* drawItems = &m_main3DBatch.drawItems;
    if (m_activeRenderTargetId != 0) {
        const auto renderTargetIt = m_renderTargets.find(m_activeRenderTargetId);
        if (renderTargetIt != m_renderTargets.end()) {
            drawItems = &renderTargetIt->second.drawItems3D;
        }
    }

    VkDescriptorSet descriptorSet = m_white3DDescriptorSet;
    const auto shaderIt = m_shaderPrograms.find(m_currentShaderProgramId);
    if (shaderIt != m_shaderPrograms.end() && shaderIt->second.supports3D) {
        descriptorSet = shaderIt->second.descriptorSet3D;
    }
    drawItems->push_back({ descriptorSet, m_currentShaderProgramId, firstVertex, 3 });
}

void QuarkVkRenderer::AppendLine3D(std::vector<Vk3DVertex>& vertices,
                                   Vec3 a, Vec3 b, Color color) {
    vertices.push_back(Transform3DVertex(a, color));
    vertices.push_back(Transform3DVertex(b, color));
}

void QuarkVkRenderer::BeginMode3D(const Camera3D& camera) {
    m_viewMatrix = Mat4::lookAt(camera.position, camera.target, camera.up);
    m_viewPos = camera.position;

    if (camera.projection == CAMERA_PERSPECTIVE) {
        float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        if (m_activeRenderTargetId != 0) {
            const auto rtIt = m_renderTargets.find(m_activeRenderTargetId);
            if (rtIt != m_renderTargets.end() && rtIt->second.width > 0 && rtIt->second.height > 0) {
                aspect = static_cast<float>(rtIt->second.width) / static_cast<float>(rtIt->second.height);
            }
        }
        m_projectionMatrix = Mat4::perspectiveVulkan(
            camera.fovy * PI / 180.0f,
            aspect,
            0.1f, 1000.0f);
    } else {
        m_projectionMatrix = Mat4::identity();
    }
}

void QuarkVkRenderer::EndMode3D() {}

void QuarkVkRenderer::Set3DLightEnabled(int index, bool enabled) {
    if (index >= 0 && index < static_cast<int>(m_3DLightEnabled.size())) {
        m_3DLightEnabled[static_cast<size_t>(index)] = enabled;
        m_lights[static_cast<size_t>(index)].enabled = enabled;
    }
}

void QuarkVkRenderer::Set3DView(const Mat4& view, const Mat4& projection) {
    m_viewMatrix       = view;
    m_projectionMatrix = projection;
}

void QuarkVkRenderer::DrawLine3D(Vec3 startPos, Vec3 endPos, Color color) {
    auto& lines = GetActive3DLineVertices();
    lines.push_back(Transform3DVertex(startPos, color));
    lines.push_back(Transform3DVertex(endPos, color));
}

void QuarkVkRenderer::DrawPlane(Vec3 center, Vec2 size, Color color) {
    const Vec3 p0 = center + Vec3{-size.x * 0.5f, 0.0f, -size.y * 0.5f};
    const Vec3 p1 = center + Vec3{ size.x * 0.5f, 0.0f, -size.y * 0.5f};
    const Vec3 p2 = center + Vec3{ size.x * 0.5f, 0.0f,  size.y * 0.5f};
    const Vec3 p3 = center + Vec3{-size.x * 0.5f, 0.0f,  size.y * 0.5f};

    auto& tris = GetActive3DTriangleVertices();
    AppendTriangle3D(tris, p0, p1, p2, color);
    AppendTriangle3D(tris, p0, p2, p3, color);
}

void QuarkVkRenderer::DrawCube(Vec3 position, float width, float height, float length, Color color) {
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float hl = length * 0.5f;

    const Vec3 v[8] = {
        position + Vec3{-hw, -hh, -hl},
        position + Vec3{ hw, -hh, -hl},
        position + Vec3{ hw,  hh, -hl},
        position + Vec3{-hw,  hh, -hl},
        position + Vec3{-hw, -hh,  hl},
        position + Vec3{ hw, -hh,  hl},
        position + Vec3{ hw,  hh,  hl},
        position + Vec3{-hw,  hh,  hl}
    };

    auto& tris = GetActive3DTriangleVertices();
    AppendTriangle3D(tris, v[0], v[1], v[2], color);
    AppendTriangle3D(tris, v[0], v[2], v[3], color);

    AppendTriangle3D(tris, v[4], v[6], v[5], color);
    AppendTriangle3D(tris, v[4], v[7], v[6], color);

    AppendTriangle3D(tris, v[4], v[5], v[1], color);
    AppendTriangle3D(tris, v[4], v[1], v[0], color);

    AppendTriangle3D(tris, v[3], v[2], v[6], color);
    AppendTriangle3D(tris, v[3], v[6], v[7], color);

    AppendTriangle3D(tris, v[1], v[5], v[6], color);
    AppendTriangle3D(tris, v[1], v[6], v[2], color);

    AppendTriangle3D(tris, v[4], v[0], v[3], color);
    AppendTriangle3D(tris, v[4], v[3], v[7], color);
}

void QuarkVkRenderer::DrawCubeV(Vec3 position, Vec3 size, Color color) {
    DrawCube(position, size.x, size.y, size.z, color);
}

void QuarkVkRenderer::DrawCubeWires(Vec3 position, float width, float height, float length, Color color) {
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float hl = length * 0.5f;

    const Vec3 v[8] = {
        position + Vec3{-hw, -hh, -hl},
        position + Vec3{ hw, -hh, -hl},
        position + Vec3{ hw,  hh, -hl},
        position + Vec3{-hw,  hh, -hl},
        position + Vec3{-hw, -hh,  hl},
        position + Vec3{ hw, -hh,  hl},
        position + Vec3{ hw,  hh,  hl},
        position + Vec3{-hw,  hh,  hl}
    };

    auto& lines = GetActive3DLineVertices();
    AppendLine3D(lines, v[0], v[1], color);
    AppendLine3D(lines, v[1], v[2], color);
    AppendLine3D(lines, v[2], v[3], color);
    AppendLine3D(lines, v[3], v[0], color);

    AppendLine3D(lines, v[4], v[5], color);
    AppendLine3D(lines, v[5], v[6], color);
    AppendLine3D(lines, v[6], v[7], color);
    AppendLine3D(lines, v[7], v[4], color);

    AppendLine3D(lines, v[0], v[4], color);
    AppendLine3D(lines, v[1], v[5], color);
    AppendLine3D(lines, v[2], v[6], color);
    AppendLine3D(lines, v[3], v[7], color);
}

void QuarkVkRenderer::DrawCubeWiresV(Vec3 position, Vec3 size, Color color) {
    DrawCubeWires(position, size.x, size.y, size.z, color);
}

void QuarkVkRenderer::DrawSphere(Vec3 centerPos, float radius, Color color) {
    DrawSphereExInternal(centerPos, radius, 16, 16, color);
}

void QuarkVkRenderer::DrawSphereExInternal(Vec3 centerPos, float radius, int rings, int slices, Color color) {
    if (rings < 2 || slices < 3) {
        return;
    }

    auto& tris = GetActive3DTriangleVertices();
    for (int ri = 0; ri < rings; ++ri) {
        for (int si = 0; si < slices; ++si) {
            const float phi1 = PI * static_cast<float>(ri) / static_cast<float>(rings);
            const float phi2 = PI * static_cast<float>(ri + 1) / static_cast<float>(rings);
            const float theta1 = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
            const float theta2 = 2.0f * PI * static_cast<float>(si + 1) / static_cast<float>(slices);

            const Vec3 a{
                radius * std::sin(phi1) * std::cos(theta1),
                radius * std::cos(phi1),
                radius * std::sin(phi1) * std::sin(theta1)
            };
            const Vec3 b{
                radius * std::sin(phi1) * std::cos(theta2),
                radius * std::cos(phi1),
                radius * std::sin(phi1) * std::sin(theta2)
            };
            const Vec3 d{
                radius * std::sin(phi2) * std::cos(theta1),
                radius * std::cos(phi2),
                radius * std::sin(phi2) * std::sin(theta1)
            };
            const Vec3 e{
                radius * std::sin(phi2) * std::cos(theta2),
                radius * std::cos(phi2),
                radius * std::sin(phi2) * std::sin(theta2)
            };

            AppendTriangle3D(tris, centerPos + a, centerPos + b, centerPos + e, color);
            AppendTriangle3D(tris, centerPos + a, centerPos + e, centerPos + d, color);
        }
    }
}

void QuarkVkRenderer::DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color) {
    DrawSphereExInternal(centerPos, radius, rings, slices, color);
}

void QuarkVkRenderer::DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color) {
    if (rings < 2 || slices < 3) {
        return;
    }

    auto& lines = GetActive3DLineVertices();
    for (int ri = 0; ri <= rings; ++ri) {
        const float phi = PI * static_cast<float>(ri) / static_cast<float>(rings);
        for (int si = 0; si < slices; ++si) {
            const float t1 = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
            const float t2 = 2.0f * PI * static_cast<float>(si + 1) / static_cast<float>(slices);
            AppendLine3D(
                lines,
                centerPos + Vec3{ radius * std::sin(phi) * std::cos(t1), radius * std::cos(phi), radius * std::sin(phi) * std::sin(t1) },
                centerPos + Vec3{ radius * std::sin(phi) * std::cos(t2), radius * std::cos(phi), radius * std::sin(phi) * std::sin(t2) },
                color
            );
        }
    }

    for (int si = 0; si < slices; ++si) {
        const float th = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
        for (int ri = 0; ri < rings; ++ri) {
            const float p1 = PI * static_cast<float>(ri) / static_cast<float>(rings);
            const float p2 = PI * static_cast<float>(ri + 1) / static_cast<float>(rings);
            AppendLine3D(
                lines,
                centerPos + Vec3{ radius * std::sin(p1) * std::cos(th), radius * std::cos(p1), radius * std::sin(p1) * std::sin(th) },
                centerPos + Vec3{ radius * std::sin(p2) * std::cos(th), radius * std::cos(p2), radius * std::sin(p2) * std::sin(th) },
                color
            );
        }
    }
}

void QuarkVkRenderer::DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) {
    DrawCylinderEx(position + Vec3{0, -height * 0.5f, 0},
                   position + Vec3{0,  height * 0.5f, 0},
                   radiusBottom, radiusTop, slices, color);
}

void QuarkVkRenderer::DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides, Color color) {
    if (sides < 3) {
        return;
    }

    const Vec3 delta = endPos - startPos;
    const float length = delta.length();
    if (length <= 0.0f) {
        return;
    }
    const Vec3 dir = delta * (1.0f / length);

    Vec3 up{0, 1, 0};
    if (std::fabs(dir.dot(up)) > 0.99f) {
        up = {1, 0, 0};
    }

    const Vec3 xDir = dir.cross(up).normalized();
    const Vec3 yDir = dir.cross(xDir).normalized();

    auto& tris = GetActive3DTriangleVertices();
    for (int i = 0; i < sides; ++i) {
        const float a1 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(sides);
        const float a2 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(sides);

        const Vec3 p1 = startPos + xDir * std::cos(a1) * startRadius + yDir * std::sin(a1) * startRadius;
        const Vec3 p2 = startPos + xDir * std::cos(a2) * startRadius + yDir * std::sin(a2) * startRadius;
        const Vec3 p3 = endPos   + xDir * std::cos(a2) * endRadius   + yDir * std::sin(a2) * endRadius;
        const Vec3 p4 = endPos   + xDir * std::cos(a1) * endRadius   + yDir * std::sin(a1) * endRadius;

        AppendTriangle3D(tris, p1, p2, p3, color);
        AppendTriangle3D(tris, p1, p3, p4, color);

        AppendTriangle3D(tris, startPos, p2, p1, color);
        AppendTriangle3D(tris, endPos, p3, p4, color);
    }
}

void QuarkVkRenderer::DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) {
    DrawCylinderWiresEx(position + Vec3{0, -height * 0.5f, 0},
                        position + Vec3{0,  height * 0.5f, 0},
                        radiusBottom, radiusTop, slices, color);
}

void QuarkVkRenderer::DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int slices, Color color) {
    if (slices < 3) {
        return;
    }

    const Vec3 delta = endPos - startPos;
    const float length = delta.length();
    if (length <= 0.0f) {
        return;
    }
    const Vec3 dir = delta * (1.0f / length);

    Vec3 up{0, 1, 0};
    if (std::fabs(dir.dot(up)) > 0.99f) {
        up = {1, 0, 0};
    }

    const Vec3 xDir = dir.cross(up).normalized();
    const Vec3 yDir = dir.cross(xDir).normalized();

    auto& lines = GetActive3DLineVertices();
    for (int i = 0; i < slices; ++i) {
        const float a1 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        const float a2 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(slices);

        const Vec3 p1 = startPos + xDir * std::cos(a1) * startRadius + yDir * std::sin(a1) * startRadius;
        const Vec3 p2 = startPos + xDir * std::cos(a2) * startRadius + yDir * std::sin(a2) * startRadius;
        const Vec3 p3 = endPos   + xDir * std::cos(a1) * endRadius   + yDir * std::sin(a1) * endRadius;
        const Vec3 p4 = endPos   + xDir * std::cos(a2) * endRadius   + yDir * std::sin(a2) * endRadius;

        AppendLine3D(lines, p1, p2, color);
        AppendLine3D(lines, p3, p4, color);
        AppendLine3D(lines, p1, p3, color);
    }
}

void QuarkVkRenderer::DrawGrid(int slices, float spacing, Color color) {
    const float half = static_cast<float>(slices) * spacing * 0.5f;
    for (int i = 0; i <= slices; ++i) {
        const float f = -half + static_cast<float>(i) * spacing;
        DrawLine3D({f, 0.0f, -half}, {f, 0.0f, half}, color);
        DrawLine3D({-half, 0.0f, f}, {half, 0.0f, f}, color);
    }
}

}; // namespace qc