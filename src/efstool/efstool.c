/*
 * efstool -- read/write files in the Qualcomm modem's EFS from the phone itself (root).
 *
 * There is no /dev/diag on this kernel; DIAG belongs to the vendor diag-router daemon. Its
 * stock client /vendor/bin/diag_socket_log bridges DIAG <-> a TCP connection it opens to
 * -a <ip> -p <port> (meant for remote QXDM). efstool listens on 127.0.0.1, starts
 * diag_socket_log against itself, and speaks the EFS2 subsystem (DIAG 0x4B / subsys 0x13)
 * over the HDLC-framed stream -- the same packets QCSuper's --efs-shell sends over USB.
 *
 *   efstool stat <efs_path>                 prints "size=<n> mode=<octal>"; exit 2 if missing
 *   efstool get  <efs_path> <local_file>
 *   efstool put  <local_file> <efs_path>    O_WRONLY|O_CREAT|O_TRUNC, keeps an existing file's mode
 *
 * Options: -p <port> (default 2577)  -d <diag_socket_log path>  -v (log frames to stderr)
 * Exit: 0 ok, 1 error, 2 not found.
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DIAG_SUBSYS_CMD_F 0x4B
#define SUBSYS_FS 0x13
enum { EFS_HELLO = 0, EFS_OPEN = 2, EFS_CLOSE = 3, EFS_READ = 4, EFS_WRITE = 5, EFS_STAT = 15 };
#define EFS_ENOENT 2
#define CHUNK 1024

static int sock = -1, verbose;
static pid_t child = -1;
static unsigned char rbuf[65536];
static size_t rlen;

static void die(const char *m) { fprintf(stderr, "efstool: %s\n", m); exit(1); }

static uint16_t crc16(const unsigned char *b, size_t n) {   /* CRC-16/X-25, as DIAG uses */
  uint16_t c = 0xFFFF;
  while (n--) { c ^= *b++; for (int i = 0; i < 8; i++) c = (c & 1) ? (c >> 1) ^ 0x8408 : c >> 1; }
  return c ^ 0xFFFF;
}
static void put16(unsigned char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void send_frame(const unsigned char *pkt, size_t n) {
  static unsigned char out[2 * 8192 + 8];
  size_t o = 0;
  unsigned char tmp[8192 + 2];
  if (n > 8192) die("packet too large");
  memcpy(tmp, pkt, n);
  put16(tmp + n, crc16(pkt, n));
  for (size_t i = 0; i < n + 2; i++) {
    if (tmp[i] == 0x7d || tmp[i] == 0x7e) { out[o++] = 0x7d; out[o++] = tmp[i] ^ 0x20; }
    else out[o++] = tmp[i];
  }
  out[o++] = 0x7e;
  for (size_t w = 0; w < o;) {
    ssize_t r = write(sock, out + w, o - w);
    if (r <= 0) { if (errno == EINTR) continue; die("write to diag failed"); }
    w += r;
  }
}

/* Next valid frame (unescaped, CRC stripped) into f; returns its length, or -1 on timeout. */
static int recv_frame(unsigned char *f, size_t cap, int timeout_ms) {
  for (;;) {
    unsigned char *end = memchr(rbuf, 0x7e, rlen);
    if (end) {
      size_t raw = end - rbuf, n = 0;
      for (size_t i = 0; i < raw && n < cap; i++) {
        if (rbuf[i] == 0x7d && i + 1 < raw) f[n++] = rbuf[++i] ^ 0x20;
        else f[n++] = rbuf[i];
      }
      memmove(rbuf, end + 1, rlen - raw - 1);
      rlen -= raw + 1;
      if (n >= 3 && crc16(f, n - 2) == (f[n - 2] | f[n - 1] << 8)) return (int)(n - 2);
      continue;                                 /* empty/corrupt fragment: skip */
    }
    struct pollfd p = { sock, POLLIN, 0 };
    int pr = poll(&p, 1, timeout_ms);
    if (pr <= 0) return -1;
    ssize_t r = read(sock, rbuf + rlen, sizeof rbuf - rlen);
    if (r <= 0) die("diag connection closed");
    rlen += r;
    if (rlen == sizeof rbuf) rlen = 0;          /* runaway unframed data: drop it */
  }
}

/* Send an EFS2 request and return the matching response payload (after 0x4B 0x13 cmd16). */
static int efs_req(unsigned char *req, size_t n, unsigned char *resp, size_t cap) {
  static unsigned char f[16384];
  uint16_t cmd = req[2] | req[3] << 8;
  for (int attempt = 0; attempt < 3; attempt++) {
    send_frame(req, n);
    for (;;) {
      int len = recv_frame(f, sizeof f, 4000);
      if (len < 0) break;                        /* timeout -> resend */
      if (verbose) fprintf(stderr, "< %02x len %d\n", f[0], len);
      if (len >= 4 && f[0] == DIAG_SUBSYS_CMD_F && f[1] == SUBSYS_FS && (f[2] | f[3] << 8) == cmd) {
        if ((size_t)len - 4 > cap) die("response too large");
        memcpy(resp, f + 4, len - 4);
        return len - 4;
      }
      if (len >= 1 && (f[0] == 0x13 || f[0] == 0x14 || f[0] == 0x15 || f[0] == 0x18 || f[0] == 0x42 || f[0] == 0x47))
        die("modem rejected the EFS request (bad cmd/param/len/mode)");
      /* anything else is unrelated log/msg traffic: ignore */
    }
  }
  die("no response from modem");
  return -1;
}

static unsigned char *hdr(unsigned char *b, int cmd) { b[0] = DIAG_SUBSYS_CMD_F; b[1] = SUBSYS_FS; put16(b + 2, cmd); return b + 4; }

static void efs_hello(void) {
  unsigned char b[64], r[64], *p = hdr(b, EFS_HELLO);
  for (int i = 0; i < 6; i++) put32(p + 4 * i, 0x100000);
  put32(p + 24, 1); put32(p + 28, 1); put32(p + 32, 1); put32(p + 36, 0xFFFFFFFF);
  if (efs_req(b, 4 + 40, r, sizeof r) < 28 || get32(r + 24) != 1) die("EFS2 hello failed");
}

/* returns errno; fills size/mode */
static int efs_stat(const char *path, uint32_t *size, uint32_t *mode) {
  unsigned char b[600], r[64], *p = hdr(b, EFS_STAT);
  size_t pl = strlen(path) + 1;
  if (pl > 512) die("path too long");
  memcpy(p, path, pl);
  if (efs_req(b, 4 + pl, r, sizeof r) < 16) die("short STAT reply");
  if (mode) *mode = get32(r + 4);
  if (size) *size = get32(r + 8);
  return (int)get32(r);
}

static int efs_open(const char *path, uint32_t oflag, uint32_t mode) {
  unsigned char b[600], r[64], *p = hdr(b, EFS_OPEN);
  size_t pl = strlen(path) + 1;
  if (pl > 512) die("path too long");
  put32(p, oflag); put32(p + 4, mode); memcpy(p + 8, path, pl);
  if (efs_req(b, 12 + pl, r, sizeof r) < 8) die("short OPEN reply");
  int err = (int)get32(r + 4);
  if (err) { fprintf(stderr, "efstool: open %s: EFS errno %d\n", path, err); exit(err == EFS_ENOENT ? 2 : 1); }
  return (int)get32(r);
}

static void efs_close(int fd) {
  unsigned char b[16], r[16], *p = hdr(b, EFS_CLOSE);
  put32(p, fd);
  if (efs_req(b, 8, r, sizeof r) < 4 || get32(r)) die("close failed");
}

static void cmd_get(const char *src, const char *dst) {
  int fd = efs_open(src, 0 /* O_RDONLY */, 0);
  FILE *o = fopen(dst, "wb");
  if (!o) die("cannot create local file");
  uint32_t off = 0;
  for (;;) {
    unsigned char b[32], r[CHUNK + 64], *p = hdr(b, EFS_READ);
    put32(p, fd); put32(p + 4, CHUNK); put32(p + 8, off);
    int n = efs_req(b, 16, r, sizeof r);
    if (n < 16) die("short READ reply");
    int got = (int)get32(r + 8), err = (int)get32(r + 12);
    if (err) die("read failed");
    if (got > n - 16) die("READ length mismatch");
    if (got > 0 && fwrite(r + 16, 1, got, o) != (size_t)got) die("local write failed");
    off += got;
    if (got < CHUNK) break;
  }
  if (fclose(o)) die("local write failed");
  efs_close(fd);
}

static void cmd_put(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if (!in) die("cannot open local file");
  uint32_t mode = 0777, size;
  if (efs_stat(dst, &size, &mode) == 0) mode &= 0777; else mode = 0777;
  int fd = efs_open(dst, 01101 /* O_WRONLY|O_CREAT|O_TRUNC */, mode);
  unsigned char b[CHUNK + 32], r[64];
  uint32_t off = 0;
  size_t n;
  while ((n = fread(hdr(b, EFS_WRITE) + 8, 1, CHUNK, in)) > 0) {
    put32(b + 4, fd); put32(b + 8, off);
    if (efs_req(b, 12 + n, r, sizeof r) < 16) die("short WRITE reply");
    if (get32(r + 12) || get32(r + 8) != n) die("write failed");
    off += n;
  }
  fclose(in);
  efs_close(fd);
}

static void cleanup(void) {
  if (sock >= 0) close(sock);
  if (child > 0) {
    kill(child, SIGTERM);
    for (int i = 0; i < 20 && waitpid(child, NULL, WNOHANG) == 0; i++) usleep(100000);
    if (waitpid(child, NULL, WNOHANG) == 0) { kill(child, SIGKILL); waitpid(child, NULL, 0); }
  }
}

static void usage(void) {
  fprintf(stderr, "usage: efstool [-p port] [-d diag_socket_log] [-v] stat <efs> | get <efs> <local> | put <local> <efs>\n");
  exit(1);
}

int main(int argc, char **argv) {
  int port = 2577, o;
  const char *dsl = "/vendor/bin/diag_socket_log";
  while ((o = getopt(argc, argv, "p:d:v")) != -1) {
    if (o == 'p') port = atoi(optarg);
    else if (o == 'd') dsl = optarg;
    else if (o == 'v') verbose = 1;
    else usage();
  }
  argc -= optind; argv += optind;
  if (argc < 2) usage();
  if (!strcmp(argv[0], "stat") ? argc != 2 : argc != 3) usage();
  if (strcmp(argv[0], "stat") && strcmp(argv[0], "get") && strcmp(argv[0], "put")) usage();

  signal(SIGPIPE, SIG_IGN);
  int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
  if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 1)) die("cannot listen on 127.0.0.1");

  char ps[8];
  snprintf(ps, sizeof ps, "%d", port);
  child = fork();
  if (child == 0) {
    int dn = open("/dev/null", O_RDWR);
    if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); }
    execl(dsl, dsl, "-a", "127.0.0.1", "-p", ps, "-r", "3", (char *)NULL);
    _exit(127);
  }
  if (child < 0) die("fork failed");
  atexit(cleanup);

  struct pollfd p = { ls, POLLIN, 0 };
  if (poll(&p, 1, 15000) <= 0) die("diag_socket_log did not connect (needs root; is diag-router running?)");
  sock = accept(ls, NULL, NULL);
  close(ls);
  if (sock < 0) die("accept failed");

  efs_hello();
  if (!strcmp(argv[0], "stat")) {
    uint32_t size = 0, mode = 0;
    int err = efs_stat(argv[1], &size, &mode);
    if (err == EFS_ENOENT) return 2;
    if (err) { fprintf(stderr, "efstool: stat: EFS errno %d\n", err); return 1; }
    printf("size=%u mode=%o\n", size, mode);
  } else if (!strcmp(argv[0], "get")) {
    cmd_get(argv[1], argv[2]);
  } else {
    cmd_put(argv[1], argv[2]);
  }
  return 0;
}
