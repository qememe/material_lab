#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lab {
struct Vec2 {
    float x{}, y{};
    constexpr Vec2 operator+(Vec2 b) const { return {x+b.x,y+b.y}; }
    constexpr Vec2 operator-(Vec2 b) const { return {x-b.x,y-b.y}; }
    constexpr Vec2 operator-() const { return {-x,-y}; }
    constexpr Vec2 operator*(float s) const { return {x*s,y*s}; }
    constexpr Vec2 operator/(float s) const { return {x/s,y/s}; }
    Vec2& operator+=(Vec2 b) { x+=b.x; y+=b.y; return *this; }
    Vec2& operator-=(Vec2 b) { x-=b.x; y-=b.y; return *this; }
    Vec2& operator*=(float s) { x*=s; y*=s; return *this; }
};
inline float dot(Vec2 a,Vec2 b) { return a.x*b.x+a.y*b.y; }
inline float cross(Vec2 a,Vec2 b) { return a.x*b.y-a.y*b.x; }
inline float length(Vec2 a) { return std::sqrt(dot(a,a)); }
inline Vec2 normalized(Vec2 a) { float n=length(a); return n>1e-6f?a/n:Vec2{1,0}; }
inline bool finite(Vec2 a) { return std::isfinite(a.x)&&std::isfinite(a.y); }
inline std::uint64_t gridKey(int x,int y) {
    return (std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y);
}
}
