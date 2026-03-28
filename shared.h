/*
 * shared.h
 * Author: student
 * Date: 2026-03-27
 * Description: Shared data structures and IPC key definitions for OSS scheduler.
 *              Included by both oss.c and worker.c.
 */

#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */
#define MAX_PROCESSES   20
#define BASE_QUANTUM_NS 25000000    /* base time quantum: 25 ms in nanoseconds */
#define BLOCKED_TIME_NS 100000000   /* I/O block duration: 100 ms in nanoseconds */
#define MAX_LOG_LINES   10000       /* hard cap on log file lines */

/* ── IPC keys (ftok path + project-id; must match in oss.c and worker.c) ── */
#define SHM_KEY_PATH  "/tmp"
#define SHM_KEY_ID    'A'   /* shared memory key  */
#define MQ_OSS_KEY_ID 'B'   /* message queue key  */

/* ── Simulated clock (stored in shared memory, written only by OSS) ─────── */
typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimClock;

/* ── Process Control Block ────────────────────────────────────────────────── */
typedef struct {
    int   occupied;           /* 1 = slot in use, 0 = free              */
    pid_t pid;                /* actual OS PID of this child             */
    int   startSeconds;       /* simulated time when process was created */
    int   startNano;
    int   serviceTimeSeconds; /* total simulated CPU time used           */
    int   serviceTimeNano;
    int   eventWaitSec;       /* simulated time when I/O block ends      */
    int   eventWaitNano;
    int   blocked;            /* 1 = waiting on I/O event, 0 = not       */
    int   localPid;           /* simulated PID (table index + 1)         */
} PCB;

/* ── Shared memory layout ─────────────────────────────────────────────────── */
typedef struct {
    SimClock clock;                      /* simulated system clock          */
    PCB      processTable[MAX_PROCESSES];/* PCB array (up to 18 used)       */
} SharedData;

/* ── Message passed on the message queue ─────────────────────────────────── */
typedef struct {
    long mtype;  /* OSS→worker: worker's real PID; worker→OSS: OSS's PID  */
    long value;  /* OSS→worker: quantum (ns); worker→OSS: ns used (+/-)   */
} Message;

#endif /* SHARED_H */
