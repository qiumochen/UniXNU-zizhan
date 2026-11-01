// pinalloc: create f2fs pinned (GC-immovable), fully allocated files and print
// their physical extents, so XNU can address them as raw UFS windows while
// Android keeps its /data. Freestanding aarch64 Linux binary (no libc).
//
//   pinalloc create PATH BYTES   new pinned file, fallocated, then mapped
//   pinalloc map PATH            print "pin=N size=S" and "EXT logical physical length flags"
//
// Build: clang --target=aarch64-linux-gnu -static -nostdlib -ffreestanding -fno-stack-protector \
//        -O2 -fuse-ld=lld -o pinalloc pinalloc.c
typedef unsigned long u64;
typedef unsigned int u32;
typedef long i64;

static i64 sys(long n, long a, long b, long c, long d) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
  return x0;
}
enum { SYS_ioctl = 29, SYS_fallocate = 47, SYS_openat = 56, SYS_close = 57, SYS_write = 64,
       SYS_fsync = 82, SYS_exit = 93, SYS_fstat = 80 };
#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_EXCL 0200
#define F2FS_IOC_SET_PIN_FILE 0x4004f50dUL
#define F2FS_IOC_GET_PIN_FILE 0x8004f50eUL
#define FS_IOC_FIEMAP 0xc020660bUL
#define FIEMAP_FLAG_SYNC 1u
#define FIEMAP_EXTENT_LAST 1u

struct fiemap_extent { u64 logical, physical, length, r1, r2; u32 flags, r3, r4, r5; };
struct fiemap { u64 start, length; u32 flags, mapped, count, reserved; struct fiemap_extent ext[256]; };
static struct fiemap fm;

static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sys(SYS_write, 1, (long)s, slen(s), 0); }
static void num(u64 v) {
  char b[24]; int i = 23; b[i] = 0;
  do { b[--i] = '0' + v % 10; v /= 10; } while (v);
  out(b + i);
}
static __attribute__((noreturn)) void die(const char *what, i64 err) {
  out("ERR "); out(what); out(" errno="); num((u64)-err); out("\n");
  sys(SYS_exit, 1, 0, 0, 0);
  __builtin_unreachable();
}
static u64 parse(const char *s) { u64 v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
static int eq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }

static void map(long fd) {
  u32 pin = 0;
  i64 r = sys(SYS_ioctl, fd, F2FS_IOC_GET_PIN_FILE, (long)&pin, 0);
  out("pin="); num(r < 0 ? 0 : pin);
  u64 st[16];
  if (sys(SYS_fstat, fd, (long)st, 0, 0) < 0) die("fstat", -1);
  out(" size="); num(st[6]); out("\n");   // struct stat st_size at byte 48 on aarch64
  u64 pos = 0;
  for (;;) {
    fm.start = pos; fm.length = ~0UL; fm.flags = FIEMAP_FLAG_SYNC; fm.count = 256; fm.mapped = 0;
    r = sys(SYS_ioctl, fd, FS_IOC_FIEMAP, (long)&fm, 0);
    if (r < 0) die("FIEMAP", r);
    if (!fm.mapped) break;
    for (u32 i = 0; i < fm.mapped; i++) {
      struct fiemap_extent *e = &fm.ext[i];
      out("EXT "); num(e->logical); out(" "); num(e->physical); out(" "); num(e->length);
      out(" "); num(e->flags); out("\n");
      pos = e->logical + e->length;
      if (e->flags & FIEMAP_EXTENT_LAST) return;
    }
  }
}

void _start_c(long *sp) {
  long argc = sp[0];
  char **argv = (char **)(sp + 1);
  if (argc == 4 && eq(argv[1], "create")) {
    u64 size = parse(argv[3]);
    long fd = sys(SYS_openat, AT_FDCWD, (long)argv[2], O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) die("open", fd);
    u32 one = 1;
    i64 r = sys(SYS_ioctl, fd, F2FS_IOC_SET_PIN_FILE, (long)&one, 0);
    if (r < 0) die("SET_PIN_FILE", r);
    r = sys(SYS_fallocate, fd, 0, 0, size);
    if (r < 0) die("fallocate", r);
    r = sys(SYS_fsync, fd, 0, 0, 0);
    if (r < 0) die("fsync", r);
    sys(SYS_close, fd, 0, 0, 0);
    fd = sys(SYS_openat, AT_FDCWD, (long)argv[2], O_RDONLY, 0);
    if (fd < 0) die("reopen", fd);
    map(fd);
    sys(SYS_exit, 0, 0, 0, 0);
  }
  if (argc == 3 && eq(argv[1], "map")) {
    long fd = sys(SYS_openat, AT_FDCWD, (long)argv[2], O_RDONLY, 0);
    if (fd < 0) die("open", fd);
    map(fd);
    sys(SYS_exit, 0, 0, 0, 0);
  }
  out("usage: pinalloc create PATH BYTES | pinalloc map PATH\n");
  sys(SYS_exit, 2, 0, 0, 0);
}
__asm__(".globl _start\n_start:\n mov x0, sp\n b _start_c\n");
