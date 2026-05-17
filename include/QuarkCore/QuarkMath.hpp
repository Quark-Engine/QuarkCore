/*
    ========================================================
    
        Quark Math Module
        By Quark Engine Development Team

    --------------------------------------------------------

    This file contains:
        * Vector and matrix operations
        * Math utilities for 2D and 3D graphics

    ========================================================
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>

#define PI 3.14159265358979323846f
#define EPSILON 0.000001f
#define DEG2RAD (PI/180.0f)
#define RAD2DEG (180.0f/PI)

namespace qc {

/**
 * @brief Clamp value to range.
 */
inline float Clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief Linear interpolation between two values.
 */
inline float Lerp(float start, float end, float amount) {
    return start + (end - start) * amount;
}

/**
 * @brief Smooth step interpolation.
 */
inline float SmoothStep(float edge0, float edge1, float x) {
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief Convert degrees to radians.
 */
inline float ToRadians(float degrees) {
    return degrees * 3.14159265359f / 180.0f;
}

/**
 * @brief Convert radians to degrees.
 */
inline float ToDegrees(float radians) {
    return radians * 180.0f / 3.14159265359f;
}

/**
 * @brief Normalize a value to the [0, 1] range within [min, max].
 */
inline float Normalize(float value, float min, float max) {
    if (max == min) return 0.0f;
    return (value - min) / (max - min);
}

/**
 * @brief Move a value towards target by a maximum delta.
 */
inline float MoveTowards(float value, float target, float maxDelta) {
    if (std::fabs(target - value) <= maxDelta) return target;
    return value + (target > value ? maxDelta : -maxDelta);
}

/**
 * @brief Sign of a value.
 */
inline float Sign(float value) {
    return static_cast<float>((value > 0.0f) - (value < 0.0f));
}

/**
 * @brief Color structure.
 */
struct Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

inline constexpr Color LIGHTGRAY{200, 200, 200, 255};
inline constexpr Color GRAY{130, 130, 130, 255};
inline constexpr Color DARKGRAY{80, 80, 80, 255};
inline constexpr Color YELLOW{253, 249, 0, 255};
inline constexpr Color ORANGE{255, 161, 0, 255};
inline constexpr Color RED{230, 41, 55, 255};
inline constexpr Color GREEN{0, 228, 48, 255};
inline constexpr Color BLUE{0, 121, 241, 255};
inline constexpr Color SKYBLUE{102, 191, 255, 255};
inline constexpr Color PURPLE{200, 122, 255, 255};
inline constexpr Color WHITE{255, 255, 255, 255};
inline constexpr Color BLACK{0, 0, 0, 255};
inline constexpr Color BLANK{0, 0, 0, 0};

/**
 * @brief 2D vector structure.
 */
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * @brief 2D integer vector structure.
 */
struct IVec2 {
    int x = 0;
    int y = 0;
};

/**
 * @brief Rectangle structure.
 */
struct Rectangle {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/**
 * @brief 3D vector structure.
 */
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& v) const {
        return Vec3(x + v.x, y + v.y, z + v.z);
    }

    Vec3 operator-(const Vec3& v) const {
        return Vec3(x - v.x, y - v.y, z - v.z);
    }

    Vec3 operator*(float s) const {
        return Vec3(x * s, y * s, z * s);
    }

    float dot(const Vec3& v) const {
        return x * v.x + y * v.y + z * v.z;
    }

    Vec3 cross(const Vec3& v) const {
        return Vec3(
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        );
    }

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vec3 normalized() const {
        float len = length();
        if (len > 0.0f) {
            return *this * (1.0f / len);
        }
        return *this;
    }
};

/**
 * @brief 4D vector structure.
 */
struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    Vec4() = default;
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    Vec4 operator+(const Vec4& v) const {
        return Vec4(x + v.x, y + v.y, z + v.z, w + v.w);
    }

    Vec4 operator-(const Vec4& v) const {
        return Vec4(x - v.x, y - v.y, z - v.z, w - v.w);
    }

    Vec4 operator*(float s) const {
        return Vec4(x * s, y * s, z * s, w * s);
    }

    float dot(const Vec4& v) const {
        return x * v.x + y * v.y + z * v.z + w * v.w;
    }

    float length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    Vec4 normalized() const {
        float len = length();
        if (len > 0.0f) {
            return *this * (1.0f / len);
        }
        return *this;
    }
};

/**
 * @brief Bounding box structure.
 */
struct BoundingBox {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};
};

inline Vec2 Lerp(const Vec2& start, const Vec2& end, float amount) {
    return Vec2{
        Lerp(start.x, end.x, amount),
        Lerp(start.y, end.y, amount)
    };
}

inline Vec3 Lerp(const Vec3& start, const Vec3& end, float amount) {
    return Vec3{
        Lerp(start.x, end.x, amount),
        Lerp(start.y, end.y, amount),
        Lerp(start.z, end.z, amount)
    };
}

/**
 * @brief Vertex structure for rendering.
 */
struct Vertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

/**
 * @brief 4x4 matrix for 3D transformations.
 */
struct QCAPI Mat4 {
    float m[16]{};

    Mat4() {
        m[0] = 1.0f;
        m[5] = 1.0f;
        m[10] = 1.0f;
        m[15] = 1.0f;
    }

    static Mat4 identity() {
        Mat4 result;
        return result;
    }

    static Mat4 translation(float x, float y, float z) {
        Mat4 result = identity();
        result.m[12] = x;
        result.m[13] = y;
        result.m[14] = z;
        return result;
    }

    static Mat4 scale(float x, float y, float z) {
        Mat4 result = identity();
        result.m[0] = x;
        result.m[5] = y;
        result.m[10] = z;
        return result;
    }

    static Mat4 rotationX(float angle) {
        Mat4 result = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        result.m[5] = c;
        result.m[6] = s;
        result.m[9] = -s;
        result.m[10] = c;
        return result;
    }

    static Mat4 rotationY(float angle) {
        Mat4 result = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        result.m[0] = c;
        result.m[2] = -s;
        result.m[8] = s;
        result.m[10] = c;
        return result;
    }

    static Mat4 rotationZ(float angle) {
        Mat4 result = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        result.m[0] = c;
        result.m[1] = s;
        result.m[4] = -s;
        result.m[5] = c;
        return result;
    }

    static Mat4 perspective(float fov, float aspect, float near, float far) {
        Mat4 result{};
        float f = 1.0f / std::tan(fov * 0.5f);
        result.m[0] = f / aspect;
        result.m[5] = f;
        result.m[10] = (far + near) / (near - far);
        result.m[11] = -1.0f;
        result.m[14] = (2.0f * far * near) / (near - far);
        return result;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);

        Mat4 result = identity();
        result.m[0] = s.x;
        result.m[4] = s.y;
        result.m[8] = s.z;
        result.m[1] = u.x;
        result.m[5] = u.y;
        result.m[9] = u.z;
        result.m[2] = -f.x;
        result.m[6] = -f.y;
        result.m[10] = -f.z;
        result.m[12] = -s.dot(eye);
        result.m[13] = -u.dot(eye);
        result.m[14] = f.dot(eye);
        return result;
    }

    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i * 4 + j] = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    result.m[i * 4 + j] += m[i * 4 + k] * other.m[k * 4 + j];
                }
            }
        }
        return result;
    }
};

using Matrix = Mat4;

}; // namespace qc