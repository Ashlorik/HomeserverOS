/* hs-gui - tiny framebuffer desktop for HomeServer OS: wallpaper, icons, taskbar, draggable window, mouse cursor.
 * No libraries. Reads the mouse from /dev/input/mice, draws on /dev/fb0 (needs UEFI GOP/efifb, simpledrm or a KMS driver).
 * Applications are the console programs: the desktop hands over the screen, runs them, and comes back.
 * Test mode (no devices): HS_GUI_TEST=WIDTHxHEIGHT:/path/shot.ppm hs-gui   -> renders one frame to a PPM file. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <errno.h>
#include "hs-font.h"
#include "hs-app.h"
#include "hs-app-3d.h"

typedef unsigned int u32;
static int W = 800, H = 600, fbfd = -1, mfd = -1, tty_gfx = 0, test_mode = 0;
static u32 *fb, *bb; static size_t fbsize; static int stride; /* in pixels */
static int ro, go, bo; /* colour offsets */
static struct termios oldt; static int have_oldt;
static char test_path[256];
/* desktop theme (loaded from /etc/hs/desktop.conf): accent 0..4, wallpaper variant 0..3 */
static int theme_accent = 0, theme_wall = 0;

static u32 rgb(int r, int g, int b) { return ((u32)r << ro) | ((u32)g << go) | ((u32)b << bo); }
/* theme palettes: accent (titlebar/taskbar highlight) and wallpaper top/bottom gradient */
static const int accent_rgb[5][3] = {{36,92,184},{40,150,110},{150,70,190},{190,90,60},{70,80,110}}; /* blue, green, purple, orange, slate */
static const int wall_rgb[4][2][3] = {
  {{18,30,62},{30,74,102}},  /* ocean */
  {{20,40,30},{36,86,70}},   /* forest */
  {{40,24,54},{92,54,110}},  /* twilight */
  {{30,30,34},{60,62,72}} }; /* graphite */
static u32 accent(int shade) { const int *a = accent_rgb[theme_accent]; int s = shade; /* shade: -40..+40 brightness nudge */
  int r = a[0]+s, g = a[1]+s, b = a[2]+s; if (r<0)r=0; if(g<0)g=0; if(b<0)b=0; if(r>255)r=255; if(g>255)g=255; if(b>255)b=255; return rgb(r,g,b); }
static void px(int x, int y, u32 c) { if (x >= 0 && y >= 0 && x < W && y < H) bb[y * W + x] = c; }
static void rect(int x, int y, int w, int h, u32 c) {
  for (int j = y < 0 ? 0 : y; j < y + h && j < H; j++) for (int i = x < 0 ? 0 : x; i < x + w && i < W; i++) bb[j * W + i] = c;
}
static void frame(int x, int y, int w, int h, u32 c) { rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c); rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c); }
/* ---- drawing helpers: alpha blend, rounded rects, shadows, vertical gradient (integer-only, no libm) ---- */
static void text(int x, int y, const char *s, u32 c, int sc);
static int cget(u32 v, int off) { return (v >> off) & 0xff; }
static void blend(int x, int y, u32 c, int a) { /* a: 0..255 */
  if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
  if (a >= 255) { bb[y * W + x] = c; return; }
  u32 d = bb[y * W + x];
  int r = (cget(c, ro) * a + cget(d, ro) * (255 - a)) / 255;
  int g = (cget(c, go) * a + cget(d, go) * (255 - a)) / 255;
  int b = (cget(c, bo) * a + cget(d, bo) * (255 - a)) / 255;
  bb[y * W + x] = rgb(r, g, b);
}
static void vgrad(int x, int y, int w, int h, u32 top, u32 bot) {
  for (int j = 0; j < h && y + j < H; j++) { if (y + j < 0) continue;
    int t = h > 1 ? j * 255 / (h - 1) : 0;
    int r = cget(top, ro) + (cget(bot, ro) - cget(top, ro)) * t / 255;
    int g = cget(top, go) + (cget(bot, go) - cget(top, go)) * t / 255;
    int b = cget(top, bo) + (cget(bot, bo) - cget(top, bo)) * t / 255;
    rect(x, y + j, w, 1, rgb(r, g, b)); }
}
static void rrect(int x, int y, int w, int h, int r, u32 c) { /* filled rounded rect */
  for (int j = 0; j < h; j++) { int dy = j < r ? r - 1 - j : (j >= h - r ? j - (h - r) : -1);
    int inset = 0; if (dy >= 0) { int dx = r - 1; while (dx >= 0 && dx * dx + dy * dy > (r - 1) * (r - 1)) dx--; inset = r - 1 - dx; }
    rect(x + inset, y + j, w - 2 * inset, 1, c); }
}
static void rframe(int x, int y, int w, int h, int r, u32 c, int a) { /* rounded border; corners traced with an integer circle (no libm) */
  for (int i = r; i < w - r; i++) { blend(x + i, y, c, a); blend(x + i, y + h - 1, c, a); }
  for (int j = r; j < h - r; j++) { blend(x, y + j, c, a); blend(x + w - 1, y + j, c, a); }
  for (int dy = 0; dy < r; dy++) { int dx = r; while (dx > 0 && dx * dx + dy * dy > r * r) dx--; int ox = r - dx, oy = r - dy;
    blend(x + ox, y + oy, c, a); blend(x + w - 1 - ox, y + oy, c, a); blend(x + ox, y + h - 1 - oy, c, a); blend(x + w - 1 - ox, y + h - 1 - oy, c, a); }
}
static void shadow(int x, int y, int w, int h, int r) { /* soft drop shadow: filled band below + right, alpha falls off with distance */
  for (int s = 1; s <= r; s++) { int a = 50 - 50 * s / (r + 1);
    for (int i = 0; i < w; i++) blend(x + i + s, y + h - 1 + s, rgb(0, 0, 0), a);   /* bottom */
    for (int j = 0; j < h; j++) blend(x + w - 1 + s, y + j + s, rgb(0, 0, 0), a);   /* right */
  }
}
/* vector-ish 32x32 icons drawn with primitives; id selects the picture */
static void draw_icon(int x, int y, int id, u32 fg) {
  int cx = x + 16, cy = y + 16; (void)cx; (void)cy;
  switch (id) {
    case 0: /* terminal: window with > _ prompt */
      rframe(x + 3, y + 5, 26, 22, 3, fg, 255); rect(x + 7, y + 11, 2, 2, fg); rect(x + 9, y + 13, 2, 2, fg); rect(x + 7, y + 15, 2, 2, fg); rect(x + 13, y + 17, 8, 2, fg); break;
    case 1: /* files: folder */
      rect(x + 4, y + 10, 11, 3, fg); rrect(x + 4, y + 11, 24, 14, 2, fg); rect(x + 6, y + 14, 20, 9, rgb(236,240,246)); break;
    case 2: /* monitor: bar chart */
      rect(x + 6, y + 18, 4, 8, fg); rect(x + 13, y + 12, 4, 14, fg); rect(x + 20, y + 7, 4, 19, fg); break;
    case 3: { /* menu/settings: gear (ring + 8 teeth at fixed offsets, no libm) */
      static const int teeth[8][2] = {{12,0},{8,3},{0,12},{3,8},{12,24},{8,21},{24,12},{21,8}};
      static const int teeth2[8][2] = {{12,24},{16,21},{24,12},{21,16},{12,0},{16,3},{0,12},{3,16}};
      for (int t = 0; t < 8; t++) { blend(x + teeth[t][0], y + teeth[t][1], fg, 255); blend(x + teeth2[t][0], y + teeth2[t][1], fg, 255); }
      rframe(x + 8, y + 8, 16, 16, 7, fg, 255); rframe(x + 12, y + 12, 8, 8, 3, fg, 255); break; }
    case 4: /* browser: globe */
      rframe(x + 4, y + 4, 24, 24, 11, fg, 255); rect(x + 15, y + 5, 2, 22, fg); rect(x + 5, y + 15, 22, 2, fg); rframe(x + 9, y + 4, 14, 24, 7, fg, 180); break;
    case 5: /* apps: grid of squares */
      for (int gy = 0; gy < 2; gy++) for (int gx = 0; gx < 2; gx++) rrect(x + 6 + gx * 11, y + 6 + gy * 11, 8, 8, 2, fg); break;
    case 6: /* install: disk with down arrow */
      rframe(x + 4, y + 6, 24, 20, 3, fg, 255); rect(x + 15, y + 10, 2, 8, fg); for (int i = 0; i < 5; i++) rect(x + 12 + i, y + 16 + i, 2, 2, fg), rect(x + 20 - i, y + 16 + i, 2, 2, fg); break;
    default: text(x + 10, y + 8, "?", fg, 2);
  }
}
static int glyph(unsigned cp) { for (unsigned i = 0; i < sizeof hs_cp / sizeof *hs_cp; i++) if (hs_cp[i] == cp) return i; return 0; }
static void text(int x, int y, const char *s, u32 c, int sc) {
  while (*s) {
    unsigned cp = (unsigned char)*s++;
    if (cp >= 0xC0) { int n = cp >= 0xE0 ? 2 : 1; cp &= n == 2 ? 0x0F : 0x1F; while (n-- && *s) cp = (cp << 6) | ((unsigned char)*s++ & 0x3F); }
    const unsigned char *g = hs_font[glyph(cp)];
    for (int j = 0; j < 16; j++) for (int i = 0; i < 8; i++) if (g[j] & (0x80 >> i))
      for (int a = 0; a < sc; a++) for (int b = 0; b < sc; b++) px(x + i * sc + a, y + j * sc + b, c);
    x += 8 * sc;
  }
}
static int tw(const char *s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n * 8; }


/* ---- terminal emulator (xterm-like subset: enough for sh, nnn, btop, dialog) ---- */
typedef struct { unsigned cp; unsigned char fg, bg, at; } Cell; /* at: 1 bold, 2 reverse */
typedef struct {
  int cols, rows, cx, cy, sx, sy, top, bot, fd, inalt, st, np, priv, line0, wrap, appkeys;
  pid_t pid; Cell *g, *alt; int par[16]; unsigned utf; int utfn; unsigned char fg, bg, at;
} Term;
static u32 pal(int i) {
  static const int base[16][3] = {{0,0,0},{170,0,0},{0,170,0},{170,85,0},{0,0,170},{170,0,170},{0,170,170},{170,170,170},
    {85,85,85},{255,85,85},{85,255,85},{255,255,85},{85,85,255},{255,85,255},{85,255,255},{255,255,255}};
  if (i < 16) return rgb(base[i][0], base[i][1], base[i][2]);
  if (i < 232) { i -= 16; int r = i / 36, g = i / 6 % 6, b = i % 6; return rgb(r ? r * 40 + 55 : 0, g ? g * 40 + 55 : 0, b ? b * 40 + 55 : 0); }
  i = (i - 232) * 10 + 8; return rgb(i, i, i);
}
static void t_clear(Term *t, int x0, int y0, int x1, int y1) { /* inclusive-exclusive */
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { Cell *c = &t->g[y * t->cols + x]; c->cp = ' '; c->fg = 7; c->bg = 0; c->at = 0; }
}
static void t_scroll(Term *t, int n) { /* n>0: up (content moves up), n<0: down; within region */
  int w = t->cols, h = t->bot - t->top + 1; if (h < 1) return;
  if (n > 0) { if (n > h) n = h; memmove(t->g + t->top * w, t->g + (t->top + n) * w, (size_t)(h - n) * w * sizeof(Cell)); t_clear(t, 0, t->bot - n + 1, w, t->bot + 1); }
  else { n = -n; if (n > h) n = h; memmove(t->g + (t->top + n) * w, t->g + t->top * w, (size_t)(h - n) * w * sizeof(Cell)); t_clear(t, 0, t->top, w, t->top + n); }
}
static void t_put(Term *t, unsigned cp) {
  if (t->line0 && cp >= 0x6a && cp <= 0x78) { static const char m[] = "+++++-----+|"; /* j..x line drawing -> ASCII */
    cp = (cp == 0x71) ? '-' : (cp == 0x78) ? '|' : (cp == 0x61) ? '#' : '+'; (void)m; }
  if (t->wrap) { t->cx = 0; if (t->cy == t->bot) t_scroll(t, 1); else if (t->cy < t->rows - 1) t->cy++; t->wrap = 0; }
  Cell *c = &t->g[t->cy * t->cols + t->cx]; c->cp = cp; c->fg = t->fg; c->bg = t->bg; c->at = t->at;
  if (t->cx < t->cols - 1) t->cx++; else t->wrap = 1;
}
static int pa(Term *t, int i, int d) { return (i < t->np && t->par[i] > 0) ? t->par[i] : d; }
static void t_sgr(Term *t) {
  if (t->np == 0) { t->np = 1; t->par[0] = 0; }
  for (int i = 0; i < t->np; i++) {
    int v = t->par[i];
    if (v == 0) { t->fg = 7; t->bg = 0; t->at = 0; } else if (v == 1) t->at |= 1; else if (v == 22) t->at &= ~1; else if (v == 7) t->at |= 2; else if (v == 27) t->at &= ~2;
    else if (v >= 30 && v <= 37) t->fg = v - 30; else if (v == 39) t->fg = 7; else if (v >= 40 && v <= 47) t->bg = v - 40; else if (v == 49) t->bg = 0;
    else if (v >= 90 && v <= 97) t->fg = v - 90 + 8; else if (v >= 100 && v <= 107) t->bg = v - 100 + 8;
    else if ((v == 38 || v == 48) && i + 1 < t->np) {
      int idx = 0;
      if (t->par[i + 1] == 5 && i + 2 < t->np) { idx = t->par[i + 2]; i += 2; }
      else if (t->par[i + 1] == 2 && i + 4 < t->np) { int r = t->par[i + 2], g = t->par[i + 3], b = t->par[i + 4]; i += 4;
        idx = (r == g && g == b) ? 232 + r * 23 / 255 : 16 + 36 * (r * 5 / 255) + 6 * (g * 5 / 255) + b * 5 / 255; }
      if (v == 38) t->fg = idx; else t->bg = idx;
    }
  }
}
static void t_csi(Term *t, int f) {
  int x = t->cx, y = t->cy;
  if (t->priv) { /* private modes: cursor keys mode, alternate screen */
    if (f == 'h' || f == 'l') for (int i = 0; i < t->np; i++) if (t->par[i] == 1) t->appkeys = (f == 'h');
    if (f == 'h' || f == 'l') for (int i = 0; i < t->np; i++) if (t->par[i] == 1049 || t->par[i] == 47 || t->par[i] == 1047) {
      int want = f == 'h'; if (want != t->inalt) { Cell *tmp = t->g; t->g = t->alt; t->alt = tmp; t->inalt = want; if (want) t_clear(t, 0, 0, t->cols, t->rows); }
    }
    return;
  }
  switch (f) {
  case 'A': t->cy = y - pa(t, 0, 1) < t->top ? t->top : y - pa(t, 0, 1); break;
  case 'B': t->cy = y + pa(t, 0, 1) > t->bot ? t->bot : y + pa(t, 0, 1); break;
  case 'C': t->cx = x + pa(t, 0, 1) >= t->cols ? t->cols - 1 : x + pa(t, 0, 1); break;
  case 'D': t->cx = x - pa(t, 0, 1) < 0 ? 0 : x - pa(t, 0, 1); break;
  case 'E': t->cy = y + pa(t, 0, 1) > t->bot ? t->bot : y + pa(t, 0, 1); t->cx = 0; break;
  case 'F': t->cy = y - pa(t, 0, 1) < t->top ? t->top : y - pa(t, 0, 1); t->cx = 0; break;
  case 'G': t->cx = pa(t, 0, 1) - 1; break;
  case 'd': t->cy = pa(t, 0, 1) - 1; break;
  case 'H': case 'f': t->cy = pa(t, 0, 1) - 1; t->cx = pa(t, 1, 1) - 1; break;
  case 'J': { int m = t->np ? t->par[0] : 0; if (m == 0) { t_clear(t, x, y, t->cols, y + 1); t_clear(t, 0, y + 1, t->cols, t->rows); } else if (m == 1) { t_clear(t, 0, 0, t->cols, y); t_clear(t, 0, y, x + 1, y + 1); } else t_clear(t, 0, 0, t->cols, t->rows); break; }
  case 'K': { int m = t->np ? t->par[0] : 0; if (m == 0) t_clear(t, x, y, t->cols, y + 1); else if (m == 1) t_clear(t, 0, y, x + 1, y + 1); else t_clear(t, 0, y, t->cols, y + 1); break; }
  case 'L': { int n = pa(t, 0, 1), ot = t->top; t->top = y; t_scroll(t, -n); t->top = ot; break; }
  case 'M': { int n = pa(t, 0, 1), ot = t->top; t->top = y; t_scroll(t, n); t->top = ot; break; }
  case 'P': { int n = pa(t, 0, 1); if (n > t->cols - x) n = t->cols - x; Cell *r = t->g + y * t->cols; memmove(r + x, r + x + n, (size_t)(t->cols - x - n) * sizeof(Cell)); t_clear(t, t->cols - n, y, t->cols, y + 1); break; }
  case '@': { int n = pa(t, 0, 1); if (n > t->cols - x) n = t->cols - x; Cell *r = t->g + y * t->cols; memmove(r + x + n, r + x, (size_t)(t->cols - x - n) * sizeof(Cell)); t_clear(t, x, y, x + n, y + 1); break; }
  case 'X': { int n = pa(t, 0, 1); t_clear(t, x, y, x + n > t->cols ? t->cols : x + n, y + 1); break; }
  case 'S': t_scroll(t, pa(t, 0, 1)); break;
  case 'T': t_scroll(t, -pa(t, 0, 1)); break;
  case 'm': t_sgr(t); break;
  case 'r': t->top = pa(t, 0, 1) - 1; t->bot = pa(t, 1, t->rows) - 1; if (t->bot >= t->rows) t->bot = t->rows - 1; if (t->top >= t->bot) { t->top = 0; t->bot = t->rows - 1; } t->cx = 0; t->cy = 0; break;
  case 's': t->sx = x; t->sy = y; break;
  case 'u': t->cx = t->sx; t->cy = t->sy; break;
  case 'n': if (t->np && t->par[0] == 6) { char r[32]; int n = snprintf(r, sizeof r, "\033[%d;%dR", y + 1, x + 1); (void)!write(t->fd, r, n); } break;
  case 'c': (void)!write(t->fd, "\033[?1;2c", 7); break;
  }
  if (t->cx < 0) t->cx = 0; if (t->cx >= t->cols) t->cx = t->cols - 1; if (t->cy < 0) t->cy = 0; if (t->cy >= t->rows) t->cy = t->rows - 1;
}
static void t_feed(Term *t, unsigned char b) {
  switch (t->st) {
  case 1: /* after ESC */
    t->st = 0;
    if (b == '[') { t->st = 2; t->np = 0; t->priv = 0; memset(t->par, 0, sizeof t->par); }
    else if (b == ']') t->st = 4; else if (b == '(') t->st = 5; else if (b == ')' || b == '#' || b == '%') t->st = 6;
    else if (b == '7') { t->sx = t->cx; t->sy = t->cy; } else if (b == '8') { t->cx = t->sx; t->cy = t->sy; }
    else if (b == 'M') { if (t->cy == t->top) t_scroll(t, -1); else if (t->cy > 0) t->cy--; }
    else if (b == 'D') { if (t->cy == t->bot) t_scroll(t, 1); else t->cy++; }
    else if (b == 'E') { t->cx = 0; if (t->cy == t->bot) t_scroll(t, 1); else t->cy++; }
    else if (b == 'c') { t->fg = 7; t->bg = 0; t->at = 0; t_clear(t, 0, 0, t->cols, t->rows); t->cx = t->cy = 0; }
    return;
  case 2: /* CSI */
    if (b >= '0' && b <= '9') { if (t->np == 0) t->np = 1; t->par[t->np - 1] = t->par[t->np - 1] * 10 + (b - '0'); }
    else if (b == ';') { if (t->np == 0) t->np = 1; if (t->np < 15) t->np++; }
    else if (b == '?' || b == '>' || b == '<' || b == '=') t->priv = 1;
    else if (b >= 0x20 && b <= 0x2f) { }
    else { t->st = 0; t_csi(t, b); }
    return;
  case 4: if (b == 7 || b == 0x9c) t->st = 0; else if (b == 27) t->st = 7; return; /* OSC: ignored */
  case 7: t->st = 0; return;
  case 5: t->line0 = (b == '0'); t->st = 0; return;
  case 6: t->st = 0; return;
  }
  if (b == 27) { t->st = 1; return; }
  if (b < 32 || b == 127) {
    t->wrap = 0;
    if (b == '\n' || b == 11 || b == 12) { if (t->cy == t->bot) t_scroll(t, 1); else if (t->cy < t->rows - 1) t->cy++; }
    else if (b == '\r') t->cx = 0; else if (b == 8) { if (t->cx > 0) t->cx--; }
    else if (b == '\t') { t->cx = (t->cx / 8 + 1) * 8; if (t->cx >= t->cols) t->cx = t->cols - 1; }
    return;
  }
  if (b >= 0x80) { /* UTF-8 */
    if (b >= 0xC0) { t->utfn = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1; t->utf = b & (0x3F >> t->utfn); }
    else if (t->utfn > 0) { t->utf = (t->utf << 6) | (b & 0x3F); if (--t->utfn == 0) t_put(t, t->utf); }
    return;
  }
  t_put(t, b);
}
static Term *t_new(int cols, int rows, const char *cmd) {
  Term *t = calloc(1, sizeof *t); t->cols = cols; t->rows = rows; t->top = 0; t->bot = rows - 1; t->fg = 7;
  t->g = calloc((size_t)cols * rows, sizeof(Cell)); t->alt = calloc((size_t)cols * rows, sizeof(Cell)); t_clear(t, 0, 0, cols, rows);
  Cell *sv = t->g; t->g = t->alt; t_clear(t, 0, 0, cols, rows); t->g = sv;
  int m = posix_openpt(O_RDWR | O_NOCTTY); if (m < 0 || grantpt(m) || unlockpt(m)) { free(t); return NULL; }
  char *sn = ptsname(m); struct winsize ws = {rows, cols, 0, 0}; ioctl(m, TIOCSWINSZ, &ws);
  pid_t p = fork();
  if (p == 0) {
    setsid(); int s = open(sn, O_RDWR); ioctl(s, TIOCSCTTY, 0); dup2(s, 0); dup2(s, 1); dup2(s, 2); if (s > 2) close(s); close(m);
    setenv("TERM", "xterm-256color", 1); char b[16]; snprintf(b, sizeof b, "%d", cols); setenv("COLUMNS", b, 1); snprintf(b, sizeof b, "%d", rows); setenv("LINES", b, 1);
    execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127);
  }
  t->fd = m; t->pid = p; fcntl(m, F_SETFL, O_NONBLOCK); return t;
}
static void t_free(Term *t) { if (t->pid > 0) { kill(t->pid, SIGHUP); waitpid(t->pid, NULL, WNOHANG); } close(t->fd); free(t->g); free(t->alt); free(t); }

/* ---- windows ----
 * A window is a content container. Its content is one of:
 *   - a terminal (Term *t),
 *   - an application (HsApp *app drawing into an offscreen HsCanvas), or
 *   - the built-in info panel (neither t nor app).
 * Window state adds min (minimised) and max (maximised: fills the screen
 * above the taskbar; rx/ry/rw/rh remember the floating geometry to restore). */
#define MAXWIN 8
struct win { int used, x, y, w, h, min, max, rx, ry, rw, rh; char title[40];
  Term *t; HsApp *app; HsCanvas canvas; int frame; };
static struct win wins[MAXWIN]; static int zorder[MAXWIN], nz; /* zorder[nz-1] is the focused window */
static void raise_win(int i) { int k; for (k = 0; k < nz && zorder[k] != i; k++); if (k == nz) return; memmove(zorder + k, zorder + k + 1, (nz - k - 1) * sizeof(int)); zorder[nz - 1] = i; }
static int new_win(int w, int h, const char *title, Term *t, int x, int y) {
  for (int i = 0; i < MAXWIN; i++) if (!wins[i].used) { wins[i] = (struct win){.used = 1, .x = x, .y = y, .w = w, .h = h, .t = t}; snprintf(wins[i].title, sizeof wins[i].title, "%s", title); zorder[nz++] = i; return i; }
  return -1;
}
static void close_win(int i) {
  if (wins[i].t) t_free(wins[i].t);
  if (wins[i].app) { if (wins[i].app->done) wins[i].app->done(wins[i].app->ud); free(wins[i].canvas.px); free(wins[i].canvas.zb); }
  wins[i].used = 0; int k; for (k = 0; k < nz && zorder[k] != i; k++); if (k < nz) { memmove(zorder + k, zorder + k + 1, (nz - k - 1) * sizeof(int)); nz--; }
}
static int open_term(const char *title, const char *cmd) {
  static int n; int cols = 80, rows = 24; if (W < 900) { cols = 60; rows = 20; }
  Term *t = t_new(cols, rows, cmd); if (!t) return -1;
  int off = (n++ % 6) * 28; return new_win(cols * 8 + 4, rows * 16 + 30, title, t, 90 + off, 30 + off);
}
/* the client area of a window is everything below the 26px titlebar, inset 2px */
static void win_client(struct win *w, int *cx, int *cy, int *cw, int *ch) {
  *cx = w->x + 2; *cy = w->y + 26; *cw = w->w - 4; *ch = w->h - 28;
  if (*cw < 1) *cw = 1; if (*ch < 1) *ch = 1;
}
/* (re)allocate an app window's canvas to match its current client size */
static void app_fit_canvas(struct win *w) {
  int cx, cy, cw, ch; win_client(w, &cx, &cy, &cw, &ch);
  if (w->canvas.w == cw && w->canvas.h == ch && w->canvas.px) return;
  free(w->canvas.px); free(w->canvas.zb);
  w->canvas.w = cw; w->canvas.h = ch;
  w->canvas.px = calloc((size_t)cw * ch, sizeof(hu32));
  w->canvas.zb = w->app && w->app->animated ? calloc((size_t)cw * ch, sizeof(float)) : NULL;
}
static int open_app(HsApp *app) {
  static int n; int cw = app->w, ch = app->h;
  int off = (n++ % 6) * 28, x = 120 + off, y = 40 + off;
  int i = new_win(cw + 4, ch + 28, app->name, NULL, x, y);
  if (i < 0) return -1;
  wins[i].app = app; app_fit_canvas(&wins[i]);
  if (app->init) app->init(&wins[i].canvas, app->ud);
  return i;
}

/* ---- state ---- */
struct icon { const char *label, *cmd; int r, g, b; int pic; int full; };
static struct icon icons[] = {
  {"Terminal", "sh -l", 40, 160, 90, 0, 0}, {"Files", "nnn", 230, 160, 40, 1, 0}, {"Monitor", "btop", 60, 140, 230, 2, 0},
  {"Menu", "hs-menu", 170, 80, 220, 3, 0}, {"Browser", "hs-web", 220, 70, 90, 4, 1}, {"Apps", "hs-apps", 40, 180, 190, 5, 0},
};
#define NICON (int)(sizeof icons / sizeof *icons)
static struct { const char *label, *cmd; } start_items[] = {
  {"New terminal", "sh -l"}, {"Management menu", "hs-menu"}, {"Apps (launcher)", "hs-launch"}, {"Install to disk", ":install"}, {"Exit to console", ":exit"}, {"Restart", "reboot"}, {"Power off", "poweroff"}};
#define NSTART (int)(sizeof start_items / sizeof *start_items)
/* right-click context menu on the desktop */
static struct { const char *label, *cmd; } ctx_items[] = {
  {"New terminal", "sh -l"}, {"File manager", "nnn"}, {"3D demo", ":demo3d"}, {"Settings", ":settings"}, {"Management menu", "hs-menu"}};
#define NCTX (int)(sizeof ctx_items / sizeof *ctx_items)
static int mx, my, mb, mbr, start_open, drag = -1, dgx, dgy;
static int ctx_open, ctx_x, ctx_y; /* context menu position when open */
static int settings_open;           /* settings panel visible */
static char info[8][96]; static int ninfo; static time_t info_t;

#define CONF_PATH "/etc/hs/desktop.conf"
static void load_theme(void) {
  FILE *f = fopen(CONF_PATH, "r"); if (!f) return;
  char line[128];
  while (fgets(line, sizeof line, f)) { int v;
    if (sscanf(line, "accent=%d", &v) == 1 && v >= 0 && v < 5) theme_accent = v;
    else if (sscanf(line, "wallpaper=%d", &v) == 1 && v >= 0 && v < 4) theme_wall = v;
  }
  fclose(f);
}
static void save_theme(void) {
  FILE *f = fopen(CONF_PATH, "w"); if (!f) return;
  fprintf(f, "# HomeServer OS desktop theme\naccent=%d\nwallpaper=%d\n", theme_accent, theme_wall);
  fclose(f);
}

static void load_info(void) {
  ninfo = 0; FILE *p = popen("hs-status plain 2>/dev/null", "r");
  if (p) { while (ninfo < 8 && fgets(info[ninfo], sizeof info[0], p)) { info[ninfo][strcspn(info[ninfo], "\n")] = 0; ninfo++; } pclose(p); }
  if (!ninfo) { snprintf(info[0], sizeof info[0], "HomeServer OS"); ninfo = 1; }
  info_t = time(NULL);
}
static void shadowtext(int x, int y, const char *s, u32 c, int sc) { text(x + 1, y + 1, s, rgb(0, 0, 0), sc); text(x, y, s, c, sc); }

static void draw_term(struct win *w, int focused) {
  Term *t = w->t; int ox = w->x + 2, oy = w->y + 26;
  for (int y = 0; y < t->rows; y++) for (int x = 0; x < t->cols; x++) {
    Cell *c = &t->g[y * t->cols + x]; int fg = c->fg, bg = c->bg; if ((c->at & 1) && fg < 8) fg += 8; if (c->at & 2) { int q = fg; fg = bg; bg = q; }
    int cur = focused && x == t->cx && y == t->cy; if (cur) { int q = fg; fg = bg; bg = q; }
    if (bg || cur) rect(ox + x * 8, oy + y * 16, 8, 16, pal(bg)); else rect(ox + x * 8, oy + y * 16, 8, 16, rgb(0, 0, 0));
    if (c->cp > ' ') { char u[5]; int n = 0; unsigned cp = c->cp;
      if (cp < 0x80) u[n++] = cp; else if (cp < 0x800) { u[n++] = 0xC0 | cp >> 6; u[n++] = 0x80 | (cp & 63); } else { u[n++] = 0xE0 | cp >> 12; u[n++] = 0x80 | (cp >> 6 & 63); u[n++] = 0x80 | (cp & 63); }
      u[n] = 0; text(ox + x * 8, oy + y * 16, u, pal(fg), 1); }
  }
}
/* app engine: run the app's draw() into its 0xRRGGBB canvas, then blit the
 * canvas into the backbuffer, translating to the framebuffer channel order. */
static void draw_app(struct win *w) {
  app_fit_canvas(w);
  if (w->app->draw) w->app->draw(&w->canvas, w->frame, w->app->ud);
  int ox = w->x + 2, oy = w->y + 26, cw = w->canvas.w, ch = w->canvas.h;
  for (int y = 0; y < ch; y++) { int sy = oy + y; if (sy < 0 || sy >= H) continue;
    hu32 *src = w->canvas.px + (size_t)y * cw;
    for (int x = 0; x < cw; x++) { int sx = ox + x; if (sx < 0 || sx >= W) continue;
      hu32 v = src[x]; bb[sy * W + sx] = rgb((v >> 16) & 255, (v >> 8) & 255, v & 255); }
  }
}
#define TBAR 40          /* taskbar height */
static void draw(void) {
  /* wallpaper: smooth vertical gradient from the chosen theme */
  { const int (*wc)[3] = wall_rgb[theme_wall]; u32 top = rgb(wc[0][0], wc[0][1], wc[0][2]), bot = rgb(wc[1][0], wc[1][1], wc[1][2]);
    for (int y = 0; y < H; y++) { int t = y * 255 / (H > 1 ? H - 1 : 1);
      int r = cget(top, ro) + (cget(bot, ro) - cget(top, ro)) * t / 255;
      int g = cget(top, go) + (cget(bot, go) - cget(top, go)) * t / 255;
      int b = cget(top, bo) + (cget(bot, bo) - cget(top, bo)) * t / 255;
      rect(0, y, W, 1, rgb(r, g, b)); } }
  shadowtext(W - 8 * 2 * 13 - 28, H - TBAR - 52, "HomeServer OS", rgb(225, 238, 255), 2);
  for (int i = 0; i < NICON; i++) {
    int x = 28, y = 28 + i * 94; int hot = mx >= x - 4 && mx < x + 68 && my >= y - 4 && my < y + 68;
    if (hot) for (int j = -6; j < 70; j++) for (int xx = -6; xx < 70; xx++) blend(x + xx, y + j, rgb(255, 255, 255), 36); /* soft hover glow */
    shadow(x, y, 64, 64, 5);
    rrect(x, y, 64, 64, 12, rgb(icons[i].r, icons[i].g, icons[i].b));
    /* glossy top highlight */
    for (int j = 0; j < 20; j++) for (int xx = 6; xx < 58; xx++) blend(x + xx, y + 4 + j, rgb(255, 255, 255), (20 - j) * 2);
    draw_icon(x + 16, y + 16, icons[i].pic, rgb(255, 255, 255));
    rframe(x, y, 64, 64, 12, hot ? rgb(255, 255, 255) : rgb(12, 18, 36), hot ? 220 : 120);
    shadowtext(x + 32 - tw(icons[i].label) / 2, y + 70, icons[i].label, rgb(240, 246, 255), 1);
  }
  for (int k = 0; k < nz; k++) {
    struct win *w = &wins[zorder[k]]; int foc = k == nz - 1;
    if (w->min) continue; /* minimised: shown only on the taskbar */
    shadow(w->x, w->y, w->w, w->h, foc ? 8 : 4);
    rect(w->x, w->y + 26, w->w, w->h - 26, w->t ? rgb(12, 14, 20) : rgb(240, 243, 248));
    /* titlebar: rounded top, gradient (accent when focused) */
    rrect(w->x, w->y, w->w, 26, 8, foc ? accent(0) : rgb(64, 74, 104));
    vgrad(w->x + 1, w->y + 8, w->w - 2, 18, foc ? accent(16) : rgb(70, 80, 112), foc ? accent(-24) : rgb(54, 62, 92));
    text(w->x + 12, w->y + 5, w->title, rgb(255, 255, 255), 1);
    /* window buttons as circles: maximise (green), minimise (yellow), close (red) */
    rrect(w->x + w->w - 54, w->y + 8, 12, 12, 6, rgb(96, 200, 110));
    rrect(w->x + w->w - 38, w->y + 8, 12, 12, 6, rgb(240, 192, 64));
    rrect(w->x + w->w - 22, w->y + 8, 12, 12, 6, rgb(226, 86, 74)); text(w->x + w->w - 19, w->y + 6, "x", rgb(120, 20, 16), 1);
    frame(w->x, w->y + 26, w->w, w->h - 26, foc ? rgb(24, 56, 110) : rgb(40, 46, 70));
    if (w->t) draw_term(w, foc);
    else if (w->app) draw_app(w);
    else { for (int i = 0; i < ninfo; i++) text(w->x + 16, w->y + 44 + i * 22, info[i], rgb(28, 38, 58), 1);
      text(w->x + 16, w->y + w->h - 30, "Click an icon to open a program.", rgb(110, 120, 140), 1); }
  }
  /* taskbar */
  vgrad(0, H - TBAR, W, TBAR, rgb(26, 32, 54), rgb(14, 18, 34));
  rect(0, H - TBAR, W, 1, rgb(70, 110, 190));
  { int hot = mx >= 6 && mx < 134 && my >= H - TBAR + 5; rrect(6, H - TBAR + 5, 128, TBAR - 10, 8, start_open || hot ? accent(24) : accent(-20));
    draw_icon(10, H - TBAR + 4, 5, rgb(255, 255, 255)); text(46, H - TBAR / 2 - 7, "Home", rgb(255, 255, 255), 1); }
  { int x = 146; for (int i = 0; i < MAXWIN; i++) if (wins[i].used) { int foc = nz && zorder[nz - 1] == i && !wins[i].min;
      rrect(x, H - TBAR + 6, 150, TBAR - 12, 6, foc ? accent(16) : (wins[i].min ? rgb(30, 36, 62) : rgb(40, 48, 84)));
      if (foc) rect(x + 8, H - 8, 134, 2, accent(60));
      char b[20]; snprintf(b, sizeof b, "%.17s", wins[i].title); text(x + 10, H - TBAR / 2 - 7, b, wins[i].min ? rgb(150, 160, 185) : rgb(235, 240, 255), 1); x += 156; } }
  time_t t = time(NULL); struct tm *tm = localtime(&t); char clk[32]; strftime(clk, sizeof clk, "%H:%M", tm); char dt[32]; strftime(dt, sizeof dt, "%d.%m.%Y", tm);
  text(W - tw(clk) - 16, H - TBAR / 2 - 15, clk, rgb(240, 245, 255), 1);
  text(W - tw(dt) - 16, H - TBAR / 2 + 2, dt, rgb(170, 185, 215), 1);
  if (start_open) {
    int h = NSTART * 30 + 10, sy = H - TBAR - h - 4;
    shadow(6, sy, 220, h, 8); rrect(6, sy, 220, h, 8, rgb(26, 32, 58)); rframe(6, sy, 220, h, 8, rgb(80, 110, 190), 200);
    for (int i = 0; i < NSTART; i++) { int y = sy + 5 + i * 30; int hot = mx >= 10 && mx < 222 && my >= y && my < y + 30;
      if (hot) rrect(10, y, 212, 30, 5, accent(16));
      text(20, y + 7, start_items[i].label, rgb(240, 245, 255), 1); }
  }
  if (ctx_open) {
    int h = NCTX * 30 + 10;
    shadow(ctx_x, ctx_y, 200, h, 8); rrect(ctx_x, ctx_y, 200, h, 8, rgb(26, 32, 58)); rframe(ctx_x, ctx_y, 200, h, 8, rgb(80, 110, 190), 200);
    for (int i = 0; i < NCTX; i++) { int y = ctx_y + 5 + i * 30; int hot = mx >= ctx_x + 4 && mx < ctx_x + 196 && my >= y && my < y + 30;
      if (hot) rrect(ctx_x + 4, y, 192, 30, 5, accent(16));
      text(ctx_x + 14, y + 7, ctx_items[i].label, rgb(240, 245, 255), 1); }
  }
  if (settings_open) {
    int pw = 420, ph = 250, px0 = (W - pw) / 2, py0 = (H - ph) / 2;
    shadow(px0, py0, pw, ph, 10); rrect(px0, py0, pw, ph, 10, rgb(30, 36, 60)); rframe(px0, py0, pw, ph, 10, accent(30), 220);
    rrect(px0, py0, pw, 32, 10, accent(0)); text(px0 + 16, py0 + 9, "Desktop settings", rgb(255, 255, 255), 1);
    rrect(px0 + pw - 24, py0 + 10, 12, 12, 6, rgb(226, 86, 74)); text(px0 + pw - 21, py0 + 8, "x", rgb(120, 20, 16), 1);
    text(px0 + 20, py0 + 50, "Accent colour", rgb(210, 220, 240), 1);
    for (int i = 0; i < 5; i++) { int cx = px0 + 24 + i * 56, cy = py0 + 74; const int *a = accent_rgb[i];
      rrect(cx, cy, 44, 32, 8, rgb(a[0], a[1], a[2]));
      if (i == theme_accent) rframe(cx - 2, cy - 2, 48, 36, 9, rgb(255, 255, 255), 255); }
    text(px0 + 20, py0 + 122, "Wallpaper", rgb(210, 220, 240), 1);
    for (int i = 0; i < 4; i++) { int cx = px0 + 24 + i * 72, cy = py0 + 146; const int (*wc)[3] = wall_rgb[i];
      for (int yy = 0; yy < 40; yy++) { int t = yy * 255 / 39;
        int r = wc[0][0] + (wc[1][0]-wc[0][0])*t/255, g = wc[0][1] + (wc[1][1]-wc[0][1])*t/255, b = wc[0][2] + (wc[1][2]-wc[0][2])*t/255;
        rect(cx, cy + yy, 60, 1, rgb(r, g, b)); }
      if (i == theme_wall) rframe(cx - 2, cy - 2, 64, 44, 4, rgb(255, 255, 255), 255); }
    text(px0 + 20, py0 + ph - 26, "Click a swatch to apply. Saved to /etc/hs.", rgb(150, 160, 185), 1);
  }
  static const char *cur[] = {"X","XX","X.X","X..X","X...X","X....X","X.....X","X......X","X.......X","X........X","X.....XXXXX","X..X..X","X.X X..X","XX  X..X","X    X..X","     X..X","      XX"};
  for (int j = 0; j < 17; j++) for (int i = 0; cur[j][i]; i++) { char c = cur[j][i]; if (c == 'X') px(mx + i, my + j, rgb(0, 0, 0)); else if (c == '.') px(mx + i, my + j, rgb(255, 255, 255)); }
}
static void flip(void) {
  if (test_mode) return;
  for (int y = 0; y < H; y++) memcpy(fb + (size_t)y * stride, bb + (size_t)y * W, (size_t)W * 4);
}

static void gfx(int on) {
  if (test_mode) return;
  if (on) { if (!tty_gfx && ioctl(0, KDSETMODE, KD_GRAPHICS) == 0) tty_gfx = 1; }
  else if (tty_gfx) { ioctl(0, KDSETMODE, KD_TEXT); tty_gfx = 0; }
}
static void rawtty(int on) {
  if (test_mode) return;
  if (on) { if (tcgetattr(0, &oldt) == 0) { have_oldt = 1; struct termios t = oldt; t.c_iflag &= ~(IXON | ICRNL); t.c_lflag &= ~(ICANON | ECHO | ISIG); t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &t); } }
  else if (have_oldt) tcsetattr(0, TCSANOW, &oldt);
}
static void leave(int code) { for (int i = 0; i < MAXWIN; i++) if (wins[i].used) close_win(i); gfx(0); rawtty(0); if (!test_mode) { (void)!write(1, "\033[2J\033[H", 7); } exit(code); }
static void on_sig(int s) { (void)s; leave(0); }
static void run_full(const char *cmd) { /* programs that want the whole screen (the graphical browser) */
  gfx(0); rawtty(0); (void)!write(1, "\033[2J\033[H", 7);
  pid_t p = fork(); if (p == 0) { execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
  if (p > 0) waitpid(p, NULL, 0);
  rawtty(1); gfx(1); load_info();
}
static void launch(const char *title, const char *cmd, int full) { if (full) run_full(cmd); else open_term(title, cmd); }
static int win_at(int x, int y) { for (int k = nz - 1; k >= 0; k--) { struct win *w = &wins[zorder[k]]; if (!w->min && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return zorder[k]; } return -1; }
/* maximise toggle: fill the screen above the taskbar, or restore the saved floating geometry */
static void toggle_max(struct win *w) {
  if (w->max) { w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh; w->max = 0; }
  else { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
    w->x = 0; w->y = 0; w->w = W; w->h = H - TBAR; w->max = 1; }
}
static void rclick(void) { /* right button: open context menu on the desktop, clamped to the screen */
  if (win_at(mx, my) >= 0 || my >= H - TBAR) return;
  ctx_open = 1; ctx_x = mx; ctx_y = my;
  int h = NCTX * 30 + 10;
  if (ctx_x + 200 > W) ctx_x = W - 200; if (ctx_y + h > H - TBAR) ctx_y = H - TBAR - h;
}
static void click(void) {
  if (settings_open) {
    int pw = 420, ph = 250, px0 = (W - pw) / 2, py0 = (H - ph) / 2;
    if (mx >= px0 + pw - 24 && mx < px0 + pw - 12 && my >= py0 + 10 && my < py0 + 22) { settings_open = 0; return; } /* close */
    for (int i = 0; i < 5; i++) { int cx = px0 + 24 + i * 56, cy = py0 + 74;
      if (mx >= cx && mx < cx + 44 && my >= cy && my < cy + 32) { theme_accent = i; save_theme(); return; } }
    for (int i = 0; i < 4; i++) { int cx = px0 + 24 + i * 72, cy = py0 + 146;
      if (mx >= cx && mx < cx + 60 && my >= cy && my < cy + 40) { theme_wall = i; save_theme(); return; } }
    if (mx < px0 || mx >= px0 + pw || my < py0 || my >= py0 + ph) settings_open = 0; /* click outside closes */
    return;
  }
  if (ctx_open) {
    int h = NCTX * 30 + 10; (void)h;
    for (int i = 0; i < NCTX; i++) { int y = ctx_y + 5 + i * 30;
      if (mx >= ctx_x + 4 && mx < ctx_x + 196 && my >= y && my < y + 30) { ctx_open = 0;
        if (!strcmp(ctx_items[i].cmd, ":minall")) { for (int j = 0; j < MAXWIN; j++) if (wins[j].used) wins[j].min = 1; return; }
        if (!strcmp(ctx_items[i].cmd, ":demo3d")) { open_app(hs_app_3d()); return; }
        if (!strcmp(ctx_items[i].cmd, ":settings")) { settings_open = 1; return; }
        launch(ctx_items[i].label, ctx_items[i].cmd, 0); return; } }
    ctx_open = 0; return;
  }
  if (start_open) {
    int h = NSTART * 30 + 10, sy = H - TBAR - h - 4;
    for (int i = 0; i < NSTART; i++) { int y = sy + 5 + i * 30;
      if (mx >= 10 && mx < 222 && my >= y && my < y + 30) { start_open = 0;
        if (!strcmp(start_items[i].cmd, ":exit")) leave(0);
        if (!strcmp(start_items[i].cmd, ":install")) { run_full("hs-install"); return; } /* installer owns the whole screen (dialog TUI) */
        launch(start_items[i].label, start_items[i].cmd, 0); return; } }
    start_open = 0; if (!(my >= H - TBAR && mx < 134)) return;
  }
  if (my >= H - TBAR) {
    if (mx >= 6 && mx < 134) { start_open = !start_open; return; }
    int x = 146; for (int i = 0; i < MAXWIN; i++) if (wins[i].used) { if (mx >= x && mx < x + 150) {
      /* taskbar button: restore if minimised; if already focused and visible, minimise; else raise */
      if (wins[i].min) { wins[i].min = 0; raise_win(i); }
      else if (nz && zorder[nz - 1] == i) wins[i].min = 1;
      else raise_win(i);
      return; } x += 156; }
    return;
  }
  int wi = win_at(mx, my);
  if (wi >= 0) {
    raise_win(wi); struct win *w = &wins[wi];
    if (mx >= w->x + w->w - 22 && mx < w->x + w->w - 10 && my >= w->y + 8 && my < w->y + 20) { close_win(wi); return; } /* close (red) */
    if (mx >= w->x + w->w - 38 && mx < w->x + w->w - 26 && my >= w->y + 8 && my < w->y + 20) { w->min = 1; return; } /* minimise (yellow) */
    if (mx >= w->x + w->w - 54 && mx < w->x + w->w - 42 && my >= w->y + 8 && my < w->y + 20) { toggle_max(w); return; }   /* maximise (green) */
    if (my < w->y + 26 && !w->max) { drag = wi; dgx = mx - w->x; dgy = my - w->y; } /* maximised windows don't drag */
    return;
  }
  for (int i = 0; i < NICON; i++) if (mx >= 28 && mx < 92 && my >= 28 + i * 94 && my < 92 + i * 94) { launch(icons[i].label, icons[i].cmd, icons[i].full); return; }
}

static int init_fb(void) {
  fbfd = open("/dev/fb0", O_RDWR); if (fbfd < 0) fbfd = open("/dev/graphics/fb0", O_RDWR);
  if (fbfd < 0) { fprintf(stderr, "hs-gui: no framebuffer (/dev/fb0). Boot in UEFI mode or load a video driver (menu: Graphics and 3D).\n"); return -1; }
  struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
  if (ioctl(fbfd, FBIOGET_VSCREENINFO, &v) || ioctl(fbfd, FBIOGET_FSCREENINFO, &f)) { perror("hs-gui: fb ioctl"); return -1; }
  if (v.bits_per_pixel != 32) { fprintf(stderr, "hs-gui: framebuffer is %u bpp, only 32 bpp is supported\n", v.bits_per_pixel); return -1; }
  W = v.xres; H = v.yres; stride = f.line_length / 4; ro = v.red.offset; go = v.green.offset; bo = v.blue.offset;
  fbsize = (size_t)f.line_length * v.yres_virtual; fb = mmap(0, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
  if (fb == MAP_FAILED) { perror("hs-gui: mmap"); return -1; }
  return 0;
}
static void pump_terms(int wait_ms) { /* read terminal output (used by the main loop and by the test mode) */
  struct pollfd p[MAXWIN]; int idx[MAXWIN], n = 0;
  for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].t) { p[n].fd = wins[i].t->fd; p[n].events = POLLIN; idx[n++] = i; }
  if (!n) return; if (poll(p, n, wait_ms) <= 0) return;
  for (int k = 0; k < n; k++) if (p[k].revents & (POLLIN | POLLHUP | POLLERR)) {
    unsigned char buf[4096]; ssize_t r = read(wins[idx[k]].t->fd, buf, sizeof buf);
    if (r > 0) for (ssize_t j = 0; j < r; j++) t_feed(wins[idx[k]].t, buf[j]);
    else if (r == 0 || (errno != EAGAIN && errno != EINTR)) close_win(idx[k]);
  }
}

int main(int argc, char **argv) {
  int demo = argc > 1 && !strcmp(argv[1], "--demo");
  const char *tt = getenv("HS_GUI_TEST");
#ifdef HS_PREVIEW
  if (tt) sscanf(tt, "%dx%d:%255s", &W, &H, test_path); ro = 16; go = 8; bo = 0;
#else
  if (tt) { test_mode = 1; sscanf(tt, "%dx%d:%255s", &W, &H, test_path); ro = 16; go = 8; bo = 0; }
  else if (init_fb()) return 1;
#endif
  bb = calloc((size_t)W * H, 4); mx = W / 2; my = H / 2; load_theme(); load_info();
  new_win(420, 250, "System", NULL, W > 900 ? W - 470 : 20, 40);
#ifdef HS_PREVIEW
  /* Windows-only design preview: render one frame to a PPM without fork()/pty/fb. Build with -DHS_PREVIEW. Never set in CI. */
  { int cols = W < 900 ? 60 : 80, rows = W < 900 ? 20 : 24;
    Term *t = calloc(1, sizeof *t); t->cols = cols; t->rows = rows; t->top = 0; t->bot = rows - 1; t->fg = 7;
    t->g = calloc((size_t)cols * rows, sizeof(Cell)); t->alt = calloc((size_t)cols * rows, sizeof(Cell)); t_clear(t, 0, 0, cols, rows);
    t->fd = -1; t->pid = -1;
    const char *demo_txt = "HomeServer terminal\n/ # ls\nbin  etc  home  opt  usr\n/ # hs-status\nCPU 4%  RAM 128M  up 0:12\n/ # ";
    for (const char *p = demo_txt; *p; p++) t_feed(t, (unsigned char)*p);
    int wi = new_win(cols * 8 + 4, rows * 16 + 30, "Terminal", t, 90, 30); (void)wi;
    mx = W - 100; my = H - 160;
#ifdef HS_PREVIEW_START
    start_open = 1; mx = 60; my = H - TBAR - 40;
#endif
#ifdef HS_PREVIEW_CTX
    ctx_open = 1; ctx_x = 320; ctx_y = 180; mx = 340; my = 240;
#endif
#ifdef HS_PREVIEW_SETTINGS
    settings_open = 1; mx = W / 2; my = H / 2 - 50;
#endif
#ifdef HS_PREVIEW_3D
    { int ai = open_app(hs_app_3d()); if (ai >= 0) { for (int f = 0; f < 20; f++) wins[ai].frame++; } mx = W - 80; my = H - 120; }
#endif
    draw();
    FILE *o = fopen(test_path[0] ? test_path : "preview.ppm", "wb"); if (!o) return 2;
    fprintf(o, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { u32 c = bb[i]; fputc(c >> 16 & 255, o); fputc(c >> 8 & 255, o); fputc(c & 255, o); }
    fclose(o); return 0; }
#endif
  if (test_mode) {
    open_term("Terminal", "printf '\\033[1;32mHomeServer\\033[0m terminal\\n'; ls --color=never / | head -5; echo; printf '\\033[44m blue \\033[0m ready\\n'; sleep 3");
    open_term("Files", "printf 'second window\\n'; sleep 3");
    for (int i = 0; i < 6; i++) pump_terms(150);
    mx = 700; my = 400; draw(); FILE *o = fopen(test_path, "wb"); if (!o) return 2;
    fprintf(o, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) { u32 c = bb[i]; fputc(c >> 16 & 255, o); fputc(c >> 8 & 255, o); fputc(c & 255, o); }
    fclose(o); leave(0);
  }
  mfd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
  int demo3d = argc > 1 && !strcmp(argv[1], "--demo3d");
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig); signal(SIGHUP, on_sig);
  gfx(1); rawtty(1);
  if (demo) { open_term("Terminal", "sh -l"); }
  if (demo3d) { int wi = open_app(hs_app_3d()); if (wi >= 0) toggle_max(&wins[wi]); } /* 3D fullscreen for the CI screenshot */
  for (int i = 0; i < 4; i++) pump_terms(50);
  draw(); flip();
  if (demo) { printf("HS-GUI-OK %dx%d\n", W, H); fflush(stdout); }
  if (demo3d) { for (int f = 0; f < 30; f++) { for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].app) wins[i].frame++; draw(); flip(); } printf("HS-3D-OK %dx%d\n", W, H); fflush(stdout); }
  for (;;) {
    struct pollfd p[2] = {{0, POLLIN, 0}, {mfd, POLLIN, 0}};
    poll(p, mfd >= 0 ? 2 : 1, 50);
    if (p[0].revents & POLLIN) {
      char k[256]; ssize_t r = read(0, k, sizeof k);
      for (ssize_t j = 0; j < r; j++) {
        if (k[j] == 0x1c) leave(0); /* Ctrl-\ : emergency exit to the console */
        struct win *f = nz ? &wins[zorder[nz - 1]] : NULL;
        if (f && f->t) {
          /* arrows/Home/End: the console sends ESC [ x, applications that switched on "application cursor keys" (ESC [ ? 1 h) expect ESC O x */
          if (k[j] == 27 && j + 2 < r && k[j + 1] == '[' && strchr("ABCDHF", k[j + 2]) && f->t->appkeys) { char a[3] = {27, 'O', k[j + 2]}; (void)!write(f->t->fd, a, 3); j += 2; }
          else (void)!write(f->t->fd, &k[j], 1);
        } else if (k[j] == 'q') leave(0);
      }
    }
    if (mfd >= 0 && (p[1].revents & POLLIN)) {
      signed char m[3];
      while (read(mfd, m, 3) == 3) {
        if (!(m[0] & 8)) { char d; (void)!read(mfd, &d, 1); continue; }
        int dx = (unsigned char)m[1] - ((m[0] << 4) & 0x100), dy = (unsigned char)m[2] - ((m[0] << 3) & 0x100);
        mx += dx; my -= dy; if (mx < 0) mx = 0; if (my < 0) my = 0; if (mx >= W) mx = W - 1; if (my >= H) my = H - 1;
        int nb = m[0] & 1, nbr = (m[0] >> 1) & 1;
        if (drag >= 0 && wins[drag].used) { wins[drag].x = mx - dgx; wins[drag].y = my - dgy; if (wins[drag].y < 0) wins[drag].y = 0; }
        if (nb && !mb) click();
        if (nbr && !mbr) rclick();
        if (!nb) drag = -1;
        mb = nb; mbr = nbr;
      }
    }
    pump_terms(0);
    while (waitpid(-1, NULL, WNOHANG) > 0) ;
    if (time(NULL) - info_t >= 5) load_info();
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].app && wins[i].app->animated && !wins[i].min) wins[i].frame++; /* advance animated apps */
    draw(); flip();
  }
}
