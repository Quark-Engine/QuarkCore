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

#ifndef __QUARK_MATH__
#define __QUARK_MATH__

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
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
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
    if (max == min) {
        return 0.0f;
    }
    return (value - min) / (max - min);
}

inline float Remap(float value, float inputStart, float inputEnd, float outputStart, float outputEnd) {
    return (value - inputStart) / (inputEnd - inputStart) * (outputEnd - outputStart) + outputStart;
}

inline float Wrap(float value, float min, float max) {
    return value - (max - min) * std::floor((value - min) / (max - min));
}

inline bool FloatEquals(float x, float y) {
    return std::fabs(x - y) <=
        (EPSILON * std::fmax(1.0f, std::fmax(std::fabs(x), std::fabs(y))));
}

/**
 * @brief Move a value towards target by a maximum delta.
 */
inline float MoveTowards(float value, float target, float maxDelta) {
    if (std::fabs(target - value) <= maxDelta) {
        return target;
    }
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

    bool operator==(const Color& c) const {
        return r == c.r && g == c.g && b == c.b && a == c.a;
    }

    bool operator!=(const Color& c) const {
        return !(*this == c);
    }
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
inline constexpr Color MAGENTA{255, 0, 255, 255};
inline constexpr Color CYAN{0, 255, 255, 255};
inline constexpr Color PINK{255, 109, 194, 255};
inline constexpr Color BROWN{127, 106, 79, 255};
inline constexpr Color LIME{0, 158, 47, 255};

/**
 * @brief 2D vector structure.
 */
struct Float3 {
    float v[3]{};
};

struct Float16 {
    float v[16]{};
};

struct Mat4;
struct Quaternion;

template <typename T = float>
struct Vec2T {
    T x = T{};
    T y = T{};

    Vec2T() = default;
    Vec2T(T x, T y) : x(x), y(y) {}

    Vec2T& operator+=(const Vec2T& v) {
        x += v.x;
        y += v.y;
        return *this;
    }

    Vec2T& operator-=(const Vec2T& v) {
        x -= v.x;
        y -= v.y;
        return *this;
    }

    Vec2T& operator*=(T s) {
        x *= s;
        y *= s;
        return *this;
    }

    Vec2T& operator*=(const Vec2T& v) {
        x *= v.x;
        y *= v.y;
        return *this;
    }

    Vec2T& operator/=(T s) {
        x /= s;
        y /= s;
        return *this;
    }

    Vec2T& operator/=(const Vec2T& v) {
        x /= v.x;
        y /= v.y;
        return *this;
    }

    Vec2T operator+(const Vec2T& v) const {
        return Vec2T(x + v.x, y + v.y);
    }

    Vec2T operator-(const Vec2T& v) const {
        return Vec2T(x - v.x, y - v.y);
    }

    Vec2T operator-() const {
        return Vec2T(-x, -y);
    }

    Vec2T operator*(T s) const {
        return Vec2T(x * s, y * s);
    }

    Vec2T operator*(const Vec2T& v) const {
        return Vec2T(x * v.x, y * v.y);
    }

    Vec2T operator/(T s) const {
        return Vec2T(x / s, y / s);
    }

    Vec2T operator/(const Vec2T& v) const {
        return Vec2T(x / v.x, y / v.y);
    }

    bool operator==(const Vec2T& v) const {
        return x == v.x && y == v.y;
    }

    bool operator!=(const Vec2T& v) const {
        return !(*this == v);
    }

    T dot(const Vec2T& v) const {
        return x * v.x + y * v.y;
    }

    Vec2T addValue(T value) const {
        return Vec2T(x + value, y + value);
    }

    Vec2T subtractValue(T value) const {
        return Vec2T(x - value, y - value);
    }

    T lengthSquared() const {
        return x * x + y * y;
    }

    auto length() const {
        return std::sqrt(lengthSquared());
    }

    T cross(const Vec2T& v) const {
        return x * v.y - y * v.x;
    }

    auto distanceSquared(const Vec2T& v) const {
        const auto dx = x - v.x;
        const auto dy = y - v.y;
        return dx * dx + dy * dy;
    }

    auto angle(const Vec2T& v) const {
        return std::atan2(cross(v), dot(v));
    }

    auto lineAngle(const Vec2T& end) const {
        return -std::atan2(end.y - y, end.x - x);
    }
    Vec2T transformed(const Mat4& transform) const;

    Vec2T reflect(const Vec2T& normal) const {
        const auto dotProduct = dot(normal);
        return Vec2T(x - 2 * normal.x * dotProduct, y - 2 * normal.y * dotProduct);
    }

    Vec2T componentMin(const Vec2T& v) const {
        return Vec2T(std::fmin(x, v.x), std::fmin(y, v.y));
    }
    Vec2T componentMax(const Vec2T& v) const {
        return Vec2T(std::fmax(x, v.x), std::fmax(y, v.y));
    }

    Vec2T rotate(T angle) const {
        const auto cosAngle = std::cos(angle);
        const auto sinAngle = std::sin(angle);
        return Vec2T(x * cosAngle - y * sinAngle, x * sinAngle + y * cosAngle);
    }

    Vec2T moveTowards(const Vec2T& target, T maxDistance) const {
        const auto dx = target.x - x;
        const auto dy = target.y - y;
        const auto distanceSquared = dx * dx + dy * dy;
        if (distanceSquared == 0 ||
            (maxDistance >= 0 && distanceSquared <= maxDistance * maxDistance)) {
            return target;
        }
        const auto distance = std::sqrt(distanceSquared);
        return Vec2T(x + dx / distance * maxDistance, y + dy / distance * maxDistance);
    }

    Vec2T inverted() const {
        return Vec2T(T{1} / x, T{1} / y);
    }

    Vec2T clamped(const Vec2T& min, const Vec2T& max) const {
        return Vec2T(std::fmin(max.x, std::fmax(min.x, x)),
                     std::fmin(max.y, std::fmax(min.y, y)));
    }

    Vec2T clampedValue(T min, T max) const {
        const auto magnitudeSquared = lengthSquared();
        if (magnitudeSquared == 0) {
            return *this;
        }
        const auto magnitude = std::sqrt(magnitudeSquared);
        auto scale = 1.0;
        if (magnitude < min) {
            scale = min / magnitude;
        } else if (magnitude > max) {
            scale = max / magnitude;
        }
        return Vec2T(static_cast<T>(x * scale), static_cast<T>(y * scale));
    }

    Vec2T refract(const Vec2T& normal, T ratio) const {
        const auto dotProduct = dot(normal);
        auto discriminant = T{1} - ratio * ratio * (T{1} - dotProduct * dotProduct);
        if (discriminant < 0) {
            return Vec2T{};
        }
        discriminant = static_cast<T>(std::sqrt(discriminant));
        return Vec2T(ratio * x - (ratio * dotProduct + discriminant) * normal.x,
                     ratio * y - (ratio * dotProduct + discriminant) * normal.y);
    }

    Vec2T normalized() const {
        const auto len = length();
        if (len > 0) {
            return *this * static_cast<T>(1.0 / len);
        }
        return *this;
    }
};

template <typename T>
inline Vec2T<T> operator*(T s, const Vec2T<T>& v) {
    return v * s;
}

using Vec2i = Vec2T<int>;
using Vec2f = Vec2T<float>;
using Vec2d = Vec2T<double>;
using Vec2u = Vec2T<unsigned int>;
using Vec2 = Vec2f;
using Vector2 = Vec2f;

template <typename T>
struct Rect {
    T x = T{};
    T y = T{};
    T width = T{};
    T height = T{};

    bool operator==(const Rect& r) const {
        return x == r.x && y == r.y && width == r.width && height == r.height;
    }
    bool operator!=(const Rect& r) const {
        return !(*this == r);
    }
};

using Recti = Rect<int>;
using Rectf = Rect<float>;
using Rectd = Rect<double>;
using Rectu = Rect<unsigned int>;
using Rectangle = Rectf;

/**
 * @brief 3D vector structure.
 */
template <typename T = float>
struct Vec3T {
    T x = T{};
    T y = T{};
    T z = T{};

    Vec3T() = default;
    Vec3T(T x, T y, T z) : x(x), y(y), z(z) {}

    Vec3T& operator+=(const Vec3T& v) {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    Vec3T& operator-=(const Vec3T& v) {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    Vec3T& operator*=(T s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }

    Vec3T& operator*=(const Vec3T& v) {
        x *= v.x;
        y *= v.y;
        z *= v.z;
        return *this;
    }

    Vec3T& operator/=(T s) {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    Vec3T& operator/=(const Vec3T& v) {
        x /= v.x;
        y /= v.y;
        z /= v.z;
        return *this;
    }

    Vec3T operator+(const Vec3T& v) const {
        return Vec3T(x + v.x, y + v.y, z + v.z);
    }

    Vec3T operator-(const Vec3T& v) const {
        return Vec3T(x - v.x, y - v.y, z - v.z);
    }

    Vec3T operator-() const {
        return Vec3T(-x, -y, -z);
    }

    Vec3T operator*(T s) const {
        return Vec3T(x * s, y * s, z * s);
    }

    Vec3T operator*(const Vec3T& v) const {
        return Vec3T(x * v.x, y * v.y, z * v.z);
    }

    Vec3T operator/(T s) const {
        return Vec3T(x / s, y / s, z / s);
    }

    Vec3T operator/(const Vec3T& v) const {
        return Vec3T(x / v.x, y / v.y, z / v.z);
    }

    bool operator==(const Vec3T& v) const {
        return x == v.x && y == v.y && z == v.z;
    }

    bool operator!=(const Vec3T& v) const {
        return !(*this == v);
    }

    T dot(const Vec3T& v) const {
        return x * v.x + y * v.y + z * v.z;
    }

    Vec3T addValue(T value) const {
        return Vec3T(x + value, y + value, z + value);
    }

    Vec3T subtractValue(T value) const {
        return Vec3T(x - value, y - value, z - value);
    }

    Vec3T cross(const Vec3T& v) const {
        return Vec3T(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    }

    Vec3T perpendicular() const {
        T minComponent = static_cast<T>(std::fabs(x));
        Vec3T cardinalAxis{T{1}, T{}, T{}};
        if (std::fabs(y) < minComponent) {
            minComponent = static_cast<T>(std::fabs(y));
            cardinalAxis = Vec3T{T{}, T{1}, T{}};
        }
        if (std::fabs(z) < minComponent) {
            cardinalAxis = Vec3T{T{}, T{}, T{1}};
        }
        return cross(cardinalAxis);
    }

    auto angle(const Vec3T& v) const {
        return std::atan2(cross(v).length(), dot(v));
    }

    Vec3T projectedOnto(const Vec3T& v) const {
        return v * static_cast<T>(dot(v) / v.lengthSquared());
    }

    Vec3T rejectedFrom(const Vec3T& v) const {
        return *this - projectedOnto(v);
    }

    void orthoNormalize(Vec3T& v) {
        auto len = length();
        if (len == 0) {
            len = 1;
        }
        *this *= static_cast<T>(1.0 / len);
        Vec3T normal = cross(v);
        len = normal.length();
        if (len == 0) {
            len = 1;
        }
        normal *= static_cast<T>(1.0 / len);
        v = normal.cross(*this);
    }

    Vec3T rotatedByQuaternion(const Quaternion& q) const;
    Vec3T rotatedByAxisAngle(const Vec3T& axis, float angle) const;

    Vec3T moveTowards(const Vec3T& target, T maxDistance) const {
        const Vec3T delta = target - *this;
        const auto distanceSquared = delta.lengthSquared();
        if (distanceSquared == 0 ||
            (maxDistance >= 0 && distanceSquared <= maxDistance * maxDistance)) {
            return target;
        }
        return *this + delta * static_cast<T>(maxDistance / std::sqrt(distanceSquared));
    }

    Vec3T cubicHermite(const Vec3T& tangent1, const Vec3T& end,
                       const Vec3T& tangent2, float amount) const {
        const float amountSquared = amount * amount;
        const float amountCubed = amountSquared * amount;
        const float h00 = 2.0f * amountCubed - 3.0f * amountSquared + 1.0f;
        const float h10 = amountCubed - 2.0f * amountSquared + amount;
        const float h01 = -2.0f * amountCubed + 3.0f * amountSquared;
        const float h11 = amountCubed - amountSquared;
        return Vec3T(static_cast<T>(h00 * x + h10 * tangent1.x + h01 * end.x + h11 * tangent2.x),
                     static_cast<T>(h00 * y + h10 * tangent1.y + h01 * end.y + h11 * tangent2.y),
                     static_cast<T>(h00 * z + h10 * tangent1.z + h01 * end.z + h11 * tangent2.z));
    }

    Vec3T reflected(const Vec3T& normal) const {
        return *this - normal * (T{2} * dot(normal));
    }
    Vec3T componentMin(const Vec3T& v) const {
        return Vec3T(std::fmin(x, v.x), std::fmin(y, v.y), std::fmin(z, v.z));
    }
    Vec3T componentMax(const Vec3T& v) const {
        return Vec3T(std::fmax(x, v.x), std::fmax(y, v.y), std::fmax(z, v.z));
    }

    Vec3T barycenter(const Vec3T& a, const Vec3T& b, const Vec3T& c) const {
        const Vec3T v0 = b - a;
        const Vec3T v1 = c - a;
        const Vec3T v2 = *this - a;
        const auto d00 = v0.dot(v0);
        const auto d01 = v0.dot(v1);
        const auto d11 = v1.dot(v1);
        const auto d20 = v2.dot(v0);
        const auto d21 = v2.dot(v1);
        const auto denominator = d00 * d11 - d01 * d01;
        const auto v = (d11 * d20 - d01 * d21) / denominator;
        const auto w = (d00 * d21 - d01 * d20) / denominator;
        return Vec3T(static_cast<T>(1 - (w + v)), static_cast<T>(v), static_cast<T>(w));
    }

    Vec3T unprojected(const Mat4& projection, const Mat4& view) const;
    Float3 toFloatV() const {
        return Float3{{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}};
    }

    Vec3T inverted() const {
        return Vec3T(T{1} / x, T{1} / y, T{1} / z);
    }
    Vec3T clamped(const Vec3T& min, const Vec3T& max) const {
        return Vec3T(std::fmin(max.x, std::fmax(min.x, x)),
                     std::fmin(max.y, std::fmax(min.y, y)),
                     std::fmin(max.z, std::fmax(min.z, z)));
    }

    Vec3T clampedValue(T min, T max) const {
        const auto magnitudeSquared = lengthSquared();
        if (magnitudeSquared == 0) {
            return *this;
        }
        const auto magnitude = std::sqrt(magnitudeSquared);
        auto scale = 1.0;
        if (magnitude < min) {
            scale = min / magnitude;
        } else if (magnitude > max) {
            scale = max / magnitude;
        }
        return Vec3T(static_cast<T>(x * scale), static_cast<T>(y * scale), static_cast<T>(z * scale));
    }

    Vec3T refracted(const Vec3T& normal, T ratio) const {
        const auto dotProduct = dot(normal);
        auto discriminant = T{1} - ratio * ratio * (T{1} - dotProduct * dotProduct);
        if (discriminant < 0) {
            return Vec3T{};
        }
        discriminant = static_cast<T>(std::sqrt(discriminant));
        return *this * ratio - normal * (ratio * dotProduct + discriminant);
    }

    T lengthSquared() const {
        return x * x + y * y + z * z;
    }

    auto length() const {
        return std::sqrt(lengthSquared());
    }

    Vec3T normalized() const {
        const auto len = length();
        if (len > 0) {
            return *this * static_cast<T>(1.0 / len);
        }
        return *this;
    }
};

template <typename T>
inline Vec3T<T> operator*(T s, const Vec3T<T>& v) {
    return v * s;
}

using Vec3i = Vec3T<int>;
using Vec3f = Vec3T<float>;
using Vec3d = Vec3T<double>;
using Vec3u = Vec3T<unsigned int>;
using Vec3 = Vec3f;
using Vector3 = Vec3f;

/**
 * @brief 4D vector structure.
 */
template <typename T = float>
struct Vec4T {
    T x = T{};
    T y = T{};
    T z = T{};
    T w = T{};

    Vec4T() = default;
    Vec4T(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {}

    Vec4T& operator+=(const Vec4T& v) {
        x += v.x;
        y += v.y;
        z += v.z;
        w += v.w;
        return *this;
    }

    Vec4T& operator-=(const Vec4T& v) {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        w -= v.w;
        return *this;
    }

    Vec4T& operator*=(T s) {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }

    Vec4T& operator*=(const Vec4T& v) {
        x *= v.x;
        y *= v.y;
        z *= v.z;
        w *= v.w;
        return *this;
    }

    Vec4T& operator/=(T s) {
        x /= s;
        y /= s;
        z /= s;
        w /= s;
        return *this;
    }

    Vec4T& operator/=(const Vec4T& v) {
        x /= v.x;
        y /= v.y;
        z /= v.z;
        w /= v.w;
        return *this;
    }

    Vec4T operator+(const Vec4T& v) const {
        return Vec4T(x + v.x, y + v.y, z + v.z, w + v.w);
    }

    Vec4T operator-(const Vec4T& v) const {
        return Vec4T(x - v.x, y - v.y, z - v.z, w - v.w);
    }

    Vec4T operator-() const {
        return Vec4T(-x, -y, -z, -w);
    }

    Vec4T operator*(T s) const {
        return Vec4T(x * s, y * s, z * s, w * s);
    }

    Vec4T operator*(const Vec4T& v) const {
        return Vec4T(x * v.x, y * v.y, z * v.z, w * v.w);
    }

    Vec4T operator/(T s) const {
        return Vec4T(x / s, y / s, z / s, w / s);
    }

    Vec4T operator/(const Vec4T& v) const {
        return Vec4T(x / v.x, y / v.y, z / v.z, w / v.w);
    }

    bool operator==(const Vec4T& v) const {
        return x == v.x && y == v.y && z == v.z && w == v.w;
    }

    bool operator!=(const Vec4T& v) const {
        return !(*this == v);
    }

    T dot(const Vec4T& v) const {
        return x * v.x + y * v.y + z * v.z + w * v.w;
    }

    auto length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    Vec4T normalized() const {
        const auto len = length();
        if (len > 0) {
            return *this * static_cast<T>(1.0 / len);
        }
        return *this;
    }
};

template <typename T>
inline Vec4T<T> operator*(T s, const Vec4T<T>& v) {
    return v * s;
}

using Vec4i = Vec4T<int>;
using Vec4f = Vec4T<float>;
using Vec4d = Vec4T<double>;
using Vec4u = Vec4T<unsigned int>;
using Vec4 = Vec4f;
using Vector4 = Vec4f;

/**
 * @brief Bounding box structure.
 */
struct BoundingBox {
    Vec3f min{0.0f, 0.0f, 0.0f};
    Vec3f max{0.0f, 0.0f, 0.0f};
};

template <typename T>
inline Vec2T<T> Lerp(const Vec2T<T>& start, const Vec2T<T>& end, float amount) {
    return Vec2T<T>{
        static_cast<T>(Lerp(start.x, end.x, amount)),
        static_cast<T>(Lerp(start.y, end.y, amount))
    };
}

template <typename T>
inline Vec3T<T> Lerp(const Vec3T<T>& start, const Vec3T<T>& end, float amount) {
    return Vec3T<T>{
        static_cast<T>(Lerp(start.x, end.x, amount)),
        static_cast<T>(Lerp(start.y, end.y, amount)),
        static_cast<T>(Lerp(start.z, end.z, amount))
    };
}

template <typename T>
inline Vec4T<T> Lerp(const Vec4T<T>& start, const Vec4T<T>& end, float amount) {
    return Vec4T<T>{
        static_cast<T>(Lerp(start.x, end.x, amount)),
        static_cast<T>(Lerp(start.y, end.y, amount)),
        static_cast<T>(Lerp(start.z, end.z, amount)),
        static_cast<T>(Lerp(start.w, end.w, amount))
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
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
    union {
        float m[16];
        float m16[16];
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
        struct {
            float m0, m1, m2, m3;
            float m4, m5, m6, m7;
            float m8, m9, m10, m11;
            float m12, m13, m14, m15;
        };
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    Mat4() {
        for (int i = 0; i < 16; i++) {
            m[i] = 0.0f;
        }
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
        result.m[15] = 0.0f;
        return result;
    }

    static Mat4 perspectiveVulkan(float fov, float aspect, float near, float far) {
        Mat4 result{};
        float f = 1.0f / std::tan(fov * 0.5f);
        result.m[0] = f / aspect;
        result.m[5] = -f;
        result.m[10] = far / (near - far);
        result.m[11] = -1.0f;
        result.m[14] = (far * near) / (near - far);
        result.m[15] = 0.0f;
        return result;
    }

    static Mat4 lookAt(const Vec3f& eye, const Vec3f& center, const Vec3f& up) {
        Vec3f f = (center - eye).normalized();
        Vec3f s = f.cross(up).normalized();
        Vec3f u = s.cross(f);

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

    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
        Mat4 result;
        result.m[0]  =  2.0f / (right - left);
        result.m[5]  =  2.0f / (top - bottom);
        result.m[10] = -2.0f / (far - near);
        result.m[12] = -(right + left) / (right - left);
        result.m[13] = -(top + bottom) / (top - bottom);
        result.m[14] = -(far + near) / (far - near);
        result.m[15] =  1.0f;
        return result;

    }

    Mat4 inverted() const {
        Mat4 result{};

        float A2323 = m[10] * m[15] - m[11] * m[14];
        float A1323 = m[9]  * m[15] - m[11] * m[13];
        float A1223 = m[9]  * m[14] - m[10] * m[13];
        float A0323 = m[8]  * m[15] - m[11] * m[12];
        float A0223 = m[8]  * m[14] - m[10] * m[12];
        float A0123 = m[8]  * m[13] - m[9]  * m[12];
        float A2313 = m[6]  * m[15] - m[7]  * m[14];
        float A1313 = m[5]  * m[15] - m[7]  * m[13];
        float A1213 = m[5]  * m[14] - m[6]  * m[13];
        float A2312 = m[6]  * m[11] - m[7]  * m[10];
        float A1312 = m[5]  * m[11] - m[7]  * m[9];
        float A1212 = m[5]  * m[10] - m[6]  * m[9];
        float A0313 = m[4]  * m[15] - m[7]  * m[12];
        float A0213 = m[4]  * m[14] - m[6]  * m[12];
        float A0312 = m[4]  * m[11] - m[7]  * m[8];
        float A0212 = m[4]  * m[10] - m[6]  * m[8];
        float A0113 = m[4]  * m[13] - m[5]  * m[12];
        float A0112 = m[4]  * m[9]  - m[5]  * m[8];
        float A0012 = m[4]  * m[9]  - m[5]  * m[8];

        float det =
            m[0] * (m[5] * A2323 - m[6] * A1323 + m[7] * A1223)
        -m[1] * (m[4] * A2323 - m[6] * A0323 + m[7] * A0223)
        +m[2] * (m[4] * A1323 - m[5] * A0323 + m[7] * A0123)
        -m[3] * (m[4] * A1223 - m[5] * A0223 + m[6] * A0123);

        if (std::fabs(det) <= EPSILON) {
            return result;
        }

        float invDet = 1.0f / det;

        result.m[0]  =  invDet * (m[5] * A2323 - m[6] * A1323 + m[7] * A1223);
        result.m[1]  = -invDet * (m[1] * A2323 - m[2] * A1323 + m[3] * A1223);
        result.m[2]  =  invDet * (m[1] * A2313 - m[2] * A1313 + m[3] * A1213);
        result.m[3]  = -invDet * (m[1] * A2312 - m[2] * A1312 + m[3] * A1212);

        result.m[4]  = -invDet * (m[4] * A2323 - m[6] * A0323 + m[7] * A0223);
        result.m[5]  =  invDet * (m[0] * A2323 - m[2] * A0323 + m[3] * A0223);
        result.m[6]  = -invDet * (m[0] * A2313 - m[2] * A0313 + m[3] * A0213);
        result.m[7]  =  invDet * (m[0] * A2312 - m[2] * A0312 + m[3] * A0212);

        result.m[8]  =  invDet * (m[4] * A1323 - m[5] * A0323 + m[7] * A0123);
        result.m[9]  = -invDet * (m[0] * A1323 - m[1] * A0323 + m[3] * A0123);
        result.m[10] =  invDet * (m[0] * A1313 - m[1] * A0313 + m[3] * A0113);
        result.m[11] = -invDet * (m[0] * A1312 - m[1] * A0312 + m[3] * A0112);

        result.m[12] = -invDet * (m[4] * A1223 - m[5] * A0223 + m[6] * A0123);
        result.m[13] =  invDet * (m[0] * A1223 - m[1] * A0223 + m[2] * A0123);
        result.m[14] = -invDet * (m[0] * A1213 - m[1] * A0213 + m[2] * A0113);
        result.m[15] =  invDet * (m[0] * A1212 - m[1] * A0112 + m[2] * A0012);

        return result;
    }

    Mat4& operator*=(float s) {
        for (int i = 0; i < 16; ++i) {
            m[i] *= s;
        }
        return *this;
    }

    Mat4& operator/=(float s) {
        for (int i = 0; i < 16; ++i) {
            m[i] /= s;
        }
        return *this;
    }

    Mat4& operator*=(const Mat4& other) {
        *this = *this * other;
        return *this;
    }

    Mat4 operator*(float s) const {
        Mat4 result = *this;
        result *= s;
        return result;
    }

    Mat4 operator/(float s) const {
        Mat4 result = *this;
        result /= s;
        return result;
    }

    Mat4 operator+(const Mat4& other) const {
        Mat4 result{};
        for (int i = 0; i < 16; ++i) {
            result.m[i] = m[i] + other.m[i];
        }
        return result;
    }

    Mat4 operator-(const Mat4& other) const {
        Mat4 result{};
        for (int i = 0; i < 16; ++i) {
            result.m[i] = m[i] - other.m[i];
        }
        return result;
    }

    bool operator==(const Mat4& other) const {
        for (int i = 0; i < 16; ++i) {
            if (m[i] != other.m[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const Mat4& other) const {
        return !(*this == other);
    }

    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                result.m[column * 4 + row] = 0.0f;
                for (int index = 0; index < 4; ++index) {
                    result.m[column * 4 + row] +=
                        m[index * 4 + row] * other.m[column * 4 + index];
                }
            }
        }
        return result;
    }

    Vec2f operator*(const Vec2f& v) const {
        return Vec2f(
            m[0] * v.x + m[4] * v.y + m[12],
            m[1] * v.x + m[5] * v.y + m[13]
        );
    }

    Vec3f operator*(const Vec3f& other) const {
        return Vec3f(
            m[0] * other.x + m[4] * other.y + m[8]  * other.z + m[12],
            m[1] * other.x + m[5] * other.y + m[9]  * other.z + m[13],
            m[2] * other.x + m[6] * other.y + m[10] * other.z + m[14]
        );
    }

    Vec4f operator*(const Vec4f& other) const {
        return Vec4f(
            m[0] * other.x + m[4] * other.y + m[8]  * other.z + m[12] * other.w,
            m[1] * other.x + m[5] * other.y + m[9]  * other.z + m[13] * other.w,
            m[2] * other.x + m[6] * other.y + m[10] * other.z + m[14] * other.w,
            m[3] * other.x + m[7] * other.y + m[11] * other.z + m[15] * other.w
        );
    }
};

template <typename T>
inline Vec2T<T> Vec2T<T>::transformed(const Mat4& transform) const {
    return Vec2T(static_cast<T>(transform.m[0] * x + transform.m[4] * y + transform.m[12]),
                 static_cast<T>(transform.m[1] * x + transform.m[5] * y + transform.m[13]));
}

template <typename T>
inline Vec3T<T> Vec3T<T>::unprojected(const Mat4& projection, const Mat4& view) const {
    const Vec4f transformed = (projection * view).inverted() *
        Vec4f(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 1.0f);
    return Vec3T(static_cast<T>(transformed.x / transformed.w),
                 static_cast<T>(transformed.y / transformed.w),
                 static_cast<T>(transformed.z / transformed.w));
}

inline Mat4 operator*(float s, const Mat4& m) {
    return m * s;
}

using Matrix = Mat4;

/**
 * @brief Quaternion structure.
 */
struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    Quaternion() = default;
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    Quaternion& operator+=(const Quaternion& q) {
        x += q.x;
        y += q.y;
        z += q.z;
        w += q.w;
        return *this;
    }

    Quaternion& operator-=(const Quaternion& q) {
        x -= q.x;
        y -= q.y;
        z -= q.z;
        w -= q.w;
        return *this;
    }

    Quaternion& operator*=(float s) {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }

    Quaternion& operator/=(float s) {
        x /= s;
        y /= s;
        z /= s;
        w /= s;
        return *this;
    }

    Quaternion operator+(const Quaternion& q) const {
        return Quaternion(x + q.x, y + q.y, z + q.z, w + q.w);
    }

    Quaternion operator-(const Quaternion& q) const {
        return Quaternion(x - q.x, y - q.y, z - q.z, w - q.w);
    }

    Quaternion operator-() const {
        return Quaternion(-x, -y, -z, -w);
    }

    Quaternion operator*(float s) const {
        return Quaternion(x * s, y * s, z * s, w * s);
    }

    Quaternion operator/(float s) const {
        return Quaternion(x / s, y / s, z / s, w / s);
    }

    bool operator==(const Quaternion& q) const {
        return x == q.x && y == q.y && z == q.z && w == q.w;
    }

    bool operator!=(const Quaternion& q) const {
        return !(*this == q);
    }
};

inline Quaternion operator*(float s, const Quaternion& q) {
    return q * s;
}

template <typename T>
inline Vec3T<T> Vec3T<T>::rotatedByQuaternion(const Quaternion& q) const {
    return Vec3T(
        x * (q.x*q.x + q.w*q.w - q.y*q.y - q.z*q.z) +
            y * (2*q.x*q.y - 2*q.w*q.z) + z * (2*q.x*q.z + 2*q.w*q.y),
        x * (2*q.w*q.z + 2*q.x*q.y) +
            y * (q.w*q.w - q.x*q.x + q.y*q.y - q.z*q.z) +
            z * (-2*q.w*q.x + 2*q.y*q.z),
        x * (-2*q.w*q.y + 2*q.x*q.z) +
            y * (2*q.w*q.x + 2*q.y*q.z) +
            z * (q.w*q.w - q.x*q.x - q.y*q.y + q.z*q.z)
    );
}

template <typename T>
inline Vec3T<T> Vec3T<T>::rotatedByAxisAngle(const Vec3T& axis, float angle) const {
    float axisLength = axis.length();
    if (axisLength == 0.0f) {
        axisLength = 1.0f;
    }
    const Vec3T normalizedAxis = axis * static_cast<T>(1.0f / axisLength);
    const float halfAngle = angle * 0.5f;
    const Vec3T quaternionVector = normalizedAxis * static_cast<T>(std::sin(halfAngle));
    const float quaternionScalar = std::cos(halfAngle);
    const Vec3T firstCross = quaternionVector.cross(*this);
    const Vec3T secondCross = quaternionVector.cross(firstCross);
    return *this + firstCross * static_cast<T>(2.0f * quaternionScalar) + secondCross * T{2};
}

using Quat = Quaternion;

inline float QuaternionLength(const Quaternion& q) {
    return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

inline Quaternion QuaternionNormalize(const Quaternion& q) {
    const float length = QuaternionLength(q);
    if (length == 0.0f) {
        return Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float ilength = 1.0f / length;
    return Quaternion{q.x * ilength, q.y * ilength, q.z * ilength, q.w * ilength};
}

inline Quaternion QuaternionAddValue(const Quaternion& q, float add) {
    return Quaternion{
        q.x + add,
        q.y + add,
        q.z + add,
        q.w + add
    };
}

inline Quaternion QuaternionSubtractValue(const Quaternion& q, float subtract) {
    return Quaternion{
        q.x - subtract,
        q.y - subtract,
        q.z - subtract,
        q.w - subtract
    };
}

inline Quaternion QuaternionInvert(const Quaternion& q) {
    const float lengthSquared =
        q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;

    if (lengthSquared == 0.0f) {
        return q;
    }

    const float inverseLengthSquared = 1.0f / lengthSquared;
    return Quaternion{
        -q.x * inverseLengthSquared,
        -q.y * inverseLengthSquared,
        -q.z * inverseLengthSquared,
        q.w * inverseLengthSquared
    };
}

inline Quaternion QuaternionMultiply(const Quaternion& q1, const Quaternion& q2) {
    Quaternion result;
    result.x = q1.x*q2.w + q1.w*q2.x + q1.y*q2.z - q1.z*q2.y;
    result.y = q1.y*q2.w + q1.w*q2.y + q1.z*q2.x - q1.x*q2.z;
    result.z = q1.z*q2.w + q1.w*q2.z + q1.x*q2.y - q1.y*q2.x;
    result.w = q1.w*q2.w - q1.x*q2.x - q1.y*q2.y - q1.z*q2.z;
    return result;
}

inline Quaternion operator*(const Quaternion& q1, const Quaternion& q2) {
    return QuaternionMultiply(q1, q2);
}

inline Quaternion QuaternionLerp(const Quaternion& q1, const Quaternion& q2, float amount) {
    Quaternion result;
    result.x = q1.x + amount*(q2.x - q1.x);
    result.y = q1.y + amount*(q2.y - q1.y);
    result.z = q1.z + amount*(q2.z - q1.z);
    result.w = q1.w + amount*(q2.w - q1.w);
    return QuaternionNormalize(result);
}

inline Quaternion QuaternionNlerp(const Quaternion& q1, const Quaternion& q2, float amount) {
    Quaternion result{
        q1.x + amount * (q2.x - q1.x),
        q1.y + amount * (q2.y - q1.y),
        q1.z + amount * (q2.z - q1.z),
        q1.w + amount * (q2.w - q1.w)
    };

    float length = QuaternionLength(result);
    if (length == 0.0f) {
        length = 1.0f;
    }

    const float inverseLength = 1.0f / length;
    result.x *= inverseLength;
    result.y *= inverseLength;
    result.z *= inverseLength;
    result.w *= inverseLength;
    return result;
}

inline Quaternion QuaternionSlerp(const Quaternion& q1, const Quaternion& q2, float amount) {
    Quaternion from = q1;
    Quaternion to = q2;

    float cosom = from.x*to.x + from.y*to.y + from.z*to.z + from.w*to.w;
    if (cosom < 0.0f) {
        to.x = -to.x;
        to.y = -to.y;
        to.z = -to.z;
        to.w = -to.w;
        cosom = -cosom;
    }

    float scale0, scale1;
    if (cosom > 0.999999f) {
        scale0 = 1.0f - amount;
        scale1 = amount;
    } else {
        const float omega = std::acos(cosom);
        const float invSin = 1.0f / std::sin(omega);
        scale0 = std::sin((1.0f - amount) * omega) * invSin;
        scale1 = std::sin(amount * omega) * invSin;
    }

    Quaternion result;
    result.x = scale0*from.x + scale1*to.x;
    result.y = scale0*from.y + scale1*to.y;
    result.z = scale0*from.z + scale1*to.z;
    result.w = scale0*from.w + scale1*to.w;
    return result;
}

inline Quaternion QuaternionCubicHermiteSpline(
    const Quaternion& q1,
    const Quaternion& outTangent1,
    const Quaternion& q2,
    const Quaternion& inTangent2,
    float amount) {
    const float amountSquared = amount * amount;
    const float amountCubed = amountSquared * amount;
    const float h00 = 2.0f * amountCubed - 3.0f * amountSquared + 1.0f;
    const float h10 = amountCubed - 2.0f * amountSquared + amount;
    const float h01 = -2.0f * amountCubed + 3.0f * amountSquared;
    const float h11 = amountCubed - amountSquared;

    Quaternion result =
        q1 * h00 +
        outTangent1 * h10 +
        q2 * h01 +
        inTangent2 * h11;

    float length = QuaternionLength(result);
    if (length == 0.0f) {
        length = 1.0f;
    }

    result *= 1.0f / length;
    return result;
}

inline Quaternion QuaternionFromVector3ToVector3(const Vec3f& from, const Vec3f& to) {
    const Vec3f cross = from.cross(to);
    const float dot = from.dot(to);

    Quaternion result{
        cross.x,
        cross.y,
        cross.z,
        std::sqrt(cross.lengthSquared() + dot * dot) + dot
    };

    float length = QuaternionLength(result);
    if (length == 0.0f) {
        length = 1.0f;
    }

    result *= 1.0f / length;
    return result;
}

inline void QuaternionToAxisAngle(
    Quaternion q,
    Vec3f* outAxis,
    float* outAngle) {
    if (std::fabs(q.w) > 1.0f) {
        q = QuaternionNormalize(q);
    }

    Vec3f axis{};
    const float angle = 2.0f * std::acos(q.w);
    const float denominator = std::sqrt(1.0f - q.w * q.w);

    if (denominator > EPSILON) {
        axis.x = q.x / denominator;
        axis.y = q.y / denominator;
        axis.z = q.z / denominator;
    } else {
        axis.x = 1.0f;
    }

    *outAxis = axis;
    *outAngle = angle;
}

inline Quaternion QuaternionFromEuler(float pitch, float yaw, float roll) {
    const float xCos = std::cos(pitch * 0.5f);
    const float xSin = std::sin(pitch * 0.5f);
    const float yCos = std::cos(yaw * 0.5f);
    const float ySin = std::sin(yaw * 0.5f);
    const float zCos = std::cos(roll * 0.5f);
    const float zSin = std::sin(roll * 0.5f);

    return Quaternion{
        xSin * yCos * zCos - xCos * ySin * zSin,
        xCos * ySin * zCos + xSin * yCos * zSin,
        xCos * yCos * zSin - xSin * ySin * zCos,
        xCos * yCos * zCos + xSin * ySin * zSin
    };
}

inline Quaternion QuaternionFromAxisAngle(const Vec3f& axis, float angle) {
    Quaternion result;
    const float half = angle * 0.5f;
    const float s = std::sin(half);
    result.x = axis.x * s;
    result.y = axis.y * s;
    result.z = axis.z * s;
    result.w = std::cos(half);
    return result;
}

inline Quaternion QuaternionFromMatrix(const Mat4& m) {
    Quaternion result;
    const float trace = m.m[0] + m.m[5] + m.m[10];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        result.w = 0.25f * s;
        result.x = (m.m[9] - m.m[6]) / s;
        result.y = (m.m[2] - m.m[8]) / s;
        result.z = (m.m[4] - m.m[1]) / s;
    } else if ((m.m[0] > m.m[5]) && (m.m[0] > m.m[10])) {
        const float s = std::sqrt(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
        result.w = (m.m[9] - m.m[6]) / s;
        result.x = 0.25f * s;
        result.y = (m.m[1] + m.m[4]) / s;
        result.z = (m.m[2] + m.m[8]) / s;
    } else if (m.m[5] > m.m[10]) {
        const float s = std::sqrt(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
        result.w = (m.m[2] - m.m[8]) / s;
        result.x = (m.m[1] + m.m[4]) / s;
        result.y = 0.25f * s;
        result.z = (m.m[6] + m.m[9]) / s;
    } else {
        const float s = std::sqrt(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
        result.w = (m.m[4] - m.m[1]) / s;
        result.x = (m.m[2] + m.m[8]) / s;
        result.y = (m.m[6] + m.m[9]) / s;
        result.z = 0.25f * s;
    }
    return result;
}

inline Mat4 QuaternionToMatrix(const Quaternion& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x*x2, xy = x*y2, xz = x*z2;
    const float yy = y*y2, yz = y*z2, zz = z*z2;
    const float wx = w*x2, wy = w*y2, wz = w*z2;

    Mat4 result;
    result.m[0] = 1.0f - (yy + zz);
    result.m[1] = xy + wz;
    result.m[2] = xz - wy;
    result.m[3] = 0.0f;
    result.m[4] = xy - wz;
    result.m[5] = 1.0f - (xx + zz);
    result.m[6] = yz + wx;
    result.m[7] = 0.0f;
    result.m[8] = xz + wy;
    result.m[9] = yz - wx;
    result.m[10] = 1.0f - (xx + yy);
    result.m[11] = 0.0f;
    result.m[12] = 0.0f;
    result.m[13] = 0.0f;
    result.m[14] = 0.0f;
    result.m[15] = 1.0f;
    return result;
}

inline Vec3f QuaternionToEuler(const Quaternion& q) {
    const float rollNumerator = 2.0f * (q.w * q.x + q.y * q.z);
    const float rollDenominator = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    const float pitchSin = Clamp(2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
    const float yawNumerator = 2.0f * (q.w * q.z + q.x * q.y);
    const float yawDenominator = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);

    return Vec3f{
        std::atan2(rollNumerator, rollDenominator),
        std::asin(pitchSin),
        std::atan2(yawNumerator, yawDenominator)
    };
}

inline Quaternion QuaternionTransform(const Quaternion& q, const Mat4& matrix) {
    return Quaternion{
        matrix.m[0] * q.x + matrix.m[4] * q.y + matrix.m[8] * q.z + matrix.m[12] * q.w,
        matrix.m[1] * q.x + matrix.m[5] * q.y + matrix.m[9] * q.z + matrix.m[13] * q.w,
        matrix.m[2] * q.x + matrix.m[6] * q.y + matrix.m[10] * q.z + matrix.m[14] * q.w,
        matrix.m[3] * q.x + matrix.m[7] * q.y + matrix.m[11] * q.z + matrix.m[15] * q.w
    };
}

inline void MatrixDecompose(
    const Mat4& matrix,
    Vec3f* translation,
    Quaternion* rotation,
    Vec3f* scale) {
    constexpr float epsilon = 1e-9f;

    translation->x = matrix.m[12];
    translation->y = matrix.m[13];
    translation->z = matrix.m[14];

    Vec3f columns[3] = {
        {matrix.m[0], matrix.m[4], matrix.m[8]},
        {matrix.m[1], matrix.m[5], matrix.m[9]},
        {matrix.m[2], matrix.m[6], matrix.m[10]}
    };

    float shearXY = 0.0f;
    float shearXZ = 0.0f;
    float shearYZ = 0.0f;
    Vec3f extractedScale{};

    float stabilizer = epsilon;
    for (const Vec3f& column : columns) {
        stabilizer = std::fmax(stabilizer, std::fabs(column.x));
        stabilizer = std::fmax(stabilizer, std::fabs(column.y));
        stabilizer = std::fmax(stabilizer, std::fabs(column.z));
    }

    const float inverseStabilizer = 1.0f / stabilizer;
    for (Vec3f& column : columns) {
        column *= inverseStabilizer;
    }

    extractedScale.x = columns[0].length();
    if (extractedScale.x > epsilon) {
        columns[0] *= 1.0f / extractedScale.x;
    }

    shearXY = columns[0].dot(columns[1]);
    columns[1] -= columns[0] * shearXY;
    extractedScale.y = columns[1].length();
    if (extractedScale.y > epsilon) {
        columns[1] *= 1.0f / extractedScale.y;
        shearXY /= extractedScale.y;
    }

    shearXZ = columns[0].dot(columns[2]);
    columns[2] -= columns[0] * shearXZ;
    shearYZ = columns[1].dot(columns[2]);
    columns[2] -= columns[1] * shearYZ;
    extractedScale.z = columns[2].length();
    if (extractedScale.z > epsilon) {
        columns[2] *= 1.0f / extractedScale.z;
        shearXZ /= extractedScale.z;
        shearYZ /= extractedScale.z;
    }

    if (columns[0].dot(columns[1].cross(columns[2])) < 0.0f) {
        extractedScale = -extractedScale;
        columns[0] = -columns[0];
        columns[1] = -columns[1];
        columns[2] = -columns[2];
    }

    *scale = extractedScale * stabilizer;

    Mat4 rotationMatrix{};
    rotationMatrix.m[0] = columns[0].x;
    rotationMatrix.m[1] = columns[0].y;
    rotationMatrix.m[2] = columns[0].z;
    rotationMatrix.m[4] = columns[1].x;
    rotationMatrix.m[5] = columns[1].y;
    rotationMatrix.m[6] = columns[1].z;
    rotationMatrix.m[8] = columns[2].x;
    rotationMatrix.m[9] = columns[2].y;
    rotationMatrix.m[10] = columns[2].z;
    *rotation = QuaternionFromMatrix(rotationMatrix);
}

inline Mat4 TransformToMatrix(const Vec3f& translation, const Quaternion& rotation, const Vec3f& scale) {
    Mat4 rot = QuaternionToMatrix(rotation);
    Mat4 result;
    result.m[0] = rot.m[0] * scale.x;
    result.m[1] = rot.m[1] * scale.x;
    result.m[2] = rot.m[2] * scale.x;
    result.m[4] = rot.m[4] * scale.y;
    result.m[5] = rot.m[5] * scale.y;
    result.m[6] = rot.m[6] * scale.y;
    result.m[8] = rot.m[8] * scale.z;
    result.m[9] = rot.m[9] * scale.z;
    result.m[10] = rot.m[10] * scale.z;
    result.m[3] = result.m[7] = result.m[11] = 0.0f;
    result.m[12] = translation.x;
    result.m[13] = translation.y;
    result.m[14] = translation.z;
    result.m[15] = 1.0f;
    return result;
}

template <typename T>
inline Vec2T<T> Vec2Zero() {
    return Vec2T<T>{T{}, T{}};
}

inline Vec2f Vec2Zero() {
    return Vec2f{};
}

template <typename T>
inline Vec2T<T> Vec2One() {
    return Vec2T<T>{T{1}, T{1}};
}

inline Vec2f Vec2One() {
    return Vec2f{1.0f, 1.0f};
}

template <typename T>
inline Vec2T<T> Vec2Add(const Vec2T<T>& left, const Vec2T<T>& right) {
    return left + right;
}

template <typename T>
inline Vec2T<T> Vec2Subtract(const Vec2T<T>& left, const Vec2T<T>& right) {
    return left - right;
}

template <typename T>
inline Vec2T<T> Vec2Scale(const Vec2T<T>& value, T scale) {
    return value * scale;
}

template <typename T>
inline auto Vec2Length(const Vec2T<T>& value) {
    return value.length();
}

template <typename T>
inline Vec2T<T> Vec2Normalize(const Vec2T<T>& value) {
    return value.normalized();
}

template <typename T>
inline auto Vec2Distance(const Vec2T<T>& left, const Vec2T<T>& right) {
    return std::sqrt(left.distanceSquared(right));
}

template <typename T>
inline Vec2T<T> Vec2AddValue(const Vec2T<T>& value, T add) {
    return value.addValue(add);
}

template <typename T>
inline Vec2T<T> Vec2SubtractValue(const Vec2T<T>& value, T subtract) {
    return value.subtractValue(subtract);
}

template <typename T>
inline T Vec2LengthSqr(const Vec2T<T>& value) {
    return value.lengthSquared();
}

template <typename T>
inline bool Vec2Equals(const Vec2T<T>& left, const Vec2T<T>& right) {
    const auto componentEquals = [](T a, T b) {
        const double leftValue = static_cast<double>(a);
        const double rightValue = static_cast<double>(b);
        const double tolerance = EPSILON * std::fmax(
            1.0,
            std::fmax(std::fabs(leftValue), std::fabs(rightValue)));
        return std::fabs(leftValue - rightValue) <= tolerance;
    };

    return componentEquals(left.x, right.x) &&
           componentEquals(left.y, right.y);
}

template <typename T>
inline T Vec2CrossProduct(const Vec2T<T>& left, const Vec2T<T>& right) {
    return left.cross(right);
}

template <typename T>
inline auto Vec2DistanceSqr(const Vec2T<T>& left, const Vec2T<T>& right) {
    return left.distanceSquared(right);
}

template <typename T>
inline auto Vec2Angle(const Vec2T<T>& left, const Vec2T<T>& right) {
    return left.angle(right);
}

template <typename T>
inline auto Vec2LineAngle(const Vec2T<T>& start, const Vec2T<T>& end) {
    return start.lineAngle(end);
}

template <typename T>
inline Vec2T<T> Vec2Transform(const Vec2T<T>& value, const Mat4& transform) {
    return value.transformed(transform);
}

template <typename T>
inline Vec2T<T> Vec2Reflect(const Vec2T<T>& value, const Vec2T<T>& normal) {
    return value.reflect(normal);
}

template <typename T>
inline Vec2T<T> Vec2Min(const Vec2T<T>& left, const Vec2T<T>& right) {
    return left.componentMin(right);
}

template <typename T>
inline Vec2T<T> Vec2Max(const Vec2T<T>& left, const Vec2T<T>& right) {
    return left.componentMax(right);
}

template <typename T>
inline Vec2T<T> Vec2Rotate(const Vec2T<T>& value, T angle) {
    return value.rotate(angle);
}

template <typename T>
inline Vec2T<T> Vec2MoveTowards(const Vec2T<T>& value, const Vec2T<T>& target, T maxDistance) {
    return value.moveTowards(target, maxDistance);
}

template <typename T>
inline Vec2T<T> Vec2Invert(const Vec2T<T>& value) {
    return value.inverted();
}

template <typename T>
inline Vec2T<T> Vec2Clamp(const Vec2T<T>& value, const Vec2T<T>& min, const Vec2T<T>& max) {
    return value.clamped(min, max);
}

template <typename T>
inline Vec2T<T> Vec2ClampValue(const Vec2T<T>& value, T min, T max) {
    return value.clampedValue(min, max);
}

template <typename T>
inline Vec2T<T> Vec2Refract(const Vec2T<T>& value, const Vec2T<T>& normal, T ratio) {
    return value.refract(normal, ratio);
}

template <typename T>
inline Vec3T<T> Vec3Zero() {
    return Vec3T<T>{T{}, T{}, T{}};
}

inline Vec3f Vec3Zero() {
    return Vec3f{};
}

template <typename T>
inline Vec3T<T> Vec3One() {
    return Vec3T<T>{T{1}, T{1}, T{1}};
}

inline Vec3f Vec3One() {
    return Vec3f{1.0f, 1.0f, 1.0f};
}

template <typename T>
inline Vec3T<T> Vec3Add(const Vec3T<T>& left, const Vec3T<T>& right) {
    return left + right;
}

inline Mat4 Mat4Identity() {
    return Mat4::identity();
}

inline Mat4 MatrixIdentity() {
    return Mat4Identity();
}

inline Mat4 Mat4Translate(float x, float y, float z) {
    return Mat4::translation(x, y, z);
}

inline Mat4 MatrixTranslate(float x, float y, float z) {
    return Mat4Translate(x, y, z);
}

inline Mat4 Mat4Scale(float x, float y, float z) {
    return Mat4::scale(x, y, z);
}

inline Mat4 MatrixScale(float x, float y, float z) {
    return Mat4Scale(x, y, z);
}

inline Mat4 Mat4RotateXYZ(const Vec3f& rotation) {
    return Mat4::rotationX(rotation.x) * Mat4::rotationY(rotation.y) * Mat4::rotationZ(rotation.z);
}

inline Mat4 MatrixRotateXYZ(const Vec3f& rotation) {
    return Mat4RotateXYZ(rotation);
}

inline Mat4 Mat4Multiply(const Mat4& left, const Mat4& right) {
    return left * right;
}

inline Mat4 MatrixMultiply(const Mat4& left, const Mat4& right) {
    return Mat4Multiply(left, right);
}

inline Mat4 Mat4Invert(const Mat4& matrix) {
    return matrix.inverted();
}

inline Mat4 MatrixInvert(const Mat4& matrix) {
    return Mat4Invert(matrix);
}

inline Mat4 Mat4Perspective(float fovy, float aspect, float nearPlane, float farPlane) {
    return Mat4::perspective(fovy, aspect, nearPlane, farPlane);
}

inline Mat4 Mat4PerspectiveVulkan(float fovy, float aspect, float nearPlane, float farPlane) {
    return Mat4::perspectiveVulkan(fovy, aspect, nearPlane, farPlane);
}

inline Mat4 MatrixPerspective(float fovy, float aspect, float nearPlane, float farPlane) {
    return Mat4Perspective(fovy, aspect, nearPlane, farPlane);
}

inline Mat4 Mat4Transpose(const Mat4& matrix) {
    Mat4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result.m[row * 4 + col] = matrix.m[col * 4 + row];
        }
    }
    return result;
}

inline Mat4 MatrixTranspose(const Mat4& matrix) {
    return Mat4Transpose(matrix);
}

inline float MatrixDeterminant(const Mat4& matrix) {
    const float m0 = matrix.m[0];
    const float m1 = matrix.m[1];
    const float m2 = matrix.m[2];
    const float m3 = matrix.m[3];
    const float m4 = matrix.m[4];
    const float m5 = matrix.m[5];
    const float m6 = matrix.m[6];
    const float m7 = matrix.m[7];
    const float m8 = matrix.m[8];
    const float m9 = matrix.m[9];
    const float m10 = matrix.m[10];
    const float m11 = matrix.m[11];
    const float m12 = matrix.m[12];
    const float m13 = matrix.m[13];
    const float m14 = matrix.m[14];
    const float m15 = matrix.m[15];

    return m0 * (m5 * (m10 * m15 - m11 * m14) -
                 m9 * (m6 * m15 - m7 * m14) +
                 m13 * (m6 * m11 - m7 * m10)) -
           m4 * (m1 * (m10 * m15 - m11 * m14) -
                 m9 * (m2 * m15 - m3 * m14) +
                 m13 * (m2 * m11 - m3 * m10)) +
           m8 * (m1 * (m6 * m15 - m7 * m14) -
                 m5 * (m2 * m15 - m3 * m14) +
                 m13 * (m2 * m7 - m3 * m6)) -
           m12 * (m1 * (m6 * m11 - m7 * m10) -
                  m5 * (m2 * m11 - m3 * m10) +
                  m9 * (m2 * m7 - m3 * m6));
}

inline float MatrixTrace(const Mat4& matrix) {
    return matrix.m[0] + matrix.m[5] + matrix.m[10] + matrix.m[15];
}

inline Mat4 MatrixMultiplyValue(const Mat4& matrix, float value) {
    return matrix * value;
}

inline Mat4 MatrixRotate(const Vec3f& axis, float angle) {
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;
    const float lengthSquared = x * x + y * y + z * z;

    if (lengthSquared != 1.0f && lengthSquared != 0.0f) {
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        x *= inverseLength;
        y *= inverseLength;
        z *= inverseLength;
    }

    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    const float oneMinusCosine = 1.0f - cosine;
    Mat4 result{};

    result.m[0] = x * x * oneMinusCosine + cosine;
    result.m[1] = y * x * oneMinusCosine + z * sine;
    result.m[2] = z * x * oneMinusCosine - y * sine;

    result.m[4] = x * y * oneMinusCosine - z * sine;
    result.m[5] = y * y * oneMinusCosine + cosine;
    result.m[6] = z * y * oneMinusCosine + x * sine;

    result.m[8] = x * z * oneMinusCosine + y * sine;
    result.m[9] = y * z * oneMinusCosine - x * sine;
    result.m[10] = z * z * oneMinusCosine + cosine;

    result.m[15] = 1.0f;
    return result;
}

inline Mat4 MatrixRotateZYX(const Vec3f& angle) {
    return Mat4::rotationZ(angle.z) *
           Mat4::rotationY(angle.y) *
           Mat4::rotationX(angle.x);
}

inline Mat4 MatrixFrustum(
    double left,
    double right,
    double bottom,
    double top,
    double nearPlane,
    double farPlane) {
    Mat4 result{};

    const float rightMinusLeft = static_cast<float>(right - left);
    const float topMinusBottom = static_cast<float>(top - bottom);
    const float farMinusNear = static_cast<float>(farPlane - nearPlane);

    result.m[0] = (static_cast<float>(nearPlane) * 2.0f) / rightMinusLeft;
    result.m[5] = (static_cast<float>(nearPlane) * 2.0f) / topMinusBottom;
    result.m[8] = (static_cast<float>(right) + static_cast<float>(left)) / rightMinusLeft;
    result.m[9] = (static_cast<float>(top) + static_cast<float>(bottom)) / topMinusBottom;
    result.m[10] = -(static_cast<float>(farPlane) + static_cast<float>(nearPlane)) / farMinusNear;
    result.m[11] = -1.0f;
    result.m[14] = -(static_cast<float>(farPlane) * static_cast<float>(nearPlane) * 2.0f) /
                   farMinusNear;
    result.m[15] = 0.0f;

    return result;
}

inline Float16 MatrixToFloatV(const Mat4& matrix) {
    Float16 result{};
    for (int index = 0; index < 16; ++index) {
        result.v[index] = matrix.m[index];
    }
    return result;
}

template <typename T>
inline Vec3T<T> Vec3Subtract(const Vec3T<T>& left, const Vec3T<T>& right) {
    return left - right;
}

template <typename T>
inline Vec3T<T> Vec3Normalize(const Vec3T<T>& value) {
    return value.normalized();
}

template <typename T>
inline Vec3T<T> Vec3Transform(const Vec3T<T>& value, const Mat4& transform) {
    return Vec3T<T>(
        static_cast<T>(transform.m[0] * value.x + transform.m[4] * value.y +
                       transform.m[8] * value.z + transform.m[12]),
        static_cast<T>(transform.m[1] * value.x + transform.m[5] * value.y +
                       transform.m[9] * value.z + transform.m[13]),
        static_cast<T>(transform.m[2] * value.x + transform.m[6] * value.y +
                       transform.m[10] * value.z + transform.m[14]));
}

template <typename T>
inline auto Vec3Distance(const Vec3T<T>& left, const Vec3T<T>& right) {
    return (left - right).length();
}

template <typename T>
inline T Vec3Dot(const Vec3T<T>& left, const Vec3T<T>& right) {
    return left.dot(right);
}

template <typename T>
inline T Vec3SquaredLength(const Vec3T<T>& value) {
    return value.lengthSquared();
}

template <typename T>
inline auto Vec3DistanceSqr(const Vec3T<T>& left, const Vec3T<T>& right) {
    return (right - left).lengthSquared();
}

template <typename T>
inline bool Vec3Equals(const Vec3T<T>& left, const Vec3T<T>& right) {
    const auto componentEquals = [](T a, T b) {
        const double leftValue = static_cast<double>(a);
        const double rightValue = static_cast<double>(b);
        const double tolerance = EPSILON * std::fmax(
            1.0,
            std::fmax(std::fabs(leftValue), std::fabs(rightValue)));
        return std::fabs(leftValue - rightValue) <= tolerance;
    };

    return componentEquals(left.x, right.x) &&
           componentEquals(left.y, right.y) &&
           componentEquals(left.z, right.z);
}

template <typename T>
inline Vec3T<T> Vec3NormalizeOrForward(const Vec3T<T>& value) {
    const float length = value.length();
    if (length < 1e-9f) {
        return Vec3T<T>{T{}, T{1}, T{}};
    }
    return value * static_cast<T>(1.0f / length);
}

template <typename T>
inline Vec3T<T> Vec3AddValue(const Vec3T<T>& value, T add) {
    return value.addValue(add);
}

template <typename T>
inline Vec3T<T> Vec3SubtractValue(const Vec3T<T>& value, T subtract) {
    return value.subtractValue(subtract);
}

template <typename T>
inline Vec3T<T> Vec3Perpendicular(const Vec3T<T>& value) {
    return value.perpendicular();
}

template <typename T>
inline auto Vec3Angle(const Vec3T<T>& left, const Vec3T<T>& right) {
    return left.angle(right);
}

template <typename T>
inline Vec3T<T> Vec3Project(const Vec3T<T>& value, const Vec3T<T>& onto) {
    return value.projectedOnto(onto);
}

template <typename T>
inline Vec3T<T> Vec3Reject(const Vec3T<T>& value, const Vec3T<T>& from) {
    return value.rejectedFrom(from);
}

template <typename T>
inline void Vec3OrthoNormalize(Vec3T<T>& v1, Vec3T<T>& v2) {
    v1.orthoNormalize(v2);
}

template <typename T>
inline Vec3T<T> Vec3RotateByQuaternion(const Vec3T<T>& value, const Quaternion& rotation) {
    return value.rotatedByQuaternion(rotation);
}

template <typename T>
inline Vec3T<T> Vec3RotateByAxisAngle(const Vec3T<T>& value, const Vec3T<T>& axis, float angle) {
    return value.rotatedByAxisAngle(axis, angle);
}

template <typename T>
inline Vec3T<T> Vec3MoveTowards(const Vec3T<T>& value, const Vec3T<T>& target, T maxDistance) {
    return value.moveTowards(target, maxDistance);
}

template <typename T>
inline Vec3T<T> Vec3CubicHermite(const Vec3T<T>& start, const Vec3T<T>& tangent1,
                                const Vec3T<T>& end, const Vec3T<T>& tangent2, float amount) {
    return start.cubicHermite(tangent1, end, tangent2, amount);
}

template <typename T>
inline Vec3T<T> Vec3Reflect(const Vec3T<T>& value, const Vec3T<T>& normal) {
    return value.reflected(normal);
}

template <typename T>
inline Vec3T<T> Vec3Min(const Vec3T<T>& left, const Vec3T<T>& right) {
    return left.componentMin(right);
}

template <typename T>
inline Vec3T<T> Vec3Max(const Vec3T<T>& left, const Vec3T<T>& right) {
    return left.componentMax(right);
}

template <typename T>
inline Vec3T<T> Vec3Barycenter(const Vec3T<T>& point, const Vec3T<T>& a,
                              const Vec3T<T>& b, const Vec3T<T>& c) {
    return point.barycenter(a, b, c);
}

template <typename T>
inline Vec3T<T> Vec3Unproject(const Vec3T<T>& source, const Mat4& projection, const Mat4& view) {
    return source.unprojected(projection, view);
}

template <typename T>
inline Float3 Vec3ToFloatV(const Vec3T<T>& value) {
    return value.toFloatV();
}

template <typename T>
inline Vec3T<T> Vec3Invert(const Vec3T<T>& value) {
    return value.inverted();
}

template <typename T>
inline Vec3T<T> Vec3Clamp(const Vec3T<T>& value, const Vec3T<T>& min, const Vec3T<T>& max) {
    return value.clamped(min, max);
}

template <typename T>
inline Vec3T<T> Vec3ClampValue(const Vec3T<T>& value, T min, T max) {
    return value.clampedValue(min, max);
}

template <typename T>
inline Vec3T<T> Vec3Refract(const Vec3T<T>& value, const Vec3T<T>& normal, T ratio) {
    return value.refracted(normal, ratio);
}

template <typename T>
inline Vec4T<T> Vec4Zero() {
    return Vec4T<T>{T{}, T{}, T{}, T{}};
}

inline Vec4f Vec4Zero() {
    return Vec4f{};
}

template <typename T>
inline Vec4T<T> Vec4One() {
    return Vec4T<T>{T{1}, T{1}, T{1}, T{1}};
}

inline Vec4f Vec4One() {
    return Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
}

template <typename T>
inline Vec4T<T> Vec4AddValue(const Vec4T<T>& value, T add) {
    return Vec4T<T>{
        value.x + add,
        value.y + add,
        value.z + add,
        value.w + add
    };
}

template <typename T>
inline Vec4T<T> Vec4SubtractValue(const Vec4T<T>& value, T subtract) {
    return Vec4T<T>{
        value.x - subtract,
        value.y - subtract,
        value.z - subtract,
        value.w - subtract
    };
}

template <typename T>
inline T Vec4LengthSqr(const Vec4T<T>& value) {
    return value.x * value.x +
           value.y * value.y +
           value.z * value.z +
           value.w * value.w;
}

template <typename T>
inline auto Vec4DistanceSqr(const Vec4T<T>& left, const Vec4T<T>& right) {
    const auto dx = right.x - left.x;
    const auto dy = right.y - left.y;
    const auto dz = right.z - left.z;
    const auto dw = right.w - left.w;
    return dx * dx + dy * dy + dz * dz + dw * dw;
}

template <typename T>
inline auto Vec4Distance(const Vec4T<T>& left, const Vec4T<T>& right) {
    return std::sqrt(Vec4DistanceSqr(left, right));
}

template <typename T>
inline Vec4T<T> Vec4Min(const Vec4T<T>& left, const Vec4T<T>& right) {
    return Vec4T<T>{
        std::fmin(left.x, right.x),
        std::fmin(left.y, right.y),
        std::fmin(left.z, right.z),
        std::fmin(left.w, right.w)
    };
}

template <typename T>
inline Vec4T<T> Vec4Max(const Vec4T<T>& left, const Vec4T<T>& right) {
    return Vec4T<T>{
        std::fmax(left.x, right.x),
        std::fmax(left.y, right.y),
        std::fmax(left.z, right.z),
        std::fmax(left.w, right.w)
    };
}

template <typename T>
inline Vec4T<T> Vec4MoveTowards(
    const Vec4T<T>& value,
    const Vec4T<T>& target,
    T maxDistance) {
    const Vec4T<T> delta = target - value;
    const auto distanceSquared = Vec4LengthSqr(delta);

    if (distanceSquared == 0 ||
        (maxDistance >= 0 && distanceSquared <= maxDistance * maxDistance)) {
        return target;
    }

    const auto distance = std::sqrt(distanceSquared);
    return value + delta * static_cast<T>(maxDistance / distance);
}

template <typename T>
inline Vec4T<T> Vec4Invert(const Vec4T<T>& value) {
    return Vec4T<T>{
        T{1} / value.x,
        T{1} / value.y,
        T{1} / value.z,
        T{1} / value.w
    };
}

template <typename T>
inline bool Vec4Equals(const Vec4T<T>& left, const Vec4T<T>& right) {
    const auto componentEquals = [](T a, T b) {
        const double leftValue = static_cast<double>(a);
        const double rightValue = static_cast<double>(b);
        const double tolerance = EPSILON * std::fmax(
            1.0,
            std::fmax(std::fabs(leftValue), std::fabs(rightValue)));
        return std::fabs(leftValue - rightValue) <= tolerance;
    };

    return componentEquals(left.x, right.x) &&
           componentEquals(left.y, right.y) &&
           componentEquals(left.z, right.z) &&
           componentEquals(left.w, right.w);
}

inline Vec3f Mat4Column(const Mat4& matrix, int column) {
    return Vec3f{matrix.m[column * 4], matrix.m[column * 4 + 1], matrix.m[column * 4 + 2]};
}

inline Mat4 Mat4PolarRotation(const Mat4& matrix) {
    Mat4 result = matrix;
    for (int iteration = 0; iteration < 24; ++iteration) {
        const Mat4 invertedTranspose = Mat4Transpose(result.inverted());
        for (int i = 0; i < 16; ++i) {
            result.m[i] = 0.5f * (result.m[i] + invertedTranspose.m[i]);
        }
    }
    for (int column = 0; column < 3; ++column) {
        const Vec3f axis = Vec3NormalizeOrForward(Mat4Column(result, column));
        result.m[column * 4] = axis.x;
        result.m[column * 4 + 1] = axis.y;
        result.m[column * 4 + 2] = axis.z;
    }
    return result;
}

}; // namespace qc

#endif // __QUARK_MATH__
