/* hs-app.h - application API for HomeServer OS desktop (hs-gui).
 *
 * An application draws into an HsCanvas (the client area of its window) and
 * optionally handles input events. The host (hs-gui) owns the window, the
 * offscreen canvas and the main loop; it calls app->draw() every frame and
 * blits the canvas onto the screen. Apps never touch /dev/fb0 directly.
 *
 * Everything here is integer/float arithmetic only - NO libm - so the
 * -static build of hs-gui links without -lm. Transcendental helpers
 * (hs_sin/hs_cos) use Bhaskara's approximation, not the C library.
 *
 * Canvas pixels are always 0x00RRGGBB; the host converts to the real
 * framebuffer channel order when it blits. This decouples apps from the
 * hardware pixel layout.
 */
#ifndef HS_APP_H
#define HS_APP_H

typedef unsigned int hu32;

typedef struct {
  hu32 *px;      /* w*h pixels, row-major, 0x00RRGGBB */
  int w, h;      /* client area size in pixels */
  float *zb;     /* optional depth buffer (w*h), larger = farther; NULL to disable */
} HsCanvas;

/* event types delivered to app->event */
enum { HS_EV_MOUSE_MOVE = 1, HS_EV_MOUSE_DOWN, HS_EV_MOUSE_UP, HS_EV_KEY };

typedef struct HsApp HsApp;
struct HsApp {
  const char *name;                                       /* window title */
  int w, h;                                               /* preferred client size */
  void (*init)(HsCanvas *c, void *ud);                    /* optional, once */
  void (*draw)(HsCanvas *c, int frame, void *ud);         /* every frame */
  void (*event)(int type, int x, int y, int key, void *ud); /* optional */
  void (*done)(void *ud);                                 /* optional, on close */
  int animated;                                           /* 1 = redraw continuously */
  void *ud;                                               /* app private data */
};

/* ---- colour ---- */
static inline hu32 hs_rgb(int r, int g, int b) {
  if (r < 0) r = 0; else if (r > 255) r = 255;
  if (g < 0) g = 0; else if (g > 255) g = 255;
  if (b < 0) b = 0; else if (b > 255) b = 255;
  return ((hu32)r << 16) | ((hu32)g << 8) | (hu32)b;
}

/* ---- canvas primitives (operate on the 0xRRGGBB client buffer) ---- */
static inline void hs_clear(HsCanvas *c, hu32 col) {
  int n = c->w * c->h;
  for (int i = 0; i < n; i++) c->px[i] = col;
  if (c->zb) for (int i = 0; i < n; i++) c->zb[i] = 1e30f;
}
static inline void hs_px(HsCanvas *c, int x, int y, hu32 col) {
  if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->px[y * c->w + x] = col;
}
static inline void hs_rect(HsCanvas *c, int x, int y, int w, int h, hu32 col) {
  for (int j = y < 0 ? 0 : y; j < y + h && j < c->h; j++)
    for (int i = x < 0 ? 0 : x; i < x + w && i < c->w; i++) c->px[j * c->w + i] = col;
}
static inline void hs_line(HsCanvas *c, int x0, int y0, int x1, int y1, hu32 col) {
  int dx = x1 - x0, dy = y1 - y0; int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, err = (ax > ay ? ax : -ay) / 2, e2;
  for (;;) { hs_px(c, x0, y0, col); if (x0 == x1 && y0 == y1) break;
    e2 = err; if (e2 > -ax) { err -= ay; x0 += sx; } if (e2 < ay) { err += ax; y0 += sy; } }
}

/* flat-shaded triangle with optional z-buffer. Coordinates are screen-space
 * floats; z is a linear depth used only for the depth test (smaller = nearer). */
static inline void hs_tri(HsCanvas *c, float x0, float y0, float z0,
                          float x1, float y1, float z1,
                          float x2, float y2, float z2, hu32 col) {
  int minx = (int)(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
  int maxx = (int)(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2)) + 1;
  int miny = (int)(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
  int maxy = (int)(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2)) + 1;
  if (minx < 0) minx = 0; if (miny < 0) miny = 0;
  if (maxx > c->w) maxx = c->w; if (maxy > c->h) maxy = c->h;
  float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (area > -0.0001f && area < 0.0001f) return;
  float inv = 1.0f / area;
  for (int y = miny; y < maxy; y++) for (int x = minx; x < maxx; x++) {
    float px = x + 0.5f, py = y + 0.5f;
    float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) * inv;
    float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) * inv;
    float w2 = 1.0f - w0 - w1;
    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
    if (c->zb) { float z = w0 * z0 + w1 * z1 + w2 * z2; int i = y * c->w + x;
      if (z >= c->zb[i]) continue; c->zb[i] = z; c->px[i] = col; }
    else c->px[y * c->w + x] = col;
  }
}

/* ---- trig without libm: Bhaskara I's sine approximation (degrees) ---- */
static inline float hs_sin(float deg) {
  deg -= 360.0f * (float)((int)(deg * (1.0f / 360.0f)));
  if (deg < 0) deg += 360.0f;
  float sign = 1.0f;
  if (deg > 180.0f) { deg -= 180.0f; sign = -1.0f; }
  float x = deg * (180.0f - deg);
  return sign * (4.0f * x) / (40500.0f - x);
}
static inline float hs_cos(float deg) { return hs_sin(deg + 90.0f); }

#endif /* HS_APP_H */
