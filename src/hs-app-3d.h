/* hs-app-3d.h - software 3D demo app: a spinning cube and a torus (doughnut),
 * flat-shaded with a z-buffer, built entirely on the hs-app.h primitives.
 * No libm, no GPU - pure CPU rasteriser, so it runs on the plain framebuffer.
 *
 * Exposes hs_app_3d() returning an HsApp the host can open as a window, and
 * hs_app_3d_render() to draw one frame straight into a canvas (used by the
 * standalone/preview build and the CI marker path). */
#ifndef HS_APP_3D_H
#define HS_APP_3D_H

#include "hs-app.h"

typedef struct { float x, y, z; } V3;

/* unit cube: 8 vertices, 12 triangles (2 per face), per-face base colour */
static const V3 cube_v[8] = {
  {-1,-1,-1},{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},
  {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}};
static const int cube_f[12][3] = {
  {0,1,2},{0,2,3}, {5,4,7},{5,7,6}, {4,0,3},{4,3,7},
  {1,5,6},{1,6,2}, {4,5,1},{4,1,0}, {3,2,6},{3,6,7}};
static const int cube_fc[6][3] = {
  {220,70,70},{70,200,120},{70,120,220},{220,200,70},{200,70,210},{70,200,210}};

/* rotate a point by yaw (Y) then pitch (X), both in degrees */
static inline V3 rot3(V3 p, float ay, float ax) {
  float cy = hs_cos(ay), sy = hs_sin(ay);
  float x = p.x * cy + p.z * sy, z = -p.x * sy + p.z * cy;
  float cx = hs_cos(ax), sx = hs_sin(ax);
  float y = p.y * cx - z * sx, z2 = p.y * sx + z * cx;
  V3 r = {x, y, z2}; return r;
}

/* project a camera-space point to screen; writes sx,sy and returns depth */
static inline float proj(V3 p, float cx, float cy, float scale, float camz, float *sx, float *sy) {
  float z = p.z + camz; if (z < 0.1f) z = 0.1f;
  *sx = cx + p.x * scale / z; *sy = cy - p.y * scale / z; return z;
}

/* shade a base colour by a 0..1 light factor */
static inline hu32 shade(const int c[3], float l) {
  if (l < 0.15f) l = 0.15f; if (l > 1.0f) l = 1.0f;
  return hs_rgb((int)(c[0] * l), (int)(c[1] * l), (int)(c[2] * l));
}

static inline void hs_app_3d_render(HsCanvas *c, int frame) {
  hs_clear(c, hs_rgb(16, 18, 30));
  float t = frame * 1.6f;
  float cxp = c->w * 0.5f, scale = (c->w < c->h ? c->w : c->h) * 0.9f;
  V3 light = {0.40f, 0.60f, -0.69f}; /* normalised-ish directional light */

  /* ---- cube (left half) ---- */
  float cubecx = c->w * 0.28f, cubecy = c->h * 0.5f;
  for (int f = 0; f < 12; f++) {
    V3 a = rot3(cube_v[cube_f[f][0]], t, t * 0.6f);
    V3 b = rot3(cube_v[cube_f[f][1]], t, t * 0.6f);
    V3 d = rot3(cube_v[cube_f[f][2]], t, t * 0.6f);
    /* face normal via cross product; back-face cull + flat light */
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    float vx = d.x - a.x, vy = d.y - a.y, vz = d.z - a.z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (nz > 0) continue; /* facing away */
    float nl = nx * nx + ny * ny + nz * nz; if (nl < 1e-6f) nl = 1e-6f;
    float inv = 1.0f; { float g = nl; float y = g * 0.5f; /* rough rsqrt */
      y = y; inv = 1.0f / (0.0001f + (g > 0 ? g : 0.0001f)); }
    float l = -(nx * light.x + ny * light.y + nz * light.z) * inv * 2.0f;
    float sx0, sy0, sx1, sy1, sx2, sy2;
    float z0 = proj(a, cubecx, cubecy, scale, 4.0f, &sx0, &sy0);
    float z1 = proj(b, cubecx, cubecy, scale, 4.0f, &sx1, &sy1);
    float z2 = proj(d, cubecx, cubecy, scale, 4.0f, &sx2, &sy2);
    hs_tri(c, sx0, sy0, z0, sx1, sy1, z1, sx2, sy2, z2, shade(cube_fc[f / 2], l));
  }

  /* ---- torus / doughnut (right half) ---- */
  float tcx = c->w * 0.72f, tcy = c->h * 0.5f;
  const int RU = 24, RV = 14; const float R = 1.0f, r = 0.42f;
  const int tc[3] = {235, 170, 70};
  for (int i = 0; i < RU; i++) for (int j = 0; j < RV; j++) {
    float u0 = 360.0f * i / RU, u1 = 360.0f * (i + 1) / RU;
    float v0 = 360.0f * j / RV, v1 = 360.0f * (j + 1) / RV;
    /* torus surface point for (u,v) */
    #define TP(U,V) ((V3){ (R + r * hs_cos(V)) * hs_cos(U), r * hs_sin(V), (R + r * hs_cos(V)) * hs_sin(U) })
    float pitch = 62.0f + 12.0f * hs_sin(t * 0.4f); /* tilt so the hole shows */
    V3 p00 = rot3(TP(u0, v0), t * 0.8f, pitch);
    V3 p10 = rot3(TP(u1, v0), t * 0.8f, pitch);
    V3 p11 = rot3(TP(u1, v1), t * 0.8f, pitch);
    V3 p01 = rot3(TP(u0, v1), t * 0.8f, pitch);
    #undef TP
    for (int th = 0; th < 2; th++) {
      V3 A = p00, B = th ? p11 : p10, C = th ? p01 : p11;
      float ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z;
      float vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
      float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      if (nz > 0) continue;
      float nl = nx * nx + ny * ny + nz * nz; if (nl < 1e-6f) nl = 1e-6f;
      float inv = 1.0f / nl * 1.2f;
      float l = -(nx * light.x + ny * light.y + nz * light.z) * inv;
      if (l < 0) l = -l; l = 0.25f + l * 0.9f;
      float ax, ay, bx, by, cxs, cys;
      float za = proj(A, tcx, tcy, scale, 4.2f, &ax, &ay);
      float zb = proj(B, tcx, tcy, scale, 4.2f, &bx, &by);
      float zc = proj(C, tcx, tcy, scale, 4.2f, &cxs, &cys);
      hs_tri(c, ax, ay, za, bx, by, zb, cxs, cys, zc, shade(tc, l));
    }
  }
  (void)cxp;
}

static void hs_app_3d_draw(HsCanvas *c, int frame, void *ud) { (void)ud; hs_app_3d_render(c, frame); }

static inline HsApp *hs_app_3d(void) {
  static HsApp app;
  app.name = "3D - cube & doughnut"; app.w = 360; app.h = 300;
  app.init = 0; app.draw = hs_app_3d_draw; app.event = 0; app.done = 0;
  app.animated = 1; app.ud = 0;
  return &app;
}

#endif /* HS_APP_3D_H */
