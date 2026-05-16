// user/stresstest.c
// Stress Testing for xv6 Enhanced Journaling
//
// Tests:
//   1. Large sequential writes                  (8 KB in 512-byte blocks)
//   2. Repeated create / write / delete         (40 iterations)
//   3. Concurrent writers via fork              (5 procs × 15 writes)
//   4. Log-overflow prevention                  (60 rapid create+write+delete)
//   5. Directory operations                     (mkdir / link / unlink)
//   6. Append stress                            (30 rounds × 64 bytes)
//   7. Read-back consistency                    (8 files, distinct patterns)
//
// Build: add $U/_stresstest to UPROGS in Makefile (already done)
// Run  : $ stresstest
//
// After all tests print PASS, run usertests to confirm full fs integrity.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ── helpers ────────────────────────────────────────────────────────────────

static void
die(const char *msg)
{
  printf("FAIL: %s\n", msg);
  exit(1);
}

// Build name like "prefix##" where ## is the decimal representation of i.
// Supports 0–99 safely; NFILES/NITER constants below stay within this range.
static void
mkname(char *out, const char *prefix, int i)
{
  int p = 0;
  while (prefix[p]) { out[p] = prefix[p]; p++; }
  if (i >= 10) out[p++] = '0' + (i / 10);
  out[p++] = '0' + (i % 10);
  out[p] = '\0';
}

// ── test 1: large sequential write ─────────────────────────────────────────

#define LARGE_BLOCKS  16      // 16 x 512 = 8 KB
#define BLOCK_SZ      512

static void
test_large_write(void)
{
  printf("[Test 1] Large sequential write (%d bytes)\n",
         LARGE_BLOCKS * BLOCK_SZ);

  int fd = open("st_large", O_CREATE | O_WRONLY);
  if (fd < 0) die("open st_large");

  char buf[BLOCK_SZ];
  int i;
  for (i = 0; i < BLOCK_SZ; i++) buf[i] = 'L';

  int t0 = uptime();
  for (i = 0; i < LARGE_BLOCKS; i++) {
    if (write(fd, buf, BLOCK_SZ) != BLOCK_SZ)
      die("write st_large");
  }
  close(fd);
  int t1 = uptime();

  // Verify size and content
  fd = open("st_large", O_RDONLY);
  if (fd < 0) die("reopen st_large");
  int total = 0, n;
  while ((n = read(fd, buf, BLOCK_SZ)) > 0) {
    int j;
    for (j = 0; j < n; j++)
      if (buf[j] != 'L') die("large write: content mismatch");
    total += n;
  }
  close(fd);

  if (total != LARGE_BLOCKS * BLOCK_SZ) die("large file: size mismatch");

  unlink("st_large");
  printf("  PASS  time=%d ticks  bytes=%d\n",
         t1 - t0, LARGE_BLOCKS * BLOCK_SZ);
}

// ── test 2: repeated create / write / delete ───────────────────────────────

#define REPEAT_ITERS  40

static void
test_create_delete(void)
{
  printf("[Test 2] Repeated create/write/delete x%d\n", REPEAT_ITERS);

  int t0 = uptime();
  int i;
  for (i = 0; i < REPEAT_ITERS; i++) {
    char name[12];
    mkname(name, "st_cd", i);

    int fd = open(name, O_CREATE | O_WRONLY);
    if (fd < 0) die("open in create_delete");
    if (write(fd, "xv6journal", 10) != 10) die("write in create_delete");
    close(fd);

    // Read back to confirm the write landed before we delete
    fd = open(name, O_RDONLY);
    if (fd < 0) die("reopen in create_delete");
    char buf[12];
    if (read(fd, buf, 10) != 10) die("readback in create_delete");
    close(fd);

    unlink(name);
  }
  int t1 = uptime();

  printf("  PASS  time=%d ticks  iterations=%d\n", t1 - t0, REPEAT_ITERS);
}

// ── test 3: concurrent writers ─────────────────────────────────────────────

#define N_WRITERS    5
#define WRITES_EACH  15
#define WRITE_SZ     128

static void
test_concurrent_writers(void)
{
  printf("[Test 3] Concurrent writers (%d procs x %d writes each)\n",
         N_WRITERS, WRITES_EACH);

  int t0 = uptime();
  int i;
  for (i = 0; i < N_WRITERS; i++) {
    int pid = fork();
    if (pid < 0) die("fork");

    if (pid == 0) {
      char name[12];
      mkname(name, "st_cw", i);

      int fd = open(name, O_CREATE | O_WRONLY);
      if (fd < 0) exit(1);

      char buf[WRITE_SZ];
      int j;
      for (j = 0; j < WRITE_SZ; j++) buf[j] = 'A' + i;
      for (j = 0; j < WRITES_EACH; j++) {
        if (write(fd, buf, WRITE_SZ) != WRITE_SZ) {
          close(fd);
          exit(1);
        }
      }
      close(fd);
      unlink(name);
      exit(0);
    }
  }

  int failures = 0, status;
  for (i = 0; i < N_WRITERS; i++) {
    wait(&status);
    if (status != 0) failures++;
  }
  int t1 = uptime();

  if (failures > 0) die("a concurrent writer child failed");

  printf("  PASS  time=%d ticks  procs=%d\n", t1 - t0, N_WRITERS);
}

// ── test 4: log-overflow prevention ────────────────────────────────────────
// xv6 LOGBLOCKS is typically 30.  We issue many more operations than that
// to confirm the enhanced log-space management never causes overflow.

#define OVERFLOW_ITERS  60

static void
test_log_overflow(void)
{
  printf("[Test 4] Log-overflow prevention (%d rapid operations)\n",
         OVERFLOW_ITERS);

  int i;
  for (i = 0; i < OVERFLOW_ITERS; i++) {
    char name[12];
    mkname(name, "st_lo", i);

    int fd = open(name, O_CREATE | O_WRONLY);
    if (fd < 0) die("open in log_overflow");
    if (write(fd, "ok", 2) != 2) die("write in log_overflow");
    close(fd);
    unlink(name);
  }

  printf("  PASS  no log overflow after %d operations\n", OVERFLOW_ITERS);
}

// ── test 5: directory operations ───────────────────────────────────────────

static void
test_dir_ops(void)
{
  printf("[Test 5] Directory operations (mkdir / link / unlink)\n");

  if (mkdir("st_dir") < 0) die("mkdir st_dir");

  int fd = open("st_dir/child", O_CREATE | O_WRONLY);
  if (fd < 0) die("open st_dir/child");
  write(fd, "hello", 5);
  close(fd);

  if (link("st_dir/child", "st_hlink") < 0) die("link");

  char buf[8];
  fd = open("st_hlink", O_RDONLY);
  if (fd < 0) die("open st_hlink");
  if (read(fd, buf, 5) != 5) die("read st_hlink");
  close(fd);

  if (buf[0]!='h' || buf[1]!='e' || buf[2]!='l')
    die("hard link content mismatch");

  unlink("st_hlink");
  unlink("st_dir/child");

  printf("  PASS  mkdir / link / unlink correct\n");
}

// ── test 6: append stress ──────────────────────────────────────────────────

#define APPEND_ROUNDS  30
#define APPEND_SZ      64

static void
test_append(void)
{
  printf("[Test 6] Append stress (%d rounds x %d bytes)\n",
         APPEND_ROUNDS, APPEND_SZ);

  int fd = open("st_app", O_CREATE | O_WRONLY);
  if (fd < 0) die("open st_app");

  int i;
  for (i = 0; i < APPEND_ROUNDS; i++) {
    char buf[APPEND_SZ];
    int j;
    for (j = 0; j < APPEND_SZ; j++) buf[j] = 'a' + (i % 26);
    if (write(fd, buf, APPEND_SZ) != APPEND_SZ) die("write in append");
  }
  close(fd);

  struct stat st;
  if (stat("st_app", &st) < 0) die("stat st_app");
  if (st.size != APPEND_ROUNDS * APPEND_SZ) die("append size mismatch");

  unlink("st_app");
  printf("  PASS  total size = %d bytes\n", APPEND_ROUNDS * APPEND_SZ);
}

// ── test 7: read-back consistency ──────────────────────────────────────────
// Writes a distinct byte pattern to each of N files, then reads them all
// back and verifies every byte.  Catches silent write drops — a symptom of
// journal bugs where blocks get absorbed but never actually written.

#define CONSIST_FILES  8
#define CONSIST_SZ     256

static void
test_readback_consistency(void)
{
  printf("[Test 7] Read-back consistency (%d files, %d bytes each)\n",
         CONSIST_FILES, CONSIST_SZ);

  int i, j;

  // Write phase: each file gets a unique fill byte so mismatches are obvious
  for (i = 0; i < CONSIST_FILES; i++) {
    char name[12];
    mkname(name, "st_rb", i);

    int fd = open(name, O_CREATE | O_WRONLY);
    if (fd < 0) die("open in readback write");

    char buf[CONSIST_SZ];
    for (j = 0; j < CONSIST_SZ; j++) buf[j] = (char)(i + 1);

    if (write(fd, buf, CONSIST_SZ) != CONSIST_SZ)
      die("write in readback");
    close(fd);
  }

  // Read-back phase
  for (i = 0; i < CONSIST_FILES; i++) {
    char name[12];
    mkname(name, "st_rb", i);

    int fd = open(name, O_RDONLY);
    if (fd < 0) die("reopen in readback verify");

    char buf[CONSIST_SZ];
    int n = read(fd, buf, CONSIST_SZ);
    close(fd);

    if (n != CONSIST_SZ) die("readback short read");

    for (j = 0; j < CONSIST_SZ; j++) {
      if (buf[j] != (char)(i + 1)) die("readback content mismatch");
    }

    unlink(name);
  }

  printf("  PASS  all %d files verified\n", CONSIST_FILES);
}

// ── main ───────────────────────────────────────────────────────────────────

int
main(void)
{
  printf("\n======================================================\n");
  printf("   xv6 Enhanced Journaling -- Stress Test Suite\n");
  printf("======================================================\n\n");

  test_large_write();
  test_create_delete();
  test_concurrent_writers();
  test_log_overflow();
  test_dir_ops();
  test_append();
  test_readback_consistency();

  printf("\n======================================================\n");
  printf("   All stress tests PASSED\n");
  printf("   Run 'usertests' next to confirm full fs integrity.\n");
  printf("======================================================\n\n");

  exit(0);
}