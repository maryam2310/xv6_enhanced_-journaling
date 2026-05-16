#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "log.h"
#include "crash_inject.h"   // fault-injection macros (no-ops in normal builds)

struct log_stats lstats;

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//                 and checksums[A, B, C, ...]
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.
//
// enhancement: each logged block has a 32-bit checksum
// stored in the header. During recovery, blocks whose checksum
// does not match are skipped, preventing corrupted partial writes
// from being installed into the filesystem.

// ------------------------------------------------------------------
// Checksum: a simple djb2-style hash over a 1024-byte block.
// Fast, dependency-free, and good enough for a teaching OS.
// ------------------------------------------------------------------
static uint
block_checksum(uchar *data, int len)
{
  uint hash = 5381;
  for (int i = 0; i < len; i++)
    hash = ((hash << 5) + hash) + data[i]; // hash * 33 + byte
  return hash;
}

// ------------------------------------------------------------------
// On-disk / in-memory log header.
// The 'checksum' array addition.
// ------------------------------------------------------------------
struct logheader {
  int n;
  int block[LOGBLOCKS];
  uint checksum[LOGBLOCKS];  // one checksum per logged block
};

struct log {
  struct spinlock lock;
  int start;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit(void);

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.start = sb->logstart;
  log.dev = dev;
  recover_from_log();
}

// ------------------------------------------------------------------
// install_trans: copy committed blocks from log to their home location.
// verify each block's checksum before installing.
// If the checksum doesn't match, the block was partially written
// (e.g., power failure mid-write) — skip it and count the error.
// ------------------------------------------------------------------
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst

    // verify checksum of the log block before installing
    uint computed = block_checksum(lbuf->data, BSIZE);
    if (computed != log.lh.checksum[tail]) {
      // Checksum mismatch: this log block is corrupted or partially written.
      // Do NOT install it — leave the home block untouched.
      lstats.checksum_errors++;
      if (recovering) {
        printf("log: checksum MISMATCH at log block %d (dst %d) — skipping\n",
               tail, log.lh.block[tail]);
      }
      brelse(lbuf);
      brelse(dbuf);
      continue; // skip this block
    }

    // Checksum OK: safe to install.
    if (recovering) {
      printf("log: recovering tail %d dst %d (checksum OK)\n",
             tail, log.lh.block[tail]);
    }
    lstats.recovered_blocks++;
    memmove(dbuf->data, lbuf->data, BSIZE);
    bwrite(dbuf);

    if (tail == 0)
      INJECT_DURING_INSTALL(); // Person 4 — Scenario C: panic after first block installed
                               // no-op unless CRASH_DURING_INSTALL is defined

    if (recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
    log.lh.checksum[i] = lh->checksum[i]; // read checksums too
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the current transaction commits.
// checksums are already in lh.checksum[] (set by write_log),
// so they are persisted here alongside block numbers.
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
    hb->checksum[i] = log.lh.checksum[i]; // persist checksum
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // if committed, copy from log to disk (with checksum check)
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGBLOCKS){
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;
  int group_size = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
    group_size = log.lh.n;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
    // wait for commit to finish if another is in progress
    while(log.committing){
      sleep(&log, &log.lock);
    }
  }
  release(&log.lock);

  if(do_commit){
    INJECT_DURING_GROUP();     // Person 4 — Scenario D: panic before group commit fires
                               // no-op unless CRASH_DURING_GROUP is defined

    // update group commit statistics
    lstats.total_commits++;
    lstats.total_ops_grouped += group_size;
    if(group_size > lstats.max_group_size)
      lstats.max_group_size = group_size;
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
// compute and store a checksum for each block written.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to   = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);

    // compute checksum of the data BEFORE writing to disk.
    // This is stored in the in-memory header; write_head() persists it.
    log.lh.checksum[tail] = block_checksum(to->data, BSIZE);

    bwrite(to);  // write the log block
    brelse(from);
    brelse(to);
  }
}

static void
commit(void)
{
  if (log.lh.n > 0) {
    write_log();             // copy dirty bufs to log area on disk (+ checksums)

    INJECT_BEFORE_COMMIT();  // Person 4 — Scenario A: panic before commit block written
                             // no-op unless CRASH_BEFORE_COMMIT is defined

    write_head();            // write header to disk -- the real commit point

    INJECT_AFTER_COMMIT();   // Person 4 — Scenario B: panic after commit, before install
                             // no-op unless CRASH_AFTER_COMMIT is defined

    install_trans(0);        // install to home blocks (checksum-verified)
    log.lh.n = 0;
    write_head();            // erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;
  acquire(&log.lock);
  if (log.lh.n >= LOGBLOCKS)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorption
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log?
    bpin(b);
    log.lh.n++;
  }
  // if i < log.lh.n, block was absorbed (already in log)
  release(&log.lock);
}

// ------------------------------------------------------------------
// get_log_stats: fill caller-provided buffer with current log stats.
// the copyout to userspace is done in sysfile.c (sys_get_log_stats)
// because proc.h / pagetable access lives there, not in log.c.
// ------------------------------------------------------------------
void
get_log_stats(struct log_stats *dst)
{
  acquire(&log.lock);
  *dst = lstats;
  release(&log.lock);
}

// Print group commit + integrity statistics to the console
void
print_log_stats(void)
{
  acquire(&log.lock);
  printf("=== Log Statistics ===\n");
  printf("Total commits     : %d\n", lstats.total_commits);
  printf("Total ops grouped : %d\n", lstats.total_ops_grouped);
  printf("Max group size    : %d\n", lstats.max_group_size);
  if(lstats.total_commits > 0)
    printf("Avg group size    : %d\n",
           lstats.total_ops_grouped / lstats.total_commits);
  printf("--- Integrity ---\n");
  printf("Checksum errors   : %d\n", lstats.checksum_errors);
  printf("Recovered blocks  : %d\n", lstats.recovered_blocks);
  printf("======================\n");
  release(&log.lock);
}