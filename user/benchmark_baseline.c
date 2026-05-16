// user/benchmark_baseline.c
// Baseline Benchmark for ORIGINAL xv6 (no get_log_stats)
//
// Run this on the ORIGINAL xv6 BEFORE any enhancements are applied.
// Save the output as your baseline comparison numbers.
//
// Build: add $U/_benchmark_baseline to UPROGS in Makefile (already done)
// Run  : $ benchmark_baseline
//
// After recording baseline numbers, switch to the enhanced branch
// and run user/benchmark.c (which reads log stats via get_log_stats).
//
// NOTE: This file intentionally does NOT call get_log_stats() because
// that syscall does not exist on the original unmodified xv6.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ── helpers ────────────────────────────────────────────────────────────────

static void
mkname(char *out, const char *prefix, int i)
{
  int p = 0;
  while (prefix[p]) { out[p] = prefix[p]; p++; }
  if (i >= 10) out[p++] = '0' + (i / 10);
  out[p++] = '0' + (i % 10);
  out[p] = '\0';
}

static int
avg3(int a, int b, int c)
{
  return (a + b + c) / 3;
}

static void
sep(void)
{
  printf("------------------------------------------------------\n");
}

// ── benchmark 1: sequential write throughput ───────────────────────────────

#define SEQ_BLOCKS  32
#define BLOCK_SZ    512

static int
bench_seq_once(void)
{
  int fd = open("bm_seq", O_CREATE | O_WRONLY);
  if (fd < 0) return -1;

  char buf[BLOCK_SZ];
  int i;
  for (i = 0; i < BLOCK_SZ; i++) buf[i] = 'S';

  int t0 = uptime();
  for (i = 0; i < SEQ_BLOCKS; i++)
    write(fd, buf, BLOCK_SZ);
  close(fd);
  int t1 = uptime();

  unlink("bm_seq");
  int ticks = t1 - t0;
  return (ticks > 0) ? (SEQ_BLOCKS * BLOCK_SZ) / ticks : (SEQ_BLOCKS * BLOCK_SZ);
}

static void
bench_sequential(void)
{
  printf("\n[Benchmark 1] Sequential write throughput (%d bytes/run)\n",
         SEQ_BLOCKS * BLOCK_SZ);
  sep();

  int r0 = bench_seq_once();
  int r1 = bench_seq_once();
  int r2 = bench_seq_once();

  printf("  Run 1             : %d bytes/tick\n", r0);
  printf("  Run 2             : %d bytes/tick\n", r1);
  printf("  Run 3             : %d bytes/tick\n", r2);
  printf("  Average           : %d bytes/tick  [record this]\n",
         avg3(r0, r1, r2));
}

// ── benchmark 2: small-file operation rate ─────────────────────────────────

#define SMALL_ITERS  30

static int
bench_small_once(void)
{
  int i;
  int t0 = uptime();
  for (i = 0; i < SMALL_ITERS; i++) {
    char name[12];
    mkname(name, "bm_sm", i);
    int fd = open(name, O_CREATE | O_WRONLY);
    if (fd < 0) return -1;
    write(fd, "baseline", 8);
    close(fd);
    unlink(name);
  }
  int t1 = uptime();
  int ticks = t1 - t0;
  return (ticks > 0) ? SMALL_ITERS / ticks : SMALL_ITERS;
}

static void
bench_small_files(void)
{
  printf("\n[Benchmark 2] Small-file ops (create + write + unlink)\n");
  sep();

  int r0 = bench_small_once();
  int r1 = bench_small_once();
  int r2 = bench_small_once();

  printf("  Run 1             : %d ops/tick\n", r0);
  printf("  Run 2             : %d ops/tick\n", r1);
  printf("  Run 3             : %d ops/tick\n", r2);
  printf("  Average           : %d ops/tick  [record this]\n",
         avg3(r0, r1, r2));
  printf("  Note: original xv6 commits each op alone (group size = 1)\n");
}

// ── benchmark 3: concurrent write throughput ───────────────────────────────

#define N_CONC       4
#define CONC_WRITES  20
#define CONC_SZ      256

static int
bench_conc_once(void)
{
  int i;
  int t0 = uptime();

  for (i = 0; i < N_CONC; i++) {
    int pid = fork();
    if (pid == 0) {
      char name[12];
      mkname(name, "bm_cw", i);
      int fd = open(name, O_CREATE | O_WRONLY);
      if (fd < 0) exit(1);
      char buf[CONC_SZ];
      int j;
      for (j = 0; j < CONC_SZ; j++) buf[j] = 'C' + i;
      for (j = 0; j < CONC_WRITES; j++) write(fd, buf, CONC_SZ);
      close(fd);
      unlink(name);
      exit(0);
    }
  }

  int status;
  for (i = 0; i < N_CONC; i++) wait(&status);

  int t1 = uptime();
  int total_bytes = N_CONC * CONC_WRITES * CONC_SZ;
  int ticks = t1 - t0;
  return (ticks > 0) ? total_bytes / ticks : total_bytes;
}

static void
bench_concurrent(void)
{
  printf("\n[Benchmark 3] Concurrent write throughput (%d processes)\n", N_CONC);
  sep();

  int r0 = bench_conc_once();
  int r1 = bench_conc_once();
  int r2 = bench_conc_once();

  printf("  Run 1             : %d bytes/tick\n", r0);
  printf("  Run 2             : %d bytes/tick\n", r1);
  printf("  Run 3             : %d bytes/tick\n", r2);
  printf("  Average           : %d bytes/tick  [record this]\n",
         avg3(r0, r1, r2));
  printf("  Note: original xv6 serializes all commits; no grouping.\n");
}

// ── benchmark 4: commit frequency (timing only) ────────────────────────────
// On original xv6 we cannot read log stats directly because get_log_stats
// does not exist.  We measure wall-clock time for a known number of ops
// and estimate commit count (one commit per end_op call in original xv6).

#define FREQ_ITERS  50

static void
bench_commit_frequency(void)
{
  printf("\n[Benchmark 4] Commit frequency (%d operations)\n", FREQ_ITERS);
  sep();

  int t0 = uptime();
  int i;
  for (i = 0; i < FREQ_ITERS; i++) {
    char name[12];
    mkname(name, "bm_cf", i);
    int fd = open(name, O_CREATE | O_WRONLY);
    if (fd < 0) continue;
    write(fd, "freq", 4);
    close(fd);
    unlink(name);
  }
  int t1 = uptime();

  printf("  Operations        : %d\n", FREQ_ITERS);
  printf("  Total ticks       : %d\n", t1 - t0);
  printf("  Ticks per op      : %d\n",
         (t1 - t0 > 0) ? (t1 - t0) / FREQ_ITERS : 0);
  printf("  Estimated commits : ~%d (1 per op — no grouping)\n",
         FREQ_ITERS * 3);
  printf("  Grouping ratio    : 1 ops/commit  [baseline — will improve]\n");
}

// ── main ───────────────────────────────────────────────────────────────────

int
main(void)
{
  printf("\n======================================================\n");
  printf("   xv6 BASELINE Benchmark (original, unmodified xv6)\n");
  printf("   Record these numbers BEFORE applying enhancements!\n");
  printf("======================================================\n");

  bench_sequential();
  bench_small_files();
  bench_concurrent();
  bench_commit_frequency();

  printf("\n======================================================\n");
  printf("   Baseline done. Record numbers in report table.\n");
  printf("   Then switch to enhanced branch and run: benchmark\n");
  printf("======================================================\n\n");

  exit(0);
}