// Small 3D maths kit. Matrices are column-major (same memory layout as the PSP's
// ScePspFMatrix4: four columns X, Y, Z, W), transforming column vectors: p' = M * p.
#pragma once
#include <cmath>
#include <cstring>

struct V3 {
    float x, y, z;
    V3() : x(0), y(0), z(0) {}
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return V3(x + o.x, y + o.y, z + o.z); }
    V3 operator-(const V3& o) const { return V3(x - o.x, y - o.y, z - o.z); }
    V3 operator*(float s) const { return V3(x * s, y * s, z * s); }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float len(const V3& a) { return sqrtf(dot(a, a)); }
inline V3 norm(const V3& a) { float l = len(a); return l > 1e-12f ? a * (1.0f / l) : V3(0, 0, 1); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float wrapPi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

struct M4 {
    float m[16];
    static M4 identity() { M4 r; memset(r.m, 0, sizeof r.m); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
    M4 operator*(const M4& b) const {
        M4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    V3 point(const V3& p) const { return V3(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]); }
    V3 dir(const V3& p) const { return V3(m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z, m[2] * p.x + m[6] * p.y + m[10] * p.z); }
    static M4 translate(const V3& t) { M4 r = identity(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static M4 scale(float x, float y, float z) { M4 r = identity(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
    static M4 rotZ(float a) { M4 r = identity(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
    static M4 rotY(float a) { M4 r = identity(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
    static M4 rotX(float a) { M4 r = identity(); float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
    // basis from forward (x), left (y), up (z) axes + position
    static M4 basis(const V3& fx, const V3& ly, const V3& uz, const V3& p) {
        M4 r = identity();
        r.m[0] = fx.x; r.m[1] = fx.y; r.m[2] = fx.z;
        r.m[4] = ly.x; r.m[5] = ly.y; r.m[6] = ly.z;
        r.m[8] = uz.x; r.m[9] = uz.y; r.m[10] = uz.z;
        r.m[12] = p.x; r.m[13] = p.y; r.m[14] = p.z;
        return r;
    }
    static M4 perspective(float fovyDeg, float aspect, float n, float f) {
        M4 r; memset(r.m, 0, sizeof r.m);
        float t = 1.0f / tanf(fovyDeg * 3.14159265f / 360.0f);
        r.m[0] = t / aspect; r.m[5] = t;
        r.m[10] = (f + n) / (n - f); r.m[11] = -1;
        r.m[14] = 2 * f * n / (n - f);
        return r;
    }
    // camera at eye looking along fwd with world up (+Z): view space looks down -Z (GL style)
    static M4 lookDir(const V3& eye, const V3& fwd, const V3& upHint) {
        V3 f = norm(fwd), s = norm(cross(f, upHint)), u = cross(s, f);
        M4 r = identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
};
