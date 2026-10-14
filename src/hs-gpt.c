/* hs-gpt: tiny GPT writer/reader for HomeServer OS.
 * Creates a disk layout of [1 MiB gap][EFI System Partition][Linux root = rest of the disk],
 * works on block devices (installer) and plain files (image builder).
 *
 *   hs-gpt create DEV --esp-mib N [--root-guid G] [--esp-guid G] [--disk-guid G] [--sector-size N]
 *   hs-gpt show   DEV      one line per partition: N START_BYTES SIZE_BYTES TYPE NAME UNIQUE_GUID
 *   hs-gpt reread DEV      ask the kernel to re-read the partition table
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIB (1024ULL * 1024ULL)
#define NENT 128
#define ENTSZ 128

typedef struct { uint8_t b[16]; } guid_t;

struct __attribute__((packed)) gpt_hdr {
  char sig[8]; uint32_t rev, size, crc, rsv;
  uint64_t my_lba, alt_lba, first, last;
  uint8_t disk[16];
  uint64_t ent_lba; uint32_t nent, entsz, ent_crc;
};
struct __attribute__((packed)) gpt_ent {
  uint8_t type[16], uniq[16]; uint64_t first, last, attr; uint16_t name[36];
};

static uint32_t crc_tab[256];
static void crc_init(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crc_tab[i] = c;
  }
}
static uint32_t crc32(const void *buf, size_t n) {
  uint32_t c = 0xFFFFFFFFu; const uint8_t *p = buf;
  while (n--) c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

static int guid_parse(const char *s, guid_t *g) {
  unsigned a, b, c, d; unsigned long long e;
  if (strlen(s) != 36 || sscanf(s, "%8x-%4x-%4x-%4x-%12llx", &a, &b, &c, &d, &e) != 5) return -1;
  g->b[0] = a; g->b[1] = a >> 8; g->b[2] = a >> 16; g->b[3] = a >> 24;
  g->b[4] = b; g->b[5] = b >> 8; g->b[6] = c; g->b[7] = c >> 8;
  g->b[8] = d >> 8; g->b[9] = d;
  for (int i = 0; i < 6; i++) g->b[10 + i] = (e >> (8 * (5 - i))) & 0xFF;
  return 0;
}
static void guid_fmt(const uint8_t *b, char *o) {
  snprintf(o, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[3], b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
static void guid_random(guid_t *g) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0 || read(fd, g->b, 16) != 16) { perror("urandom"); exit(1); }
  close(fd);
  g->b[7] = (g->b[7] & 0x0F) | 0x40; g->b[8] = (g->b[8] & 0x3F) | 0x80; /* RFC 4122 v4 */
}

static const guid_t ESP_TYPE = {{0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b}};
static const guid_t LINUX_TYPE = {{0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47, 0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4}};

static int dev_info(int fd, uint64_t *bytes, uint32_t *ssz, int *isblk) {
  struct stat st;
  if (fstat(fd, &st) < 0) return -1;
  if (S_ISBLK(st.st_mode)) {
    uint64_t sz; int s = 512;
    if (ioctl(fd, BLKGETSIZE64, &sz) < 0) return -1;
    if (ioctl(fd, BLKSSZGET, &s) < 0) s = 512;
    *bytes = sz; *ssz = s; *isblk = 1;
  } else { *bytes = st.st_size; *ssz = 512; *isblk = 0; }
  return 0;
}
static int wr(int fd, uint64_t lba, const void *buf, size_t len, uint32_t ssz) {
  const uint8_t *p = buf; uint64_t off = lba * ssz;
  while (len) { ssize_t n = pwrite(fd, p, len, off); if (n <= 0) return -1; p += n; off += n; len -= n; }
  return 0;
}
static int rd(int fd, uint64_t lba, void *buf, size_t len, uint32_t ssz) {
  uint8_t *p = buf; uint64_t off = lba * ssz;
  while (len) { ssize_t n = pread(fd, p, len, off); if (n <= 0) return -1; p += n; off += n; len -= n; }
  return 0;
}
static void set_name(struct gpt_ent *e, const char *s) {
  for (int i = 0; i < 36 && s[i]; i++) e->name[i] = (uint8_t)s[i];
}

static int reread(int fd) {
  for (int i = 0; i < 10; i++) {
    if (ioctl(fd, BLKRRPART) == 0) return 0;
    if (errno != EBUSY) break;
    usleep(300000);
  }
  return -1;
}

static int cmd_create(const char *dev, uint64_t esp_mib, guid_t *rootg, guid_t *espg, guid_t *diskg, uint32_t force_ssz) {
  int fd = open(dev, O_RDWR | O_EXCL);
  if (fd < 0) { fprintf(stderr, "hs-gpt: cannot open %s: %s\n", dev, strerror(errno)); return 1; }
  uint64_t bytes; uint32_t ssz; int isblk;
  if (dev_info(fd, &bytes, &ssz, &isblk) < 0) { perror("size"); return 1; }
  if (force_ssz && !isblk) ssz = force_ssz;
  uint64_t total = bytes / ssz, align = MIB / ssz, ent_sec = (NENT * ENTSZ + ssz - 1) / ssz;
  uint64_t first = align, last = total - ent_sec - 2;
  uint64_t esp_s = first, esp_e = esp_s + esp_mib * MIB / ssz - 1;
  uint64_t root_s = (esp_e + 1 + align - 1) / align * align;
  uint64_t root_e = (last + 1) / align * align - 1;
  if (total < 4096 || root_e < root_s + 64 * MIB / ssz) { fprintf(stderr, "hs-gpt: disk too small\n"); return 1; }

  uint8_t *ents = calloc(1, ent_sec * ssz), *sec = calloc(1, ssz);
  struct gpt_ent *e = (struct gpt_ent *)ents;
  memcpy(e[0].type, ESP_TYPE.b, 16); memcpy(e[0].uniq, espg->b, 16); e[0].first = esp_s; e[0].last = esp_e; set_name(&e[0], "EFI System");
  memcpy(e[1].type, LINUX_TYPE.b, 16); memcpy(e[1].uniq, rootg->b, 16); e[1].first = root_s; e[1].last = root_e; set_name(&e[1], "HomeServer root");
  uint32_t ecrc = crc32(ents, NENT * ENTSZ);

  struct gpt_hdr h; memset(&h, 0, sizeof h);
  memcpy(h.sig, "EFI PART", 8); h.rev = 0x00010000; h.size = 92;
  h.first = first; h.last = last; memcpy(h.disk, diskg->b, 16); h.nent = NENT; h.entsz = ENTSZ; h.ent_crc = ecrc;

  /* protective MBR */
  uint8_t *mbr = calloc(1, ssz);
  mbr[446 + 2] = 2; mbr[446 + 4] = 0xEE; mbr[446 + 5] = mbr[446 + 6] = mbr[446 + 7] = 0xFF;
  uint32_t one = 1, mbrsz = (total - 1 > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)(total - 1);
  memcpy(mbr + 446 + 8, &one, 4); memcpy(mbr + 446 + 12, &mbrsz, 4); mbr[510] = 0x55; mbr[511] = 0xAA;

  /* wipe the area up to the first partition's start, then write primary and backup structures */
  uint8_t *zero = calloc(1, MIB);
  for (uint64_t s = 0; s < first; s += align) if (wr(fd, s, zero, align * ssz > MIB ? MIB : align * ssz, ssz) < 0) { perror("wipe"); return 1; }
  if (wr(fd, 0, mbr, ssz, ssz) < 0) { perror("mbr"); return 1; }

  h.my_lba = 1; h.alt_lba = total - 1; h.ent_lba = 2; h.crc = 0; h.crc = crc32(&h, 92);
  memcpy(sec, &h, 92);
  if (wr(fd, 2, ents, ent_sec * ssz, ssz) < 0 || wr(fd, 1, sec, ssz, ssz) < 0) { perror("primary"); return 1; }

  h.my_lba = total - 1; h.alt_lba = 1; h.ent_lba = total - 1 - ent_sec; h.crc = 0; h.crc = crc32(&h, 92);
  memset(sec, 0, ssz); memcpy(sec, &h, 92);
  if (wr(fd, h.ent_lba, ents, ent_sec * ssz, ssz) < 0 || wr(fd, total - 1, sec, ssz, ssz) < 0) { perror("backup"); return 1; }
  fsync(fd);
  if (isblk && reread(fd) < 0) fprintf(stderr, "hs-gpt: warning: kernel did not re-read the partition table (%s)\n", strerror(errno));
  close(fd);
  return 0;
}

static int cmd_show(const char *dev) {
  int fd = open(dev, O_RDONLY);
  if (fd < 0) { fprintf(stderr, "hs-gpt: cannot open %s: %s\n", dev, strerror(errno)); return 1; }
  uint64_t bytes; uint32_t ssz; int isblk;
  if (dev_info(fd, &bytes, &ssz, &isblk) < 0) { perror("size"); return 1; }
  uint8_t *sec = malloc(ssz);
  if (rd(fd, 1, sec, ssz, ssz) < 0) { fprintf(stderr, "hs-gpt: no GPT header\n"); return 2; }
  struct gpt_hdr h; memcpy(&h, sec, sizeof h);
  if (memcmp(h.sig, "EFI PART", 8) != 0 || h.size < 92 || h.size > ssz) { fprintf(stderr, "hs-gpt: no GPT found\n"); return 2; }
  uint32_t want = h.crc; h.crc = 0;
  if (crc32(&h, h.size) != want) { fprintf(stderr, "hs-gpt: GPT header checksum mismatch\n"); return 2; }
  if (h.entsz < 128 || h.nent == 0 || h.nent > 1024) { fprintf(stderr, "hs-gpt: bad entry table\n"); return 2; }
  size_t tl = (size_t)h.nent * h.entsz; uint8_t *tab = malloc((tl + ssz - 1) / ssz * ssz);
  if (rd(fd, h.ent_lba, tab, (tl + ssz - 1) / ssz * ssz, ssz) < 0) { fprintf(stderr, "hs-gpt: cannot read entries\n"); return 2; }
  if (crc32(tab, tl) != h.ent_crc) { fprintf(stderr, "hs-gpt: GPT entries checksum mismatch\n"); return 2; }
  int n = 0;
  for (uint32_t i = 0; i < h.nent; i++) {
    struct gpt_ent *e = (struct gpt_ent *)(tab + (size_t)i * h.entsz);
    static const uint8_t z[16];
    if (!memcmp(e->type, z, 16)) continue;
    char tg[37], ug[37], nm[37]; guid_fmt(e->type, tg); guid_fmt(e->uniq, ug);
    for (int k = 0; k < 36; k++) { nm[k] = e->name[k] && e->name[k] < 127 ? (char)e->name[k] : (e->name[k] ? '?' : 0); }
    nm[36] = 0;
    for (char *p = nm; *p; p++) if (*p == ' ') *p = '_';
    printf("%u %llu %llu %s %s %s\n", i + 1, (unsigned long long)(e->first * ssz), (unsigned long long)((e->last - e->first + 1) * ssz), tg, nm[0] ? nm : "-", ug);
    n++;
  }
  return n ? 0 : 3;
}

int main(int argc, char **argv) {
  crc_init();
  if (argc >= 3 && !strcmp(argv[1], "show")) return cmd_show(argv[2]);
  if (argc >= 3 && !strcmp(argv[1], "reread")) {
    int fd = open(argv[2], O_RDONLY);
    if (fd < 0 || reread(fd) < 0) { fprintf(stderr, "hs-gpt: reread failed: %s\n", strerror(errno)); return 1; }
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "create")) {
    uint64_t esp = 256; uint32_t ssz = 0; guid_t rg, eg, dg; guid_random(&rg); guid_random(&eg); guid_random(&dg);
    for (int i = 3; i + 1 < argc; i += 2) {
      if (!strcmp(argv[i], "--esp-mib")) esp = strtoull(argv[i + 1], 0, 10);
      else if (!strcmp(argv[i], "--sector-size")) ssz = atoi(argv[i + 1]);
      else if (!strcmp(argv[i], "--root-guid")) { if (guid_parse(argv[i + 1], &rg)) { fprintf(stderr, "bad GUID\n"); return 1; } }
      else if (!strcmp(argv[i], "--esp-guid")) { if (guid_parse(argv[i + 1], &eg)) { fprintf(stderr, "bad GUID\n"); return 1; } }
      else if (!strcmp(argv[i], "--disk-guid")) { if (guid_parse(argv[i + 1], &dg)) { fprintf(stderr, "bad GUID\n"); return 1; } }
      else { fprintf(stderr, "unknown option %s\n", argv[i]); return 1; }
    }
    if (esp < 16 || esp > 4096) { fprintf(stderr, "--esp-mib must be 16..4096\n"); return 1; }
    return cmd_create(argv[2], esp, &rg, &eg, &dg, ssz);
  }
  fprintf(stderr, "usage: hs-gpt create DEV --esp-mib N [--root-guid G]... | show DEV | reread DEV\n");
  return 1;
}
