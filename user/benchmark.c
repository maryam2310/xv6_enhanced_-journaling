// user/benchmark.c
// Benchmark Suite for xv6 Enhanced Journaling
//
// Measures:
//   1. Sequential write throughput  (bytes/tick)
//   2. Small-file operation rate    (ops/tick)
//   3. Concurrent write throughput  (bytes/tick, 4 processes)
//   4. Commit frequency / grouping  (ops per commit — key metric)
//   5. Cumulative statistics summary via get_log_stats()
//
// Each benchmark runs THREE times; average is printed (as required).
//
// Build: add $U/_benchmark to UPROGS in Makefile (already done)
// Run  : $ benchmark
//
// WORKFLOW:
//   Step 1 — run 'benchmark_baseline' on ORIGINAL xv6, save output.
//   Step 2 — apply enhancements, rebuild.
//   Step 3 — run 'benchmark' on enhanced xv6.
//   Step 4 — put both outputs side-by-side in the report table.
//
// Key metric to compare:
//   "Grouping ratio" — original xv6 = 1 op/commit.
//   Enhanced xv6 should show > 1 when processes run concurrently.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Must match the definition in user/user.h (added by Person 2)
struct log_stats {
  int total_commits;
  int total_ops_grouped;
  int max_group_size;
  int checksum_errors;
  int recovered_blocks;
};

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

static void
print_delta(struct log_stats *before, struct log_stats *after)
{
  int commits = after->total_commits - before->total_commits;
  int ops     = after->total_ops_grouped - before->total_ops_grouped;

  printf("  Commits issued    : %d\n", commits);
  printf("  Ops grouped       : %d\n", ops);
  printf("  Max group size    : %d\n", after->max_group_size);
  if (commits > 0)
    printf("  Avg group size    : %d ops/commit\n", ops / commits);
  else
    printf("  Avg group size    : n/a\n");
}

// ── benchmark 1: sequential write throughput ───────────────────────────────

#define SEQ_BLOCKS  32       // 32 x 512 = 16 KB per run
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

  struct log_stats s0, s1;
  get_log_stats(&s0);

  int r0 = bench_seq_once();
  int r1 = bench_seq_once();
  int r2 = bench_seq_once();

  get_log_stats(&s1);

  printf("  Run 1             : %d bytes/tick\n", r0);
  printf("  Run 2             : %d bytes/tick\n", r1);
  printf("  Run 3             : %d bytes/tick\n", r2);
  printf("  Average           : %d bytes/tick\n", avg3(r0, r1, r2));
  print_delta(&s0, &s1);
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
    write(fd, "benchmark", 9);
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
  printf("\n[Benchmark 2] Small-file operation rate\n");
  sep();

  struct log_stats s0, s1;
  get_log_stats(&s0);

  int r0 = bench_small_once();
  int r1 = bench_small_once();
  int r2 = bench_small_once();

  get_log_stats(&s1);

  printf("  Run 1             : %d ops/tick\n", r0);
  printf("  Run 2             : %d ops/tick\n", r1);
  printf("  Run 3             : %d ops/tick\n", r2);
  printf("  Average           : %d ops/tick\n", avg3(r0, r1, r2));
  print_delta(&s0, &s1);
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

  struct log_stats s0, s1;
  get_log_stats(&s0);

  int r0 = bench_conc_once();
  int r1 = bench_conc_once();
  int r2 = bench_conc_once();

  get_log_stats(&s1);

  printf("  Run 1             : %d bytes/tick\n", r0);
  printf("  Run 2             : %d bytes/tick\n", r1);
  printf("  Run 3             : %d bytes/tick\n", r2);
  printf("  Average           : %d bytes/tick\n", avg3(r0, r1, r2));
  print_delta(&s0, &s1);
}

// ── benchmark 4: commit frequency / grouping ratio ─────────────────────────
// THE key metric: enhanced xv6 should show avg group size > 1.
// Original xv6 commits exactly one op at a time, so ratio = 1 always.

#define FREQ_ITERS  50

static void
bench_commit_frequency(void)
{
  printf("\n[Benchmark 4] Commit frequency / grouping ratio (%d ops)\n",
         FREQ_ITERS);
  sep();

  struct log_stats before, after;
  get_log_stats(&before);

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

  get_log_stats(&after);

  int commits = after.total_commits - before.total_commits;
  int ops     = after.total_ops_grouped - before.total_ops_grouped;

  printf("  Operations        : %d\n", FREQ_ITERS);
  printf("  Total ticks       : %d\n", t1 - t0);
  printf("  Commits issued    : %d\n", commits);
  printf("  Ops grouped       : %d\n", ops);
  if (commits > 0) {
    int ratio = ops / commits;
    printf("  Grouping ratio    : %d ops/commit\n", ratio);
    printf("  Disk write saving : ~%d%%\n",
           (FREQ_ITERS - commits) * 100 / FREQ_ITERS);
  }
  printf("  (original xv6: ratio=1 | enhanced xv6: ratio > 1)\n");
}

// ── summary ────────────────────────────────────────────────────────────────

static void
print_summary(void)
{
  printf("\n[Summary] Cumulative log statistics since boot\n");
  sep();

  struct log_stats stats;
  get_log_stats(&stats);

  printf("  total_commits     : %d\n", stats.total_commits);
  printf("  total_ops_grouped : %d\n", stats.total_ops_grouped);
  printf("  max_group_size    : %d\n", stats.max_group_size);
  if (stats.total_commits > 0)
    printf("  overall avg group : %d ops/commit\n",
           stats.total_ops_grouped / stats.total_commits);
  printf("  checksum_errors   : %d\n", stats.checksum_errors);
  printf("  recovered_blocks  : %d\n", stats.recovered_blocks);
}

// ── main ───────────────────────────────────────────────────────────────────

int
main(void)
{
  printf("\n======================================================\n");
  printf("   xv6 Enhanced Journaling -- Benchmark Suite\n");
  printf("   Compare this output against benchmark_baseline.\n");
  printf("======================================================\n");

  bench_sequential();
  bench_small_files();
  bench_concurrent();
  bench_commit_frequency();
  print_summary();

  printf("\n======================================================\n");
  printf("   Done. Copy numbers into the report comparison table.\n");
  printf("======================================================\n\n");

  exit(0);
}