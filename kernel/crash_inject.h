// kernel/crash_inject.h
// Fault Injection Points for Crash Recovery Testing

#ifndef CRASH_INJECT_H
#define CRASH_INJECT_H

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/defs.h"

// Scenario A: Panic BEFORE commit
// #define CRASH_BEFORE_COMMIT   1

// Scenario B: Panic AFTER commit, before install
// #define CRASH_AFTER_COMMIT    1

// Scenario C: Panic during install
// #define CRASH_DURING_INSTALL  1

// Scenario D: Panic during group commit
// #define CRASH_DURING_GROUP    1

#ifdef CRASH_BEFORE_COMMIT
  #define INJECT_BEFORE_COMMIT()   panic("INJECT: crash before commit")
#else
  #define INJECT_BEFORE_COMMIT()   ((void)0)
#endif

#ifdef CRASH_AFTER_COMMIT
  #define INJECT_AFTER_COMMIT()    panic("INJECT: crash after commit")
#else
  #define INJECT_AFTER_COMMIT()    ((void)0)
#endif

#ifdef CRASH_DURING_INSTALL
  #define INJECT_DURING_INSTALL()  panic("INJECT: crash during install")
#else
  #define INJECT_DURING_INSTALL()  ((void)0)
#endif

#ifdef CRASH_DURING_GROUP
  #define INJECT_DURING_GROUP()    panic("INJECT: crash during group")
#else
  #define INJECT_DURING_GROUP()    ((void)0)
#endif

#endif