#include <QuarkCore/QuarkCore.hpp>

#include <cassert>
#include <cmath>
#include <type_traits>
static_assert(std::is_same_v<decltype(Vec2i{}.x), int>);
static_assert(std::is_same_v<decltype(Vec2f{}.x), float>);
static_assert(std::is_same_v<decltype(Vec2d{}.x), double>);
static_assert(std::is_same_v<decltype(Vec2u{}.x), unsigned int>);
static_assert(std::is_same_v<decltype(Vec3i{}.z), int>);
static_assert(std::is_same_v<decltype(Vec3f{}.z), float>);
static_assert(std::is_same_v<decltype(Vec3d{}.z), double>);
static_assert(std::is_same_v<decltype(Vec3u{}.z), unsigned int>);
static_assert(std::is_same_v<decltype(Vec4i{}.w), int>);
static_assert(std::is_same_v<decltype(Vec4f{}.w), float>);
static_assert(std::is_same_v<decltype(Vec4d{}.w), double>);
static_assert(std::is_same_v<decltype(Vec4u{}.w), unsigned int>);
static_assert(std::is_same_v<decltype(Recti{}.width), int>);
static_assert(std::is_same_v<decltype(Rectf{}.width), float>);
static_assert(std::is_same_v<decltype(Rectd{}.width), double>);
static_assert(std::is_same_v<decltype(Rectu{}.width), unsigned int>);
static_assert(std::is_same_v<Recti, RectT<int>>);
static_assert(std::is_same_v<Rectf, RectT<float>>);
static_assert(std::is_same_v<Rectd, RectT<double>>);
static_assert(std::is_same_v<Rectu, RectT<unsigned int>>);
static_assert(std::is_same_v<Vec2f, Vec2T<float>>);
static_assert(std::is_same_v<Vec3f, Vec3T<float>>);
static_assert(std::is_same_v<Vec4f, Vec4T<float>>);
static_assert(std::is_same_v<Vec2, Vec2f>);
static_assert(std::is_same_v<Vec3, Vec3f>);
static_assert(std::is_same_v<Vec4, Vec4f>);
static_assert(std::is_same_v<decltype(Vec2{}), Vec2f>);
static_assert(std::is_same_v<decltype(Vec3{}), Vec3f>);
static_assert(std::is_same_v<decltype(Vec4{}), Vec4f>);
static_assert(std::is_same_v<Rectangle, Rectf>);

void AcceptFloatVectors(Vec2, Vec3, Vec4) {}

int main() {
    const Vec2 default2{1.0f, 2.0f};
    const Vec3 default3{1.0f, 2.0f, 3.0f};
    const Vec4 default4{1.0f, 2.0f, 3.0f, 4.0f};
    AcceptFloatVectors(default2, default3, default4);

    const Vec4i integer4 = Vec4i{1, 2, 3, 4} + Vec4i{4, 3, 2, 1};
    const Vec4f float4 = Vec4f{1.0f, 2.0f, 3.0f, 4.0f}.normalized();
    const Vec4d double4 = Vec4d{1.0, 2.0, 3.0, 4.0} * 2.0;
    const Vec4u unsigned4 = Vec4u{1U, 2U, 3U, 4U} * Vec4u{2U, 3U, 4U, 5U};
    const Vec4d lerped4 = Lerp(Vec4d{0.0, 0.0, 0.0, 0.0},
                               Vec4d{2.0, 4.0, 6.0, 8.0}, 0.5f);
    const Vec4i zero4 = Vec4Zero<int>();
    const Vec4d one4 = Vec4One<double>();
    const Vec4f addedValue4 = Vec4AddValue(Vec4f{1.0f, 2.0f, 3.0f, 4.0f}, 1.0f);
    const Vec4f subtractedValue4 = Vec4SubtractValue(Vec4f{1.0f, 2.0f, 3.0f, 4.0f}, 1.0f);
    const Vec4f moved4 = Vec4MoveTowards(
        Vec4f{0.0f, 0.0f, 0.0f, 0.0f},
        Vec4f{1.0f, 0.0f, 0.0f, 0.0f},
        0.5f);
    const Vec4i inverted4 = Vec4Invert(Vec4i{1, 1, 1, 1});
    const Vec4d min4 = Vec4Min(Vec4d{1.0, 4.0, 2.0, 5.0}, Vec4d{2.0, 3.0, 4.0, 1.0});
    const Vec4d max4 = Vec4Max(Vec4d{1.0, 4.0, 2.0, 5.0}, Vec4d{2.0, 3.0, 4.0, 1.0});
    const Vec4f almostEqual4{1.0f + EPSILON * 0.25f, 2.0f, 3.0f, 4.0f};
    const Vec4f notEqual4{1.0f + EPSILON * 8.0f, 2.0f, 3.0f, 4.0f};

    const Mat4 identityMatrix = MatrixIdentity();
    const Mat4 scaledMatrix = MatrixMultiplyValue(identityMatrix, 2.0f);
    const Mat4 rotatedMatrix = MatrixRotate(Vec3f{0.0f, 0.0f, 1.0f}, PI * 0.5f);
    const Mat4 rotatedZYX = MatrixRotateZYX(Vec3f{0.0f, 0.0f, PI * 0.5f});
    const Mat4 frustum = MatrixFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
    const Float16 matrixValues = MatrixToFloatV(identityMatrix);
    const Mat4 decomposable = TransformToMatrix(
        Vec3f{3.0f, 4.0f, 5.0f},
        Quaternion{},
        Vec3f{2.0f, 3.0f, 4.0f});
    Vec3f decomposedTranslation{};
    Vec3f decomposedScale{};
    Quaternion decomposedRotation{};
    MatrixDecompose(
        decomposable,
        &decomposedTranslation,
        &decomposedRotation,
        &decomposedScale);

    const Quaternion quaternion{1.0f, 2.0f, 3.0f, 4.0f};
    const Quaternion quaternionAdded = QuaternionAddValue(quaternion, 1.0f);
    const Quaternion quaternionSubtracted = QuaternionSubtractValue(quaternion, 1.0f);
    const Quaternion quaternionInverted = QuaternionInvert(Quaternion{0.0f, 0.0f, 1.0f, 1.0f});
    const Quaternion nlerped = QuaternionNlerp(
        Quaternion{},
        QuaternionFromAxisAngle(Vec3f{0.0f, 0.0f, 1.0f}, PI),
        0.5f);
    const Quaternion hermiteQuaternion = QuaternionCubicHermiteSpline(
        Quaternion{},
        Quaternion{},
        Quaternion{},
        Quaternion{},
        0.5f);
    const Quaternion fromTo = QuaternionFromVector3ToVector3(
        Vec3f{1.0f, 0.0f, 0.0f},
        Vec3f{0.0f, 1.0f, 0.0f});
    Vec3f axis{};
    float axisAngle = 0.0f;
    QuaternionToAxisAngle(
        QuaternionFromAxisAngle(Vec3f{0.0f, 0.0f, 1.0f}, PI * 0.5f),
        &axis,
        &axisAngle);
    const Quaternion eulerQuaternion = QuaternionFromEuler(0.3f, -0.2f, 0.4f);
    const Vec3f eulerAngles = QuaternionToEuler(eulerQuaternion);
    const Quaternion transformedQuaternion = QuaternionTransform(
        Quaternion{1.0f, 2.0f, 3.0f, 1.0f},
        Mat4::translation(4.0f, 5.0f, 6.0f));

    const Vec2i zero2 = Vec2Zero<int>();
    const Vec2i one2 = Vec2One<int>();
    const Vec2i added2 = Vec2Add(Vec2i{1, 2}, Vec2i{3, 4});
    const Vec2d lerped2 = Lerp(Vec2d{0.0, 0.0}, Vec2d{2.0, 4.0}, 0.5f);
    const Vec2i integer2 = Vec2i{2, 3} + Vec2i{4, 5};
    const Vec2f float2 = Vec2Rotate(Vec2f{1.0f, 2.0f}, 0.0f);
    const Vec2d double2 = Vec2d{3.0, 4.0}.componentMax(Vec2d{4.0, 3.0});
    const Vec2u unsigned2 = Vec2AddValue(Vec2u{4U, 6U}, 2U);
    const Vec2d transformed2 = Vec2Transform(Vec2d{1.0, 2.0}, Mat4::identity());
    const Vec2i reflected2 = Vec2Reflect(Vec2i{1, -1}, Vec2i{0, 1});
    const Vec2i subtracted2 = Vec2SubtractValue(Vec2i{4, 6}, 1);
    const Vec2i clamped2 = Vec2Clamp(Vec2i{4, 6}, Vec2i{0, 1}, Vec2i{3, 5});
    const Vec2i refracted2 = Vec2Refract(Vec2i{1, 0}, Vec2i{0, 1}, 1);
    const int lengthSquared2 = Vec2LengthSqr(Vec2i{3, 4});
    const int cross2 = Vec2CrossProduct(Vec2i{1, 0}, Vec2i{0, 1});
    const auto distanceSquared2 = Vec2DistanceSqr(Vec2d{1.0, 2.0}, Vec2d{4.0, 6.0});
    const auto angle2 = Vec2Angle(Vec2f{1.0f, 0.0f}, Vec2f{0.0f, 1.0f});
    const auto lineAngle2 = Vec2LineAngle(Vec2f{0.0f, 0.0f}, Vec2f{1.0f, 1.0f});
    const Vec2d rotated2 = Vec2Rotate(Vec2d{1.0, 0.0}, 0.0);
    const Vec2i towards2 = Vec2MoveTowards(Vec2i{0, 0}, Vec2i{1, 1}, 1);
    const Vec2i inverted2 = Vec2Invert(Vec2i{1, 1});
    const Vec2i clampedMagnitude2 = Vec2ClampValue(Vec2i{3, 4}, 1, 2);
    const Vec2d min2 = Vec2Min(Vec2d{1.0, 2.0}, Vec2d{2.0, 1.0});
    const Vec2d max2 = Vec2Max(Vec2d{1.0, 2.0}, Vec2d{2.0, 1.0});

    const Vec3u one3 = Vec3One<unsigned int>();
    const Vec3i subtracted3 = Vec3Subtract(Vec3i{3, 2, 1}, Vec3i{1, 1, 1});
    const Vec3d lerped3 = Lerp(Vec3d{0.0, 0.0, 0.0}, Vec3d{2.0, 4.0, 6.0}, 0.5f);
    const auto distance3 = Vec3Distance(Vec3i{0, 0, 0}, Vec3i{3, 4, 0});
    const Vec3i integer3 = Vec3i{1, 2, 3}.cross(Vec3i{0, 0, 1});
    const Vec3f float3 = Vec3f{1.0f, 0.0f, 0.0f}.rotatedByAxisAngle(
        Vec3f{0.0f, 0.0f, 1.0f}, 0.0f);
    const Vec3d double3 = Vec3d{1.0, 2.0, 3.0}.componentMin(Vec3d{2.0, 1.0, 4.0});
    const Vec3u unsigned3 = Vec3AddValue(Vec3u{2U, 3U, 4U}, 1U);
    const Vec3d projected3 = Vec3Project(Vec3d{2.0, 3.0, 4.0}, Vec3d{1.0, 0.0, 0.0});
    const Vec3i rejected3 = Vec3Reject(Vec3i{2, 3, 4}, Vec3i{1, 0, 0});
    const Vec3i perpendicular3 = Vec3Perpendicular(Vec3i{1, 0, 0});
    const Vec3i moved3 = Vec3MoveTowards(Vec3i{0, 0, 0}, Vec3i{1, 2, 3}, 1);
    const Vec3i hermite3 = Vec3CubicHermite(Vec3i{0, 0, 0}, Vec3i{1, 1, 1},
                                             Vec3i{2, 2, 2}, Vec3i{1, 1, 1}, 0.5f);
    const Vec3d reflected3 = Vec3Reflect(Vec3d{1.0, -1.0, 0.0}, Vec3d{0.0, 1.0, 0.0});
    const Vec3u min3 = Vec3Min(Vec3u{1U, 5U, 2U}, Vec3u{2U, 4U, 3U});
    const Vec3u max3 = Vec3Max(Vec3u{1U, 5U, 2U}, Vec3u{2U, 4U, 3U});
    const Vec3d barycenter3 = Vec3Barycenter(Vec3d{0.25, 0.25, 0.0},
        Vec3d{0.0, 0.0, 0.0}, Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 1.0, 0.0});
    const Vec3i refracted3 = Vec3Refract(Vec3i{1, 0, 0}, Vec3i{0, 1, 0}, 1);
    const Vec3d inverted3 = Vec3Invert(Vec3d{1.0, 2.0, 4.0});
    const Vec3u clamped3 = Vec3Clamp(Vec3u{4U, 5U, 6U}, Vec3u{1U, 2U, 3U}, Vec3u{3U, 4U, 5U});
    const Vec3d clampedMagnitude3 = Vec3ClampValue(Vec3d{3.0, 4.0, 0.0}, 1.0, 2.0);
    const Vec3d rotatedQuaternion3 = Vec3RotateByQuaternion(Vec3d{1.0, 0.0, 0.0}, Quaternion{});
    const Vec3d rotatedAxis3 = Vec3RotateByAxisAngle(
        Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 0.0, 1.0}, 0.0f);
    Vec3d ortho1{1.0, 0.0, 0.0};
    Vec3d ortho2{1.0, 1.0, 0.0};
    Vec3OrthoNormalize(ortho1, ortho2);
    const Float3 floatBuffer = Vec3ToFloatV(Vec3d{1.0, 2.0, 3.0});
    const Vec3d unprojected3 = Vec3Unproject(Vec3d{}, Mat4::identity(), Mat4::identity());

    const Recti recti{1, 2, 3, 4};
    const Rectf rectf{1.0f, 2.0f, 3.0f, 4.0f};
    const Rectd rectd{1.0, 2.0, 3.0, 4.0};
    const Rectu rectu{1U, 2U, 3U, 4U};

    assert(zero2.x == 0 && one2.y == 1 && added2.x == 4 && lerped2.y == 2.0);
    assert(integer2.x == 6 && float2.x == 1.0f && double2.x == 4.0 && unsigned2.x == 6U);
    assert(transformed2.x == 1.0 && reflected2.y == 1 && subtracted2.x == 3);
    assert(clamped2.x == 3 && refracted2.x == 1 && lengthSquared2 == 25 && cross2 == 1);
    assert(distanceSquared2 == 25.0 && angle2 > 1.5 && lineAngle2 < 0.0 && rotated2.x == 1.0);
    assert(towards2.x == 0 && inverted2.x == 1 && clampedMagnitude2.x == 1);
    assert(min2.x == 1.0 && max2.x == 2.0);
    assert(one3.z == 1U && subtracted3.x == 2 && lerped3.z == 3.0 && distance3 == 5.0);
    assert(integer3.z == 0 && float3.x == 1.0f && double3.y == 1.0 && unsigned3.z == 5U);
    assert(projected3.x == 2.0 && rejected3.x == 0 && perpendicular3.x == 0 && moved3.x == 0);
    assert(hermite3.x == 1 && reflected3.y == 1.0 && min3.x == 1U && max3.y == 5U);
    assert(barycenter3.x == 0.5 && refracted3.x == 1 && inverted3.z == 0.25);
    assert(clamped3.x == 3U && std::fabs(clampedMagnitude3.x - 1.2) < 1e-9);
    assert(rotatedQuaternion3.x == 1.0 && rotatedAxis3.x == 1.0);
    assert(ortho2.y == 1.0 && floatBuffer.v[2] == 3.0f && unprojected3.z == 0.0);
    assert(recti.width == 3 && rectf.width == 3.0f && rectd.width == 3.0 && rectu.width == 3U);
    assert(integer4.w == 5 && std::fabs(float4.length() - 1.0f) < 1e-6f);
    assert(double4.w == 8.0 && unsigned4.w == 20U && lerped4.w == 4.0);
    assert(zero4.x == 0 && one4.w == 1.0);
    assert(addedValue4.w == 5.0f && subtractedValue4.x == 0.0f);
    assert(Vec4LengthSqr(Vec4i{1, 2, 3, 4}) == 30);
    assert(Vec4DistanceSqr(Vec4i{0, 0, 0, 0}, Vec4i{1, 2, 2, 4}) == 25);
    assert(Vec4Distance(Vec4i{0, 0, 0, 0}, Vec4i{1, 2, 2, 4}) == 5.0);
    assert(min4.x == 1.0 && min4.w == 1.0 && max4.y == 4.0 && max4.z == 4.0);
    assert(moved4.x == 0.5f && inverted4.w == 1);
    assert(Vec4Equals(Vec4f{1.0f, 2.0f, 3.0f, 4.0f}, almostEqual4));
    assert(!Vec4Equals(Vec4f{1.0f, 2.0f, 3.0f, 4.0f}, notEqual4));
    assert(Vec2Equals(Vec2f{1.0f, 2.0f}, Vec2f{1.0f + EPSILON * 0.25f, 2.0f}));
    assert(!Vec2Equals(Vec2f{1.0f, 2.0f}, Vec2f{1.0f + EPSILON * 2.0f, 2.0f}));
    assert(Vec3Equals(Vec3f{1.0f, 2.0f, 3.0f}, Vec3f{1.0f, 2.0f, 3.0f + EPSILON * 0.25f}));
    assert(!Vec3Equals(Vec3f{1.0f, 2.0f, 3.0f}, Vec3f{1.0f, 2.0f, 3.0f + EPSILON * 4.0f}));
    assert(Vec3DistanceSqr(Vec3f{}, Vec3f{3.0f, 4.0f, 0.0f}) == 25.0f);
    assert(Vec3Normalize(Vec3f{}).lengthSquared() == 0.0f);
    assert(std::fabs(Vec3Normalize(Vec3f{1e-10f, 0.0f, 0.0f}).x - 1.0f) < 1e-6f);
    assert(MatrixDeterminant(identityMatrix) == 1.0f && MatrixTrace(identityMatrix) == 4.0f);
    assert(MatrixDeterminant(scaledMatrix) == 16.0f);
    assert(scaledMatrix.m[0] == 2.0f && scaledMatrix.m[15] == 2.0f);
    assert(std::fabs((rotatedMatrix * Vec3f{1.0f, 0.0f, 0.0f}).y - 1.0f) < 1e-6f);
    assert(std::fabs(rotatedZYX.m[1] - rotatedMatrix.m[1]) < 1e-6f);
    assert(frustum.m[11] == -1.0f && frustum.m[15] == 0.0f);
    assert(matrixValues.v[0] == 1.0f && matrixValues.v[15] == 1.0f);
    assert(decomposedTranslation.x == 3.0f && decomposedTranslation.y == 4.0f);
    assert(decomposedTranslation.z == 5.0f && decomposedScale.x == 2.0f);
    assert(decomposedScale.y == 3.0f && decomposedScale.z == 4.0f);
    assert(QuaternionAddValue(quaternion, 1.0f) == quaternionAdded);
    assert(QuaternionSubtractValue(quaternion, 1.0f) == quaternionSubtracted);
    assert(std::fabs(quaternionInverted.z + 0.5f) < 1e-6f);
    assert(std::fabs(QuaternionLength(nlerped) - 1.0f) < 1e-6f);
    assert(std::fabs(QuaternionLength(hermiteQuaternion) - 1.0f) < 1e-6f);
    assert(std::fabs(fromTo.z - std::sqrt(0.5f)) < 1e-6f);
    assert(std::fabs(axis.z - 1.0f) < 1e-6f && std::fabs(axisAngle - PI * 0.5f) < 1e-6f);
    assert(std::fabs(eulerAngles.x - 0.3f) < 1e-6f);
    assert(std::fabs(eulerAngles.y + 0.2f) < 1e-6f);
    assert(std::fabs(eulerAngles.z - 0.4f) < 1e-6f);
    assert((transformedQuaternion == Quaternion{5.0f, 7.0f, 9.0f, 1.0f}));
    return 0;
}
