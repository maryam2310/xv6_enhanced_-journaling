// user/crashtest.c
// Crash Recovery Testing for xv6 Enhanced Journaling
//
// Usage:
//   crashtest write          — write known data through the journal
//   crashtest verify         — after reboot: confirm committed data survived
//   crashtest verify_absent  — after reboot: confirm uncommitted data is gone
//   crashtest concurrent     — spawn concurrent writers for crash scenario D
//   crashtest help           — show usage
//
// Build: $U/_crashtest is added to UPROGS in Makefile
// Run  : $ crashtest <command>
//
// ======================================================================
// CRASH SCENARIO PROCEDURES
// ======================================================================
//
//  Scenario A — crash BEFORE commit
//  Expected: data does NOT survive (WAL atomicity)
//    1. In kernel/crash_inject.h, uncomment: #define CRASH_BEFORE_COMMIT 1
//    2. make clean && make qemu
//    3. $ crashtest write          <- system panics automatically
//    4. make qemu                  <- reboot
//    5. $ crashtest verify_absent
//    6. $ usertests
//    7. Comment out the define and rebuild.
//
//  Scenario B — crash AFTER commit, before install
//  Expected: data SURVIVES (recovery replays log on boot)
//    1. Uncomment: #define CRASH_AFTER_COMMIT 1
//    2. make clean && make qemu
//    3. $ crashtest write          <- system panics after write_head()
//    4. make qemu                  <- reboot; initlog() replays the log
//    5. $ crashtest verify
//    6. $ usertests
//    7. Comment out and rebuild.
//
//  Scenario C — crash DURING install_trans
//  Expected: data SURVIVES (recovery is idempotent)
//    1. Uncomment: #define CRASH_DURING_INSTALL 1
//    2. make clean && make qemu
//    3. $ crashtest write          <- panics after first block installed
//    4. make qemu                  <- reboot; recovery replays from start
//    5. $ crashtest verify
//    6. $ usertests
//    7. Comment out and rebuild.
//
//  Scenario D — manual QEMU kill (random crash point)
//  Expected: filesystem stays consistent regardless of crash timing
//    1. $ crashtest concurrent     <- concurrent writers running
//    2. Ctrl+A X                   <- kill QEMU at any point
//    3. make qemu                  <- reboot
//    4. $ usertests                <- must pass fully
//
//  Scenario E — crash DURING group commit (enhanced xv6 only)
//  Expected: all grouped ops atomically survive or vanish together
//    1. Uncomment: #define CRASH_DURING_GROUP 1
//    2. make clean && make qemu
//    3. $ crashtest concurrent     <- multiple ops queued; panics mid-group
//    4. make qemu                  <- reboot
//    5. $ usertests
//    6. Comment out and rebuild.
//
// ======================================================================

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ── constants ──────────────────────────────────────────────────────────────

#define COMMIT_FILE  "ct_committed"
#define EXTRA_FILE   "ct_extra"
#define MAGIC        "SURVIVE_AFTER_CRASH"
#define MAGIC_LEN    19

// ── helpers ────────────────────────────────────────────────────────────────

static void
pass(const char *msg)
{
  printf("  PASS: %s\n", msg);
}

static void
fail(const char *msg)
{
  printf("  FAIL: %s\n", msg);
  exit(1);
}

static int
streq(const char *a, const char *b)
{
  while (*a && *b) {
    if (*a != *b) return 0;
    a++; b++;
  }
  return *a == *b;
}

// ── scenario: write ────────────────────────────────────────────────────────
// Writes a known magic string and a second file through the journal.
// If a crash injection point is active in kernel/crash_inject.h, the
// system panics automatically at the chosen WAL phase.

static void
cmd_write(void)
{
  printf("[crashtest write]\n");
  printf("  Writing '%s' to %s ...\n", MAGIC, COMMIT_FILE);

  unlink(COMMIT_FILE);

  int fd = open(COMMIT_FILE, O_CREATE | O_WRONLY);
  if (fd < 0) fail("cannot create " COMMIT_FILE);

  if (write(fd, MAGIC, MAGIC_LEN) != MAGIC_LEN)
    fail("short write — journal may be full");

  close(fd);

  // A second file gives recovery more blocks to replay.
  int fd2 = open(EXTRA_FILE, O_CREATE | O_WRONLY);
  if (fd2 >= 0) {
    write(fd2, "extra_data_block", 16);
    close(fd2);
  }

  printf("  Journal commit attempted.\n");
  printf("  If an injection point was active, the panic fired above.\n");
  printf("  If no injection is active: kill QEMU now (Ctrl+A X),\n");
  printf("  then reboot and run:  crashtest verify\n\n");
}

// ── scenario: verify ───────────────────────────────────────────────────────
// Confirms that COMMIT_FILE survived the crash.
// Use after Scenarios B and C.

static void
cmd_verify(void)
{
  printf("[crashtest verify] — committed data should have survived\n");

  int fd = open(COMMIT_FILE, O_RDONLY);
  if (fd < 0) {
    printf("  FAIL: '%s' missing after reboot.\n", COMMIT_FILE);
    printf("  Committed data was lost — WAL durability violated!\n");
    exit(1);
  }

  char buf[MAGIC_LEN + 4];
  int n = read(fd, buf, MAGIC_LEN);
  close(fd);

  if (n != MAGIC_LEN) {
    printf("  FAIL: expected %d bytes, got %d\n", MAGIC_LEN, n);
    exit(1);
  }

  int i;
  for (i = 0; i < MAGIC_LEN; i++) {
    if (buf[i] != MAGIC[i]) {
      printf("  FAIL: byte %d corrupted (got 0x%x want 0x%x)\n",
             i, (unsigned char)buf[i], (unsigned char)MAGIC[i]);
      exit(1);
    }
  }

  pass("Committed data survived crash intact.  WAL durability holds.");
  unlink(COMMIT_FILE);
  unlink(EXTRA_FILE);
}

// ── scenario: verify_absent ────────────────────────────────────────────────
// Confirms that COMMIT_FILE does NOT exist.
// Use after Scenario A (crash before commit).

static void
cmd_verify_absent(void)
{
  printf("[crashtest verify_absent] — uncommitted data should be absent\n");

  int fd = open(COMMIT_FILE, O_RDONLY);
  if (fd >= 0) {
    close(fd);
    printf("  FAIL: '%s' exists after crash-before-commit!\n", COMMIT_FILE);
    printf("  Uncommitted data persisted — WAL atomicity violated!\n");
    exit(1);
  }

  pass("Uncommitted data correctly absent.  WAL atomicity holds.");
}

// ── scenario: concurrent ───────────────────────────────────────────────────
// Spawns multiple concurrent writers so group-commit is exercised.
// Kill QEMU with Ctrl+A X at any point, then reboot and run usertests.

#define CONC_PROCS  4
#define CONC_ITERS  12

static void
cmd_concurrent(void)
{
  printf("[crashtest concurrent]\n");
  printf("  Spawning %d writer processes (%d writes each)...\n",
         CONC_PROCS, CONC_ITERS);

  int i;
  for (i = 0; i < CONC_PROCS; i++) {
    int pid = fork();
    if (pid < 0) fail("fork failed");

    if (pid == 0) {
      int j;
      for (j = 0; j < CONC_ITERS; j++) {
        // filename: "ct_pN_JJ"
        char name[12];
        name[0]='c'; name[1]='t'; name[2]='_'; name[3]='p';
        name[4]='0'+i; name[5]='_';
        name[6]='0'+(j/10); name[7]='0'+(j%10); name[8]='\0';

        int fd = open(name, O_CREATE | O_WRONLY);
        if (fd < 0) exit(1);
        write(fd, "concurrent_write", 16);
        close(fd);
        // leave files on disk so post-crash state is visible
      }
      exit(0);
    }
  }

  int status, failures = 0;
  for (i = 0; i < CONC_PROCS; i++) {
    wait(&status);
    if (status != 0) failures++;
  }

  if (failures)
    printf("  WARNING: %d child process(es) reported errors.\n", failures);

  printf("\n  Writers done. Kill QEMU now (Ctrl+A X) for Scenario D/E,\n");
  printf("  or reboot and run usertests to confirm no crash occurred.\n\n");

  // Clean up if no crash happened
  for (i = 0; i < CONC_PROCS; i++) {
    int j;
    for (j = 0; j < CONC_ITERS; j++) {
      char name[12];
      name[0]='c'; name[1]='t'; name[2]='_'; name[3]='p';
      name[4]='0'+i; name[5]='_';
      name[6]='0'+(j/10); name[7]='0'+(j%10); name[8]='\0';
      unlink(name);
    }
  }
  printf("  (No crash occurred — cleaned up test files.)\n");
}

// ── help ───────────────────────────────────────────────────────────────────

static void
cmd_help(void)
{
  printf("Usage: crashtest <command>\n\n");
  printf("Commands:\n");
  printf("  write          Write known data through the journal\n");
  printf("  verify         After reboot: committed data should exist\n");
  printf("  verify_absent  After reboot: uncommitted data should be absent\n");
  printf("  concurrent     Concurrent writers -- use with Ctrl+A X crash\n");
  printf("  help           Show this message\n\n");
  printf("See top-of-file comments for full scenario procedures.\n");
}

// ── main ───────────────────────────────────────────────────────────────────

int
main(int argc, char *argv[])
{
  printf("\n====== xv6 Crash Recovery Test ======\n");

  if (argc < 2) {
    cmd_help();
    exit(1);
  }

  char *cmd = argv[1];

  if (streq(cmd, "write")) {
    cmd_write();
  } else if (streq(cmd, "verify_absent")) {
    cmd_verify_absent();
  } else if (streq(cmd, "verify")) {
    cmd_verify();
  } else if (streq(cmd, "concurrent")) {
    cmd_concurrent();
  } else if (streq(cmd, "help")) {
    cmd_help();
  } else {
    printf("  Unknown command: %s\n\n", cmd);
    cmd_help();
    exit(1);
  }

  printf("=====================================\n\n");
  exit(0);
}