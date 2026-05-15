// task4.c — test checksum-based log integrity verification
//
// This program:
//  1. Performs a burst of file writes (triggering many log commits).
//  2. Reads the log_stats struct via the new get_log_stats() syscall.
//  3. Prints all statistics — group commit + integrity.
//
// Under normal operation you will see 0 checksum_errors because no
// power failure happened. The checksum_errors counter would increase
// only when the OS recovers from a crash where a log block was partially
// written to disk. The test proves the syscall plumbing and stats work.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NFILES   4
#define NWRITES  20
#define BUFSZ    256

static char buf[BUFSZ];

// Helper: write NWRITES chunks to a file, triggering multiple log commits.
static void
stress_file(const char *path, char fill)
{
  int fd = open(path, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("task4: open %s failed\n", path);
    exit(1);
  }
  memset(buf, fill, BUFSZ);
  for (int i = 0; i < NWRITES; i++) {
    if (write(fd, buf, BUFSZ) != BUFSZ) {
      printf("task4: write failed\n");
      exit(1);
    }
  }
  close(fd);
}

int
main(void)
{
  char name[16];
  struct log_stats stats;

  printf("=== task4: Log Integrity Test ===\n\n");

  // --- Phase 1: generate log activity ---
  printf("[1] Writing to %d files (%d writes each)...\n", NFILES, NWRITES);
  for (int i = 0; i < NFILES; i++) {
    name[0] = 't'; name[1] = '4'; name[2] = 'f';
    name[3] = '0' + i; name[4] = '\0';
    stress_file(name, 'A' + i);
  }
  printf("    Done.\n\n");

  // --- Phase 2: create and delete some files (more log entries) ---
  printf("[2] Creating and removing directories...\n");
  mkdir("t4dir");
  unlink("t4dir");
  printf("    Done.\n\n");

  // --- Phase 3: read log stats from kernel ---
  printf("[3] Calling get_log_stats()...\n");
  if (get_log_stats(&stats) < 0) {
    printf("task4: get_log_stats syscall failed!\n");
    exit(1);
  }
  printf("    Success.\n\n");

  // --- Phase 4: display results ---
  printf("=== Log Statistics ===\n");
  printf("  [Group Commit]\n");
  printf("  Total commits       : %d\n", stats.total_commits);
  printf("  Total ops grouped   : %d\n", stats.total_ops_grouped);
  printf("  Max group size      : %d\n", stats.max_group_size);
  if (stats.total_commits > 0)
    printf("  Avg ops per commit  : %d\n",
           stats.total_ops_grouped / stats.total_commits);

  printf("\n  [Integrity Check]\n");
  printf("  Checksum errors     : %d\n", stats.checksum_errors);
  printf("  Recovered blocks    : %d\n", stats.recovered_blocks);

  if (stats.checksum_errors == 0)
    printf("\n  [PASS] No checksum errors — log integrity is intact.\n");
  else
    printf("\n  [WARN] %d corrupted log block(s) were detected and skipped"
           " during recovery.\n", stats.checksum_errors);

  printf("======================\n");

  // --- Cleanup ---
  for (int i = 0; i < NFILES; i++) {
    name[0] = 't'; name[1] = '4'; name[2] = 'f';
    name[3] = '0' + i; name[4] = '\0';
    unlink(name);
  }

  exit(0);
}
