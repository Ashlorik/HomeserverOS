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

typedef unsigned int u32;
static int W = 800, H = 600, fbfd = -1, mfd = -1, tty_gfx = 0, test_mode = 0;
static u32 *fb, *bb; static size_t fbsize; static int stride; /* in pixels */
static int ro, go, bo; /* colour offsets */
static struct termios oldt; static int have_oldt;
static char test_path[256];

static u32 rgb(int r, int g, int b) { return ((u32)r << ro) | ((u32)g << go) | ((u32)b << bo); }
static void px(int x, int y, u32 c) { if (x >= 0 && y >= 0 && x < W && y < H) bb[y * W + x] = c; }
static void rect(int x, int y, int w, int h, u32 c) {
  for (int j = y < 0 ? 0 : y; j < y + h && j < H; j++) for (int i = x < 0 ? 0 : x; i < x + w && i < W; i++) bb[j * W + i] = c;
}
static void frame(int x, int y, int w, int h, u32 c) { rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c); rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c); }
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
  int cols, rows, cx, cy, sx, sy, top, bot, fd, inalt, st, np, priv, line0, wrap, appcur;
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
  if (t->priv) { /* private modes: alternate screen */
    if (f == 'h' || f == 'l') for (int i = 0; i < t->np; i++) if (t->par[i] == 1) t->appcur = f == 'h';
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

/* ---- windows ---- */
#define MAXWIN 8
struct win { int used, x, y, w, h; char title[40]; Term *t; };
static struct win wins[MAXWIN]; static int zorder[MAXWIN], nz; /* zorder[nz-1] is the focused window */
static void raise_win(int i) { int k; for (k = 0; k < nz && zorder[k] != i; k++); if (k == nz) return; memmove(zorder + k, zorder + k + 1, (nz - k - 1) * sizeof(int)); zorder[nz - 1] = i; }
static int new_win(int w, int h, const char *title, Term *t, int x, int y) {
  for (int i = 0; i < MAXWIN; i++) if (!wins[i].used) { wins[i] = (struct win){1, x, y, w, h, "", t}; snprintf(wins[i].title, sizeof wins[i].title, "%s", title); zorder[nz++] = i; return i; }
  return -1;
}
static void close_win(int i) { if (wins[i].t) t_free(wins[i].t); wins[i].used = 0; int k; for (k = 0; k < nz && zorder[k] != i; k++); if (k < nz) { memmove(zorder + k, zorder + k + 1, (nz - k - 1) * sizeof(int)); nz--; } }
static int open_term(const char *title, const char *cmd) {
  static int n; int cols = 80, rows = 24; if (W < 900) { cols = 60; rows = 20; }
  Term *t = t_new(cols, rows, cmd); if (!t) return -1;
  int off = (n++ % 6) * 28; return new_win(cols * 8 + 4, rows * 16 + 30, title, t, 90 + off, 30 + off);
}

/* ---- state ---- */
struct icon { const char *label, *cmd; int r, g, b; char letter[2]; int full; };
static struct icon icons[] = {
  {"Terminal", "sh -l", 40, 160, 90, "T", 0}, {"Files", "nnn", 230, 160, 40, "F", 0}, {"Monitor", "btop", 60, 140, 230, "M", 0},
  {"Menu", "hs-menu", 170, 80, 220, "S", 0}, {"Browser", "hs-web", 220, 70, 90, "B", 1}, {"Apps", "hs-apps", 40, 180, 190, "A", 0},
};
#define NICON (int)(sizeof icons / sizeof *icons)
static struct { const char *label, *cmd; } start_items[] = {
  {"New terminal", "sh -l"}, {"Management menu", "hs-menu"}, {"Exit to console", ":exit"}, {"Restart", "reboot"}, {"Power off", "poweroff"}};
#define NSTART (int)(sizeof start_items / sizeof *start_items)
static int mx, my, mb, start_open, drag = -1, dgx, dgy;
static char info[8][96]; static int ninfo; static time_t info_t;

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
static void draw(void) {
  for (int y = 0; y < H; y++) { int t = y * 255 / H; u32 c = rgb(12 + t / 14, 24 + t / 6, 70 + t / 3); for (int x = 0; x < W; x++) bb[y * W + x] = c; }
  shadowtext(W - 8 * 2 * 13 - 24, H - 32 - 56, "HomeServer OS", rgb(220, 235, 255), 2);
  for (int i = 0; i < NICON; i++) {
    int x = 24, y = 24 + i * 92; int hot = mx >= x && mx < x + 64 && my >= y && my < y + 64;
    rect(x, y, 64, 64, rgb(icons[i].r, icons[i].g, icons[i].b)); frame(x, y, 64, 64, hot ? rgb(255, 255, 255) : rgb(20, 20, 40));
    text(x + 16, y + 16, icons[i].letter, rgb(255, 255, 255), 2);
    shadowtext(x + 32 - tw(icons[i].label) / 2, y + 68, icons[i].label, rgb(255, 255, 255), 1);
  }
  for (int k = 0; k < nz; k++) {
    struct win *w = &wins[zorder[k]]; int foc = k == nz - 1;
    rect(w->x, w->y, w->w, w->h, w->t ? rgb(0, 0, 0) : rgb(236, 240, 246)); frame(w->x, w->y, w->w, w->h, rgb(20, 30, 60));
    rect(w->x, w->y, w->w, 26, foc ? rgb(30, 80, 170) : rgb(60, 70, 100)); text(w->x + 10, w->y + 5, w->title, rgb(255, 255, 255), 1);
    rect(w->x + w->w - 22, w->y + 5, 16, 16, rgb(200, 60, 60)); text(w->x + w->w - 22, w->y + 5, "x", rgb(255, 255, 255), 1);
    if (w->t) draw_term(w, foc);
    else { for (int i = 0; i < ninfo; i++) text(w->x + 16, w->y + 42 + i * 22, info[i], rgb(20, 30, 50), 1);
      text(w->x + 16, w->y + w->h - 30, "Click an icon to open a program.", rgb(90, 100, 120), 1); }
  }
  rect(0, H - 32, W, 32, rgb(18, 22, 40)); rect(0, H - 32, W, 1, rgb(80, 110, 190));
  rect(4, H - 28, 120, 24, start_open ? rgb(70, 110, 220) : rgb(40, 70, 160)); text(16, H - 24, "HomeServer", rgb(255, 255, 255), 1);
  { int x = 132; for (int i = 0; i < MAXWIN; i++) if (wins[i].used) { int foc = nz && zorder[nz - 1] == i;
      rect(x, H - 28, 130, 24, foc ? rgb(60, 100, 200) : rgb(36, 44, 80)); char b[18]; snprintf(b, sizeof b, "%.15s", wins[i].title); text(x + 8, H - 24, b, rgb(255, 255, 255), 1); x += 134; } }
  time_t t = time(NULL); struct tm *tm = localtime(&t); char clk[32]; strftime(clk, sizeof clk, "%H:%M  %d.%m.%Y", tm);
  text(W - tw(clk) - 12, H - 24, clk, rgb(230, 235, 255), 1);
  if (start_open) {
    int h = NSTART * 28 + 8; rect(4, H - 32 - h, 200, h, rgb(24, 30, 56)); frame(4, H - 32 - h, 200, h, rgb(80, 110, 190));
    for (int i = 0; i < NSTART; i++) { int y = H - 32 - h + 4 + i * 28; int hot = mx >= 4 && mx < 204 && my >= y && my < y + 28;
      if (hot) rect(6, y, 196, 28, rgb(60, 100, 210));
      text(16, y + 6, start_items[i].label, rgb(255, 255, 255), 1); }
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
static int win_at(int x, int y) { for (int k = nz - 1; k >= 0; k--) { struct win *w = &wins[zorder[k]]; if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return zorder[k]; } return -1; }
static void click(void) {
  if (start_open) {
    int h = NSTART * 28 + 8;
    for (int i = 0; i < NSTART; i++) { int y = H - 32 - h + 4 + i * 28;
      if (mx >= 4 && mx < 204 && my >= y && my < y + 28) { start_open = 0; if (!strcmp(start_items[i].cmd, ":exit")) leave(0); launch(start_items[i].label, start_items[i].cmd, 0); return; } }
    start_open = 0; if (!(my >= H - 32 && mx < 124)) return;
  }
  if (my >= H - 32) {
    if (mx >= 4 && mx < 124) { start_open = !start_open; return; }
    int x = 132; for (int i = 0; i < MAXWIN; i++) if (wins[i].used) { if (mx >= x && mx < x + 130) { raise_win(i); return; } x += 134; }
    return;
  }
  int wi = win_at(mx, my);
  if (wi >= 0) {
    raise_win(wi); struct win *w = &wins[wi];
    if (mx >= w->x + w->w - 22 && mx < w->x + w->w - 6 && my >= w->y + 5 && my < w->y + 21) { close_win(wi); return; }
    if (my < w->y + 26) { drag = wi; dgx = mx - w->x; dgy = my - w->y; }
    return;
  }
  for (int i = 0; i < NICON; i++) if (mx >= 24 && mx < 88 && my >= 24 + i * 92 && my < 88 + i * 92) { launch(icons[i].label, icons[i].cmd, icons[i].full); return; }
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
  if (tt) { test_mode = 1; sscanf(tt, "%dx%d:%255s", &W, &H, test_path); ro = 16; go = 8; bo = 0; }
  else if (init_fb()) return 1;
  bb = calloc((size_t)W * H, 4); mx = W / 2; my = H / 2; load_info();
  new_win(420, 250, "System", NULL, W > 900 ? W - 470 : 20, 40);
  if (test_mode) {
    open_term("Terminal", "printf '\\033[1;32mHomeServer\\033[0m terminal\\n'; ls --color=never / | head -5; echo; printf '\\033[44m blue \\033[0m ready\\n'; sleep 3");
    open_term("Files", "printf 'second window\\n'; sleep 3");
    for (int i = 0; i < 6; i++) pump_terms(150);
    mx = 700; my = 400; draw(); FILE *o = fopen(test_path, "wb"); if (!o) return 2;
    fprintf(o, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) { u32 c = bb[i]; fputc(c >> 16 & 255, o); fputc(c >> 8 & 255, o); fputc(c & 255, o); }
    fclose(o); leave(0);
  }
  mfd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig); signal(SIGHUP, on_sig);
  gfx(1); rawtty(1);
  if (demo) { open_term("Terminal", "sh -l"); }
  for (int i = 0; i < 4; i++) pump_terms(50);
  draw(); flip();
  if (demo) { printf("HS-GUI-OK %dx%d\n", W, H); fflush(stdout); }
  for (;;) {
    struct pollfd p[2] = {{0, POLLIN, 0}, {mfd, POLLIN, 0}};
    poll(p, mfd >= 0 ? 2 : 1, 50);
    if (p[0].revents & POLLIN) {
      char k[256]; ssize_t r = read(0, k, sizeof k);
      for (ssize_t j = 0; j < r; j++) {
        if (k[j] == 0x1c) leave(0); /* Ctrl-\ : emergency exit to the console */
        struct win *f = nz ? &wins[zorder[nz - 1]] : NULL;
        if (f && f->t) {
          /* the Linux console sends ESC [ A for the arrows; terminals in application mode (ncurses: smkx) expect ESC O A */
          if (k[j] == 27 && j + 2 < r && k[j + 1] == '[') {
            char c = k[j + 2];
            if (c >= 'A' && c <= 'D' && f->t->appcur) { char o[3] = {27, 'O', c}; (void)!write(f->t->fd, o, 3); j += 2; continue; }
            if ((c == 'H' || c == 'F') && f->t->appcur) { char o[3] = {27, 'O', c}; (void)!write(f->t->fd, o, 3); j += 2; continue; }
            if ((c == '1' || c == '4') && j + 3 < r && k[j + 3] == '~') { char o[3] = {27, 'O', c == '1' ? 'H' : 'F'}; (void)!write(f->t->fd, o, 3); j += 3; continue; }
          }
          (void)!write(f->t->fd, &k[j], 1);
        } else if (k[j] == 'q' || k[j] == 27) leave(0);
      }
    }
    if (mfd >= 0 && (p[1].revents & POLLIN)) {
      signed char m[3];
      while (read(mfd, m, 3) == 3) {
        if (!(m[0] & 8)) { char d; (void)!read(mfd, &d, 1); continue; }
        int dx = (unsigned char)m[1] - ((m[0] << 4) & 0x100), dy = (unsigned char)m[2] - ((m[0] << 3) & 0x100);
        mx += dx; my -= dy; if (mx < 0) mx = 0; if (my < 0) my = 0; if (mx >= W) mx = W - 1; if (my >= H) my = H - 1;
        int nb = m[0] & 1;
        if (drag >= 0 && wins[drag].used) { wins[drag].x = mx - dgx; wins[drag].y = my - dgy; if (wins[drag].y < 0) wins[drag].y = 0; }
        if (nb && !mb) click();
        if (!nb) drag = -1;
        mb = nb;
      }
    }
    pump_terms(0);
    while (waitpid(-1, NULL, WNOHANG) > 0) ;
    if (time(NULL) - info_t >= 5) load_info();
    draw(); flip();
  }
}
