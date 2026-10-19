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

/* ---- state ---- */
struct icon { const char *label, *cmd; int r, g, b; char letter[2]; };
static struct icon icons[] = {
  {"Terminal", "sh -l", 40, 160, 90, "T"}, {"Files", "nnn", 230, 160, 40, "F"}, {"Monitor", "btop", 60, 140, 230, "M"},
  {"Menu", "hs-menu", 170, 80, 220, "S"}, {"Browser", "hs-browser", 220, 70, 90, "B"}, {"Apps", "hs-apps", 40, 180, 190, "A"},
};
#define NICON (int)(sizeof icons / sizeof *icons)
static struct { const char *label, *cmd; } start_items[] = {
  {"Management menu", "hs-menu"}, {"Terminal", "sh -l"}, {"Exit to console", ":exit"}, {"Restart", "reboot"}, {"Power off", "poweroff"}};
#define NSTART (int)(sizeof start_items / sizeof *start_items)
static int mx, my, mb, start_open, win_x = 140, win_y = 60, win_w = 420, win_h = 250, drag, dgx, dgy;
static char info[8][96]; static int ninfo; static time_t info_t;

static void load_info(void) {
  ninfo = 0; FILE *p = popen("hs-status plain 2>/dev/null", "r");
  if (p) { while (ninfo < 8 && fgets(info[ninfo], sizeof info[0], p)) { info[ninfo][strcspn(info[ninfo], "\n")] = 0; ninfo++; } pclose(p); }
  if (!ninfo) { snprintf(info[0], sizeof info[0], "HomeServer OS"); ninfo = 1; }
  info_t = time(NULL);
}
static void shadowtext(int x, int y, const char *s, u32 c, int sc) { text(x + 1, y + 1, s, rgb(0, 0, 0), sc); text(x, y, s, c, sc); }

static void draw(void) {
  /* wallpaper: vertical gradient */
  for (int y = 0; y < H; y++) { int t = y * 255 / H; u32 c = rgb(12 + t / 14, 24 + t / 6, 70 + t / 3); for (int x = 0; x < W; x++) bb[y * W + x] = c; }
  shadowtext(W - 8 * 2 * 13 - 24, H - 32 - 56, "HomeServer OS", rgb(220, 235, 255), 2);
  /* icons */
  for (int i = 0; i < NICON; i++) {
    int x = 24, y = 24 + i * 92; int hot = mx >= x && mx < x + 64 && my >= y && my < y + 64;
    rect(x, y, 64, 64, rgb(icons[i].r, icons[i].g, icons[i].b)); frame(x, y, 64, 64, hot ? rgb(255, 255, 255) : rgb(20, 20, 40));
    text(x + 16, y + 16, icons[i].letter, rgb(255, 255, 255), 2);
    shadowtext(x + 32 - tw(icons[i].label) / 2, y + 68, icons[i].label, rgb(255, 255, 255), 1);
  }
  /* window */
  rect(win_x, win_y, win_w, win_h, rgb(236, 240, 246)); frame(win_x, win_y, win_w, win_h, rgb(20, 30, 60));
  rect(win_x, win_y, win_w, 26, rgb(30, 60, 130)); text(win_x + 10, win_y + 5, "System", rgb(255, 255, 255), 1);
  rect(win_x + win_w - 22, win_y + 5, 16, 16, rgb(200, 60, 60)); text(win_x + win_w - 22, win_y + 5, "x", rgb(255, 255, 255), 1);
  for (int i = 0; i < ninfo; i++) text(win_x + 16, win_y + 42 + i * 22, info[i], rgb(20, 30, 50), 1);
  text(win_x + 16, win_y + win_h - 30, "Click an icon to open a program.", rgb(90, 100, 120), 1);
  /* taskbar */
  rect(0, H - 32, W, 32, rgb(18, 22, 40)); rect(0, H - 32, W, 1, rgb(80, 110, 190));
  rect(4, H - 28, 120, 24, start_open ? rgb(70, 110, 220) : rgb(40, 70, 160)); text(16, H - 24, "HomeServer", rgb(255, 255, 255), 1);
  time_t t = time(NULL); struct tm *tm = localtime(&t); char clk[32]; strftime(clk, sizeof clk, "%H:%M  %d.%m.%Y", tm);
  text(W - tw(clk) - 12, H - 24, clk, rgb(230, 235, 255), 1);
  if (start_open) {
    int h = NSTART * 28 + 8; rect(4, H - 32 - h, 200, h, rgb(24, 30, 56)); frame(4, H - 32 - h, 200, h, rgb(80, 110, 190));
    for (int i = 0; i < NSTART; i++) { int y = H - 32 - h + 4 + i * 28; int hot = mx >= 4 && mx < 204 && my >= y && my < y + 28;
      if (hot) rect(6, y, 196, 28, rgb(60, 100, 210)); text(16, y + 6, start_items[i].label, rgb(255, 255, 255), 1); }
  }
  /* cursor: arrow */
  static const char *cur[] = {"X","XX","X.X","X..X","X...X","X....X","X.....X","X......X","X.......X","X........X","X.....XXXXX","X..X..X","X.X X..X","XX  X..X","X    X..X","     X..X","      XX"};
  for (int j = 0; j < 17; j++) for (int i = 0; cur[j][i]; i++) { char c = cur[j][i]; if (c == 'X') px(mx + i, my + j, rgb(0, 0, 0)); else if (c == '.') px(mx + i, my + j, rgb(255, 255, 255)); }
  for (int j = 0; j < 17; j++) for (int i = 0; cur[j][i]; i++) if (cur[j][i] == 'X') px(mx + i, my + j, rgb(0, 0, 0));
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
  if (on) { if (tcgetattr(0, &oldt) == 0) { have_oldt = 1; struct termios t = oldt; t.c_lflag &= ~(ICANON | ECHO); t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &t); } }
  else if (have_oldt) tcsetattr(0, TCSANOW, &oldt);
}
static void leave(int code) { gfx(0); rawtty(0); if (!test_mode) { (void)!write(1, "\033[2J\033[H", 7); } exit(code); }
static void on_sig(int s) { (void)s; leave(0); }
static void run(const char *cmd) {
  gfx(0); rawtty(0); (void)!write(1, "\033[2J\033[H", 7);
  pid_t p = fork(); if (p == 0) { execl("/bin/sh", "sh", "-c", cmd, (char *)0); _exit(127); }
  if (p > 0) waitpid(p, NULL, 0);
  rawtty(1); gfx(1); load_info();
}
static void click(void) {
  if (start_open) {
    int h = NSTART * 28 + 8;
    for (int i = 0; i < NSTART; i++) { int y = H - 32 - h + 4 + i * 28;
      if (mx >= 4 && mx < 204 && my >= y && my < y + 28) { start_open = 0; if (!strcmp(start_items[i].cmd, ":exit")) leave(0); run(start_items[i].cmd); return; } }
    start_open = 0; if (!(my >= H - 32 && mx < 124)) return;
  }
  if (my >= H - 32 && mx >= 4 && mx < 124) { start_open = !start_open; return; }
  for (int i = 0; i < NICON; i++) if (mx >= 24 && mx < 88 && my >= 24 + i * 92 && my < 88 + i * 92) { run(icons[i].cmd); return; }
  if (mx >= win_x + win_w - 22 && mx < win_x + win_w - 6 && my >= win_y + 5 && my < win_y + 21) { win_x = -10000; return; }
  if (mx >= win_x && mx < win_x + win_w && my >= win_y && my < win_y + 26) { drag = 1; dgx = mx - win_x; dgy = my - win_y; }
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

int main(int argc, char **argv) {
  int demo = argc > 1 && !strcmp(argv[1], "--demo"); /* draw one frame, print the marker, stay up (used by the CI test) */
  const char *t = getenv("HS_GUI_TEST");
  if (t) { test_mode = 1; sscanf(t, "%dx%d:%255s", &W, &H, test_path); ro = 16; go = 8; bo = 0; }
  else if (init_fb()) return 1;
  bb = calloc((size_t)W * H, 4); mx = W / 2; my = H / 2; load_info();
  if (test_mode) {
    mx = 330; my = 150; draw(); FILE *o = fopen(test_path, "wb"); if (!o) return 2;
    fprintf(o, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) { u32 c = bb[i]; fputc(c >> 16 & 255, o); fputc(c >> 8 & 255, o); fputc(c & 255, o); }
    fclose(o); return 0;
  }
  mfd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig); signal(SIGHUP, on_sig);
  gfx(1); rawtty(1); draw(); flip();
  if (demo) { printf("HS-GUI-OK %dx%d\n", W, H); fflush(stdout); }
  for (;;) {
    struct pollfd p[2] = {{0, POLLIN, 0}, {mfd, POLLIN, 0}};
    int r = poll(p, mfd >= 0 ? 2 : 1, 1000); int redraw = 1; (void)r;
    if (p[0].revents & POLLIN) { char k; while (read(0, &k, 1) == 1) if (k == 'q' || k == 27) leave(0); }
    if (mfd >= 0 && (p[1].revents & POLLIN)) {
      signed char m[3];
      while (read(mfd, m, 3) == 3) {
        if (!(m[0] & 8)) { char d; (void)!read(mfd, &d, 1); continue; } /* resync */
        int dx = (unsigned char)m[1] - ((m[0] << 4) & 0x100), dy = (unsigned char)m[2] - ((m[0] << 3) & 0x100);
        mx += dx; my -= dy; if (mx < 0) mx = 0; if (my < 0) my = 0; if (mx >= W) mx = W - 1; if (my >= H) my = H - 1;
        int nb = m[0] & 1;
        if (drag) { win_x = mx - dgx; win_y = my - dgy; if (!nb) drag = 0; }
        if (nb && !mb) click();
        if (!nb) drag = 0;
        mb = nb;
      }
    }
    if (time(NULL) - info_t >= 5) load_info();
    if (redraw) { draw(); flip(); }
  }
}
