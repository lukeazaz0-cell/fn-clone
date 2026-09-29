// Shaders, procedural texture atlas, sun shadow map, sky and water.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "raylib.h"
#include "raymath.h"
#include "render.h"
#include "rlgl.h"

namespace client {

unsigned int gAtlasTex = 0; // used by the immediate-mode primitives
static int gDrawMat = 0;
void setDrawMaterial(int m) { gDrawMat = m; }
int drawMaterial() { return gDrawMat; }

namespace {

// ----------------------------------------------------------------------------------- GLSL

const char* kObjVS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    fragTexCoord = vertexTexCoord;
    fragTexCoord2 = vertexTexCoord2;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Shared lighting code (sun + sky ambient + specular + PCF shadows + fog + filmic tonemap).
const char* kCommonFS = R"(
uniform vec3 viewPos;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 skyColor;
uniform vec3 groundColor;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float exposure;
uniform float time;
uniform mat4 lightVP;
uniform sampler2D shadowMap;
uniform int shadowsOn;
uniform float shadowTexel;
uniform sampler2D texture0;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toGamma(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

float shadowFactor(vec3 wp, vec3 n) {
    if (shadowsOn == 0) return 1.0;
    vec4 ls = lightVP * vec4(wp + n * 0.06, 1.0);
    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
    float bias = 0.0006;
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++) {
            float d = texture(shadowMap, p.xy + vec2(x, y) * shadowTexel * 1.2).r;
            s += (p.z - bias > d) ? 0.0 : 1.0;
        }
    s /= 9.0;
    vec2 e = min(p.xy, 1.0 - p.xy);
    float fade = clamp(min(e.x, e.y) * 10.0, 0.0, 1.0);
    return mix(1.0, s, fade);
}

// Material table: world-space texture scale, specular strength, glossiness.
float matScale(int m) {
    if (m == 1) return 0.55; if (m == 2) return 0.22; if (m == 3) return 0.45; if (m == 4) return 0.3;
    if (m == 5) return 0.5; if (m == 6) return 0.5; if (m == 7) return 0.35; if (m == 8) return 0.35;
    if (m == 9) return 0.6; if (m == 10) return 0.25; if (m == 11) return 2.5; if (m == 12) return 1.0;
    if (m == 13) return 0.7; if (m == 14) return 0.35; if (m == 15) return 0.5; return 0.5;
}
float matSpec(int m) { if (m == 7) return 0.55; if (m == 10) return 1.0; if (m == 4) return 0.18; if (m == 14) return 0.08; return 0.04; }
float matGloss(int m) { if (m == 7) return 40.0; if (m == 10) return 160.0; if (m == 4) return 18.0; return 10.0; }

float sampleTile(int m, vec2 uv) {
    vec2 tile = vec2(float(m % 4), float(m / 4));
    vec2 f = fract(uv);
    vec2 tuv = (tile + 0.03125 + f * 0.9375) / 4.0;
    vec2 dx = dFdx(uv) * (0.9375 / 4.0), dy = dFdy(uv) * (0.9375 / 4.0);
    return textureGrad(texture0, tuv, dx, dy).r;
}

float detail(int m, vec3 wp, vec3 n) {
    if (m <= 0) return 1.0;
    float sc = matScale(m);
    vec3 bw = pow(abs(n), vec3(4.0));
    bw /= (bw.x + bw.y + bw.z);
    float t = 0.0;
    if (bw.x > 0.01) t += sampleTile(m, wp.zy * sc) * bw.x;
    if (bw.y > 0.01) t += sampleTile(m, wp.xz * sc) * bw.y;
    if (bw.z > 0.01) t += sampleTile(m, wp.xy * sc) * bw.z;
    return 0.42 + 1.16 * t;
}

vec3 shade(vec3 albedoGamma, vec3 n, vec3 wp, float spec, float gloss, float ao) {
    vec3 albedo = toLinear(albedoGamma);
    vec3 L = -sunDir;
    vec3 V = normalize(viewPos - wp);
    float ndl = max(dot(n, L), 0.0);
    float sh = ndl > 0.0 ? shadowFactor(wp, n) : 0.0;
    vec3 hemi = mix(groundColor, skyColor, 0.5 + 0.5 * n.y);
    vec3 H = normalize(L + V);
    float sp = pow(max(dot(n, H), 0.0), gloss) * spec * (gloss + 8.0) / 40.0;
    float fres = pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 c = albedo * (hemi * ao + sunColor * ndl * sh);
    c += sunColor * sp * sh;
    c += skyColor * fres * spec * 0.6;
    return c;
}

vec3 finish(vec3 c, vec3 wp) {
    float dist = length(viewPos - wp);
    float h = max(wp.y, 0.0);
    float f = 1.0 - exp(-dist * fogDensity * exp(-h * 0.01));
    vec3 V = normalize(wp - viewPos);
    float sunAmt = pow(max(dot(V, -sunDir), 0.0), 6.0);
    vec3 fc = mix(toLinear(fogColor), sunColor * 0.45, sunAmt * 0.5);
    c = mix(c, fc, clamp(f, 0.0, 1.0));
    c *= exposure;
    c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = max(mix(vec3(luma), c, 1.04), 0.0);
    return toGamma(clamp(c, 0.0, 1.0));
}
)";

const char* kObjFS = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    if (!gl_FrontFacing) n = -n;
    int m = int(fragTexCoord.x + 0.5);
    float ao = fragTexCoord.y > 0.0 ? fragTexCoord.y : 1.0;
    vec3 albedo = fragColor.rgb * colDiffuse.rgb * detail(m, fragPos, n);
    vec3 c = shade(albedo, n, fragPos, matSpec(m), matGloss(m), ao);
    finalColor = vec4(finish(c, fragPos), fragColor.a * colDiffuse.a);
}
)";

// Terrain: vertex color carries the biome tint and baked AO; texcoord/texcoord2 carry
// blend weights for grass/sand and snow/road textures; steep slopes blend in rock.
const char* kTerrainFS = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    float wGrass = fragTexCoord.x, wSand = fragTexCoord.y, wSnow = fragTexCoord2.x, wRoad = fragTexCoord2.y;
    float wRock = smoothstep(0.82, 0.62, n.y) * 1.5;
    float sum = wGrass + wSand + wSnow + wRoad + wRock + 0.0001;
    float d = 0.0;
    if (wGrass > 0.01) d += sampleTile(1, fragPos.xz * matScale(1)) * wGrass;
    if (wSand > 0.01) d += sampleTile(3, fragPos.xz * matScale(3)) * wSand;
    if (wSnow > 0.01) d += sampleTile(4, fragPos.xz * matScale(4)) * wSnow;
    if (wRoad > 0.01) d += sampleTile(14, fragPos.xz * matScale(14)) * wRoad;
    if (wRock > 0.01) {
        vec3 bw = pow(abs(n), vec3(4.0)); bw /= (bw.x + bw.y + bw.z);
        float r = sampleTile(2, fragPos.zy * 0.22) * bw.x + sampleTile(2, fragPos.xz * 0.22) * bw.y + sampleTile(2, fragPos.xy * 0.22) * bw.z;
        d += r * wRock;
    }
    d /= sum;
    vec3 albedo = fragColor.rgb * (0.42 + 1.16 * d);
    // Large-scale variation so the ground doesn't look tiled from afar
    float macro = sampleTile(8, fragPos.xz * 0.013);
    albedo *= 0.85 + 0.3 * macro;
    albedo = mix(albedo, vec3(0.47, 0.45, 0.42), clamp(wRock / sum, 0.0, 1.0) * 0.6);
    float spec = 0.03 + wSnow / sum * 0.15;
    vec3 c = shade(albedo, n, fragPos, spec, 14.0, fragColor.a);
    finalColor = vec4(finish(c, fragPos), 1.0);
}
)";

const char* kWaterFS = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
uniform vec4 colDiffuse;
uniform float worldSize;
out vec4 finalColor;
float wave(vec2 p, vec2 d, float f, float s) { return sin(dot(p, d) * f + time * s); }
void main() {
    vec2 p = fragPos.xz;
    // Analytic wave normal from a sum of directional sines
    vec2 grad = vec2(0.0);
    vec2 dirs[4] = vec2[](normalize(vec2(1.0, 0.3)), normalize(vec2(-0.4, 1.0)), normalize(vec2(0.7, -0.8)), normalize(vec2(-1.0, -0.2)));
    float fr[4] = float[](0.35, 0.55, 0.9, 1.6);
    float sp[4] = float[](1.1, 1.4, 1.9, 2.6);
    float am[4] = float[](0.10, 0.07, 0.04, 0.025);
    float distFade = clamp(1.0 - length(viewPos - fragPos) / 260.0, 0.0, 1.0);
    for (int i = 0; i < 4; i++) grad += dirs[i] * fr[i] * am[i] * cos(dot(p, dirs[i]) * fr[i] + time * sp[i]) * mix(0.15, 1.0, distFade * (i < 2 ? 1.0 : distFade));
    vec3 n = normalize(vec3(-grad.x, 1.0, -grad.y));
    // Water depth from the terrain heightmap
    vec2 uv = p / worldSize;
    float ground = -16.0;
    if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) ground = texture(texture0, uv).r;
    float depth = max(0.0, -ground);
    vec3 V = normalize(viewPos - fragPos);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 shallow = toLinear(vec3(0.22, 0.66, 0.68));
    vec3 deep = toLinear(vec3(0.03, 0.2, 0.36));
    vec3 body = mix(shallow, deep, clamp(depth / 9.0, 0.0, 1.0));
    vec3 L = -sunDir;
    body *= (0.35 + 0.65 * max(dot(n, L), 0.0)) * sunColor * 0.6 + skyColor * 0.35;
    vec3 R = reflect(-V, n);
    vec3 skyRefl = mix(toLinear(fogColor), skyColor * 1.3, clamp(R.y * 2.0, 0.0, 1.0));
    vec3 c = mix(body, skyRefl, fres);
    vec3 H = normalize(L + V);
    c += sunColor * pow(max(dot(n, H), 0.0), 220.0) * 3.0 * shadowFactor(fragPos, vec3(0, 1, 0));
    // Shore foam
    float foamBand = smoothstep(0.9, 0.0, depth) * (0.55 + 0.45 * sin(depth * 12.0 - time * 2.2 + p.x * 0.05));
    c = mix(c, toLinear(vec3(0.95)), clamp(foamBand, 0.0, 1.0) * 0.7);
    float alpha = clamp(0.55 + depth * 0.18 + fres * 0.4, 0.55, 0.97);
    finalColor = vec4(finish(c, fragPos), alpha);
}
)";

const char* kSkyVS = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";

const char* kSkyFS = R"(#version 330
uniform mat4 invVP;
uniform vec2 resolution;
uniform vec3 sunDir;
uniform vec3 fogColor;
uniform float time;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a * noise(p); p *= 2.03; a *= 0.5; } return s; }
void main() {
    vec2 ndc = gl_FragCoord.xy / resolution * 2.0 - 1.0;
    vec4 a = invVP * vec4(ndc, -1.0, 1.0);
    vec4 b = invVP * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 zenith = vec3(0.24, 0.47, 0.86);
    vec3 horizon = fogColor;
    float t = clamp(dir.y, 0.0, 1.0);
    vec3 c = mix(horizon, zenith, pow(t, 0.55));
    if (dir.y < 0.0) c = mix(horizon, horizon * 0.8, clamp(-dir.y * 3.0, 0.0, 1.0));
    vec3 L = -sunDir;
    float sd = max(dot(dir, L), 0.0);
    c += vec3(1.0, 0.85, 0.6) * pow(sd, 8.0) * 0.35;
    c += vec3(1.0, 0.95, 0.85) * pow(sd, 900.0) * 3.0;
    // Clouds on a plane high above
    if (dir.y > 0.02) {
        vec2 cp = dir.xz / dir.y * 0.9 + vec2(time * 0.01, time * 0.004);
        float n = fbm(cp * 1.3);
        float cov = smoothstep(0.5, 0.78, n);
        float shadeC = 0.82 + 0.18 * smoothstep(0.5, 0.9, fbm(cp * 1.3 + L.xz * 0.05));
        vec3 cc = mix(vec3(0.78, 0.8, 0.86), vec3(1.0), shadeC) + vec3(1.0, 0.9, 0.7) * pow(sd, 6.0) * 0.3;
        c = mix(c, cc, cov * smoothstep(0.02, 0.2, dir.y) * 0.9);
    }
    finalColor = vec4(c, 1.0);
}
)";

const char* kDepthVS = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char* kDepthFS = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)";

// ----------------------------------------------------------------------------------- textures

const int TILE = 256;

float h2(int x, int y, uint32_t seed) { return hashNoise(x, y, seed); }

// Tileable value noise with period `per` (in noise cells).
float tnoise(float x, float y, int per, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    auto H = [&](int a, int b) { return h2(((a % per) + per) % per, ((b % per) + per) % per, seed); };
    return lerpf(lerpf(H(xi, yi), H(xi + 1, yi), u), lerpf(H(xi, yi + 1), H(xi + 1, yi + 1), u), v);
}
float tfbm(float x, float y, int per, uint32_t seed, int oct = 5) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; i++) {
        s += a * tnoise(x, y, per, seed + i * 17);
        n += a;
        a *= 0.5f;
        x *= 2; y *= 2; per *= 2;
    }
    return s / n;
}

using TileFn = float (*)(int x, int y);

float texGrass(int x, int y) {
    float u = x / (float)TILE, v = y / (float)TILE;
    float n = tfbm(u * 8, v * 8, 8, 11);
    float blades = tnoise(u * 48, v * 48, 48, 12);
    float clumps = tnoise(u * 16, v * 16, 16, 13);
    return clampf(0.28f + 0.3f * n + 0.22f * blades + 0.15f * clumps, 0, 1);
}
float texRock(int x, int y) {
    float u = x / (float)TILE, v = y / (float)TILE;
    float n = tfbm(u * 4, v * 4, 4, 21);
    float r = 1.0f - std::fabs(tfbm(u * 6, v * 6, 6, 22) * 2 - 1);
    float cracks = smoothstep(0.93f, 0.99f, r);
    return clampf(0.25f + 0.55f * n - 0.35f * cracks + 0.1f * tnoise(u * 64, v * 64, 64, 23), 0, 1);
}
float texSand(int x, int y) {
    float u = x / (float)TILE, v = y / (float)TILE;
    float rip = 0.5f + 0.5f * std::sin((v * 12 + tfbm(u * 4, v * 4, 4, 31) * 3) * 2 * kPi);
    return clampf(0.35f + 0.15f * rip + 0.3f * tnoise(u * 128, v * 128, 128, 32), 0, 1);
}
float texSnow(int x, int y) {
    float u = x / (float)TILE, v = y / (float)TILE;
    float n = tfbm(u * 6, v * 6, 6, 41);
    float sparkle = h2(x, y, 42) > 0.985f ? 0.25f : 0.0f;
    return clampf(0.45f + 0.2f * n + sparkle, 0, 1);
}
float texWood(int x, int y) {
    int plank = y / 64;
    float u = x / (float)TILE;
    float tone = h2(plank, 0, 51) * 0.25f;
    float grain = 0.5f + 0.5f * std::sin((u * 18 + tfbm(u * 4, y / (float)TILE * 16, 4, 52) * 4 + plank * 3.1f) * 2 * kPi);
    float gap = (y % 64 < 3) ? 0.0f : 1.0f;
    float endGap = ((x + plank * 97) % 256 < 3) ? 0.2f : 1.0f;
    float nail = 0;
    int nx = (x + plank * 97) % 128, ny = y % 64;
    if ((nx - 8) * (nx - 8) + (ny - 32) * (ny - 32) < 9) nail = -0.3f;
    return clampf((0.35f + tone + 0.2f * grain) * gap * endGap + nail, 0, 1);
}
float texBrick(int x, int y) {
    int row = y / 32;
    int xo = x + (row % 2) * 32;
    int col = xo / 64;
    bool mortar = (y % 32) < 4 || (xo % 64) < 4;
    if (mortar) return 0.8f + 0.1f * h2(x, y, 61);
    float tone = h2(col, row, 62) * 0.3f;
    return clampf(0.3f + tone + 0.15f * tnoise(x / 8.0f, y / 8.0f, 32, 63), 0, 1);
}
float texMetal(int x, int y) {
    bool seam = (x % 128) < 3 || (y % 128) < 3;
    int px = x % 128, py = y % 128;
    bool rivet = false;
    for (int cx : {10, 118})
        for (int cy : {10, 118})
            if ((px - cx) * (px - cx) + (py - cy) * (py - cy) < 12) rivet = true;
    float brushed = 0.5f + 0.5f * tnoise(x / 1.5f, y / 40.0f, 170, 71);
    float v = 0.45f + 0.12f * brushed + 0.08f * h2(x / 128, y / 128, 72);
    if (seam) v = 0.18f;
    if (rivet) v = 0.75f;
    return v;
}
float texPlaster(int x, int y) {
    float u = x / (float)TILE, v = y / (float)TILE;
    return clampf(0.45f + 0.18f * tfbm(u * 8, v * 8, 8, 81) + 0.06f * tnoise(u * 128, v * 128, 128, 82), 0, 1);
}
float texRoof(int x, int y) {
    int row = y / 32;
    int xo = x + (row % 2) * 16;
    float inRow = (y % 32) / 32.0f;
    float edge = (xo % 32) < 2 ? 0.25f : 0.0f;
    float tone = h2(xo / 32, row, 91) * 0.2f;
    return clampf(0.7f - 0.45f * inRow + tone - edge, 0, 1);
}
float texGlass(int x, int y) {
    float u = x / (float)TILE, v = y / (float)TILE;
    float streak = 0.5f + 0.5f * std::sin((u + v * 0.6f) * 6 * kPi);
    return clampf(0.4f + 0.2f * streak + 0.08f * tnoise(u * 8, v * 64, 8, 101), 0, 1);
}
float texFabric(int x, int y) {
    float a = std::sin(x * 0.8f) * std::sin(y * 0.8f);
    return clampf(0.5f + 0.2f * a + 0.15f * tnoise(x / 16.0f, y / 16.0f, 16, 111), 0, 1);
}
float texBark(int x, int y) {
    float u = x / (float)TILE, v = y / (float)TILE;
    float f = tnoise(u * 32, v * 4, 32, 121);
    float fur = smoothstep(0.35f, 0.15f, f);
    return clampf(0.45f + 0.3f * tfbm(u * 16, v * 3, 16, 122) - 0.35f * fur, 0, 1);
}
float texLeaves(int x, int y) {
    // Cellular leaf clumps
    float u = x / (float)TILE * 10, v = y / (float)TILE * 10;
    int cx = (int)std::floor(u), cy = (int)std::floor(v);
    float best = 9, second = 9;
    int bi = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int gx = cx + dx, gy = cy + dy;
            int wx = ((gx % 10) + 10) % 10, wy = ((gy % 10) + 10) % 10;
            float px = gx + h2(wx, wy, 131), py = gy + h2(wx, wy, 132);
            float d = (px - u) * (px - u) + (py - v) * (py - v);
            if (d < best) { second = best; best = d; bi = wx * 31 + wy; }
            else if (d < second) second = d;
        }
    float edge = smoothstep(0.0f, 0.12f, std::sqrt(second) - std::sqrt(best));
    return clampf(0.25f + 0.4f * h2(bi, 1, 133) + 0.25f * edge, 0, 1);
}
float texAsphalt(int x, int y) {
    float s = h2(x, y, 141);
    return clampf(0.42f + 0.18f * tfbm(x / 32.0f, y / 32.0f, 8, 142) + (s > 0.92f ? 0.25f : s < 0.06f ? -0.2f : 0.0f), 0, 1);
}
float texTile(int x, int y) {
    bool grout = (x % 64) < 3 || (y % 64) < 3;
    if (grout) return 0.25f;
    return clampf(0.55f + 0.15f * h2(x / 64, y / 64, 151) + 0.05f * tnoise(x / 8.0f, y / 8.0f, 32, 152), 0, 1);
}
float texPlain(int, int) { return 0.5f; }

Texture2D buildAtlas() {
    const int N = TILE * 4;
    Image img = GenImageColor(N, N, GRAY);
    Color* px = (Color*)img.data;
    TileFn fns[16] = {texPlain, texGrass, texRock, texSand, texSnow, texWood, texBrick, texMetal,
                      texPlaster, texRoof, texGlass, texFabric, texBark, texLeaves, texAsphalt, texTile};
    for (int t = 0; t < 16; t++) {
        int ox = (t % 4) * TILE, oy = (t / 4) * TILE;
        for (int y = 0; y < TILE; y++)
            for (int x = 0; x < TILE; x++) {
                unsigned char v = (unsigned char)(clampf(fns[t](x, y), 0, 1) * 255);
                px[(oy + y) * N + ox + x] = {v, v, v, 255};
            }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    rlTextureParameters(tex.id, RL_TEXTURE_FILTER_ANISOTROPIC, 8);
    SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
    return tex;
}

Shader loadLit(const char* fs) {
    std::string full = std::string("#version 330\n") + kCommonFS + fs;
    Shader s = LoadShaderFromMemory(kObjVS, full.c_str());
    s.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(s, "matModel");
    s.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(s, "matNormal");
    s.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(s, "viewPos");
    s.locs[SHADER_LOC_VERTEX_TEXCOORD02] = GetShaderLocationAttrib(s, "vertexTexCoord2");
    return s;
}

void setVec3(Shader s, const char* name, Vector3 v) {
    int loc = GetShaderLocation(s, name);
    if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC3);
}
void setFloat(Shader s, const char* name, float v) {
    int loc = GetShaderLocation(s, name);
    if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT);
}
void setInt(Shader s, const char* name, int v) {
    int loc = GetShaderLocation(s, name);
    if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT);
}
Vector3 colorVec(Color c, float scale = 1.0f) { return {c.r / 255.0f * scale, c.g / 255.0f * scale, c.b / 255.0f * scale}; }

const int SHADOW_SLOT = 6;

} // namespace

void Lighting::load() {
    shader = loadLit(kObjFS);
    terrain = loadLit(kTerrainFS);
    water = loadLit(kWaterFS);
    sky = LoadShaderFromMemory(kSkyVS, kSkyFS);
    depth = LoadShaderFromMemory(kDepthVS, kDepthFS);
    atlas = buildAtlas();
    gAtlasTex = atlas.id;
    sunDir = Vector3Normalize({-0.52f, -0.62f, -0.58f});
    // Shadow map framebuffer with a depth texture attachment
    shadowFbo = rlLoadFramebuffer();
    if (shadowFbo) {
        rlEnableFramebuffer(shadowFbo);
        shadowDepth.id = rlLoadTextureDepth(shadowSize, shadowSize, false);
        shadowDepth.width = shadowDepth.height = shadowSize;
        shadowDepth.format = 19;
        shadowDepth.mipmaps = 1;
        rlFramebufferAttach(shadowFbo, shadowDepth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        if (!rlFramebufferComplete(shadowFbo)) shadows = false;
        rlDisableFramebuffer();
        rlTextureParameters(shadowDepth.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(shadowDepth.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(shadowDepth.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
        rlTextureParameters(shadowDepth.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    } else {
        shadows = false;
    }
    for (Shader* s : {&shader, &terrain, &water}) {
        setInt(*s, "shadowMap", SHADOW_SLOT);
        setFloat(*s, "shadowTexel", 1.0f / shadowSize);
        setFloat(*s, "worldSize", WORLD_SIZE);
    }
}

void Lighting::unload() {
    for (Shader* s : {&shader, &terrain, &water, &sky, &depth})
        if (s->id) UnloadShader(*s);
    if (atlas.id) UnloadTexture(atlas);
    if (heightTex.id) UnloadTexture(heightTex);
    if (shadowFbo) {
        rlUnloadTexture(shadowDepth.id);
        rlUnloadFramebuffer(shadowFbo);
    }
    *this = Lighting();
}

void Lighting::setHeightmap(const GameMap& map) {
    if (heightTex.id) UnloadTexture(heightTex);
    const int N = HM_N + 1;
    std::vector<float> data((size_t)N * N);
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) data[(size_t)z * N + x] = map.vertexHeight(x, z);
    Image img{};
    img.data = data.data();
    img.width = img.height = N;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R32;
    heightTex = LoadTextureFromImage(img);
    SetTextureFilter(heightTex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(heightTex, TEXTURE_WRAP_CLAMP);
}

void Lighting::begin(const Camera3D& cam, float time) {
    Vector3 sunC = {2.6f, 2.35f, 2.0f};
    Vector3 skyC = {0.34f, 0.46f, 0.66f};
    Vector3 groundC = {0.2f, 0.18f, 0.14f};
    Vector3 fogC = colorVec(fogColor);
    Matrix id = MatrixIdentity();
    for (Shader* s : {&shader, &terrain, &water}) {
        setVec3(*s, "viewPos", cam.position);
        setVec3(*s, "sunDir", sunDir);
        setVec3(*s, "sunColor", sunC);
        setVec3(*s, "skyColor", skyC);
        setVec3(*s, "groundColor", groundC);
        setVec3(*s, "fogColor", fogC);
        setFloat(*s, "fogDensity", fogDensity);
        setFloat(*s, "exposure", exposure);
        setFloat(*s, "time", time);
        setInt(*s, "shadowsOn", shadows && !inShadowPass ? 1 : 0);
        SetShaderValueMatrix(*s, GetShaderLocation(*s, "lightVP"), lightVP);
        SetShaderValueMatrix(*s, s->locs[SHADER_LOC_MATRIX_MODEL], id);
        SetShaderValueMatrix(*s, s->locs[SHADER_LOC_MATRIX_NORMAL], id);
    }
    rlActiveTextureSlot(SHADOW_SLOT);
    rlEnableTexture(shadows ? shadowDepth.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);
}

void Lighting::beginShadowPass(const Vector3& focusIn) {
    if (!shadows || !shadowFbo) return;
    inShadowPass = true;
    // Unbind the shadow texture so we don't sample what we render into
    rlActiveTextureSlot(SHADOW_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    // Snap the focus to shadow texels along the light's axes to avoid shimmering
    Vector3 fwd = sunDir;
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, {0, 1, 0}));
    Vector3 up = Vector3CrossProduct(right, fwd);
    float texel = shadowRadius * 2.0f / shadowSize;
    float r = std::round(Vector3DotProduct(focusIn, right) / texel) * texel;
    float u = std::round(Vector3DotProduct(focusIn, up) / texel) * texel;
    float f = Vector3DotProduct(focusIn, fwd);
    Vector3 focus = Vector3Add(Vector3Add(Vector3Scale(right, r), Vector3Scale(up, u)), Vector3Scale(fwd, f));

    Camera3D lc{};
    lc.position = Vector3Subtract(focus, Vector3Scale(sunDir, 350.0f));
    lc.target = focus;
    lc.up = {0, 1, 0};
    lc.fovy = shadowRadius * 2.0f;
    lc.projection = CAMERA_ORTHOGRAPHIC;

    rlDrawRenderBatchActive();
    rlEnableFramebuffer(shadowFbo);
    rlViewport(0, 0, shadowSize, shadowSize);
    rlClearColor(255, 255, 255, 255);
    rlClearScreenBuffers();
    rlSetClipPlanes(1.0, 800.0);
    BeginMode3D(lc);
    lightVP = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    rlDisableColorBlend();
}

void Lighting::endShadowPass() {
    if (!inShadowPass) return;
    EndMode3D();
    rlEnableColorBlend();
    rlDrawRenderBatchActive();
    rlDisableFramebuffer();
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
    inShadowPass = false;
}

void Lighting::drawSky(const Camera3D& cam, float time) {
    float w = (float)GetRenderWidth(), h = (float)GetRenderHeight();
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, w / h, 0.1, 3000.0);
    Matrix inv = MatrixInvert(MatrixMultiply(view, proj));
    SetShaderValueMatrix(sky, GetShaderLocation(sky, "invVP"), inv);
    Vector2 res{w, h};
    SetShaderValue(sky, GetShaderLocation(sky, "resolution"), &res, SHADER_UNIFORM_VEC2);
    setVec3(sky, "sunDir", sunDir);
    setVec3(sky, "fogColor", colorVec(fogColor));
    setFloat(sky, "time", time);
    BeginShaderMode(sky);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), WHITE);
    EndShaderMode();
}

void Lighting::endObjects() {
    rlSetTexture(0);
    setDrawMaterial(M_PLAIN);
}

} // namespace client
