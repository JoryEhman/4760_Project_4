/*
 * oss.c
 * Author: student
 * Date: 2026-03-27
 * Environment: Linux, gcc
 *
 * Description:
 *   Operating System Simulator implementing a Round-Robin scheduler.
 *   - Manages up to 18 concurrent child (worker) processes.
 *   - Uses shared memory for the simulated clock and process control table.
 *   - Uses a single message queue (per-process mtype) for all IPC.
 *   - All IPC keys derived with ftok() - safe on shared server (hoare).
 *   - Terminates after all -n processes finish OR 3 real-life seconds elapse.
 *   - Prints process table to BOTH screen and log every 500 ms simulated time.
 *   - Cleans up all IPC on normal and abnormal (signal) exit.
 *
 * CPU Utilization:
 *   Calculated as the total clock time incremented by OSS for overhead/idle
 *   (i.e. NOT from worker run time) divided by total simulated time, per spec.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>

#include "shared.h"
#include "queue.h"

/* ── Globals ────────────────────────────────────────────────────────────── */
static int        shmId = -1;
static int        msgId = -1;
static SharedData *shm  = NULL;

static Queue readyQueue;
static Queue blockedQueue;

static FILE *logFp         = NULL;
static int   logLines      = 0;      /* actual newline count in log        */
static int   totalLaunched = 0;
static int   totalFinished = 0;

/* Statistics
 * Per spec: CPU utilization = time OSS incremented clock for reasons OTHER
 * than a worker running (i.e. overhead + idle).  We track that separately. */
static unsigned long long totalWorkerNs   = 0; /* ns workers actually ran    */
static unsigned long long totalOverheadNs = 0; /* ns from OSS overhead/idle  */
static unsigned long long totalClockNs    = 0; /* total simulated ns          */

/* Command-line options */
static int    opt_n = 18;        /* total processes to launch        */
static int    opt_s = 18;        /* max simultaneous                 */
static double opt_t = 2.5;       /* upper bound cpu burst (seconds)  */
static double opt_i = 0.5;       /* min seconds between launches     */
static char   logFile[256] = "oss.log";

/* Real-time start for 3-second wall-clock limit */
static time_t wallStart;

/* Set to 1 by SIGALRM when the 3-second real-time limit fires */
static volatile sig_atomic_t timeExpired = 0;

/* ── Forward declarations ───────────────────────────────────────────────── */
static void cleanup(void);
static void sigHandler(int sig);
static void addNano(SimClock *c, unsigned int ns);
static unsigned long long clockToNs(const SimClock *c);
static int  countActive(void);
static void logWrite(const char *fmt, ...);
static void printProcessTable(void);
static void printQueue(Queue *q, const char *label);
static void checkBlocked(void);
static int  launchChild(int idx, unsigned long long cpuBurstNs);
static int  findFreePCB(void);
static void printFinalReport(void);

/* ── Clock helpers ──────────────────────────────────────────────────────── */

/* Add ns to clock with carry into seconds */
static void addNano(SimClock *c, unsigned int ns) {
    c->nanoseconds += ns;
    while (c->nanoseconds >= 1000000000u) {
        c->nanoseconds -= 1000000000u;
        c->seconds++;
    }
}

/* Convert SimClock to a single unsigned long long (ns) for comparisons */
static unsigned long long clockToNs(const SimClock *c) {
    return (unsigned long long)c->seconds * 1000000000ULL + c->nanoseconds;
}

/* Count occupied PCB slots */
static int countActive(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (shm->processTable[i].occupied) n++;
    return n;
}

/* ── Logging ─────────────────────────────────────────────────────────────── */
/*
 * logWrite: writes to log file AND stdout, counting actual newlines so the
 * 10,000-line cap is accurate (each '\n' = one line, matching wc -l).
 */
static void logWrite(const char *fmt, ...) {
    if (!logFp) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Count newlines for the line cap */
    for (const char *p = buf; *p; p++)
        if (*p == '\n') logLines++;

    if (logLines > MAX_LOG_LINES) return; /* stop writing once cap is hit */

    fputs(buf, logFp);
    fflush(logFp);
}

/* ── Process table: print to BOTH log and stdout (spec requirement) ─────── */
static void printProcessTable(void) {
    char header[128];
    snprintf(header, sizeof(header),
             "\n--- Process Table @ %u:%09u ---\n",
             shm->clock.seconds, shm->clock.nanoseconds);

    logWrite("%s", header);
    printf("%s", header);

    const char *col = "Slot  PID    Blocked  SvcSec         SvcNano        "
                      "LocPid  EventWait\n";
    logWrite("%s", col);
    printf("%s", col);

    for (int i = 0; i < MAX_PROCESSES; i++) {
        PCB *p = &shm->processTable[i];
        if (!p->occupied) continue;
        char row[256];
        snprintf(row, sizeof(row),
                 "%-5d %-6d %-8d %-14d %-14d %-7d %d:%09d\n",
                 i, p->pid, p->blocked,
                 p->serviceTimeSeconds, p->serviceTimeNano,
                 p->localPid,
                 p->eventWaitSec, p->eventWaitNano);
        logWrite("%s", row);
        printf("%s", row);
    }

    const char *sep = "-------------------------------------------\n";
    logWrite("%s", sep);
    printf("%s", sep);

    /* Blocked process list */
    if (!queue_empty(&blockedQueue)) {
        logWrite("Blocked processes: [");
        printf("Blocked processes: [");
        int cur = blockedQueue.head;
        for (int i = 0; i < blockedQueue.count; i++) {
            int idx = blockedQueue.data[cur];
            logWrite(" P%d", shm->processTable[idx].localPid);
            printf(" P%d", shm->processTable[idx].localPid);
            cur = (cur + 1) % QUEUE_SIZE;
        }
        logWrite(" ]\n");
        printf(" ]\n");
    }
    logWrite("\n");
    printf("\n");
}

/* Print ready queue to log only (called every scheduling decision) */
static void printQueue(Queue *q, const char *label) {
    logWrite("OSS: %s [", label);
    int cur = q->head;
    for (int i = 0; i < q->count; i++) {
        int idx = q->data[cur];
        logWrite(" P%d", shm->processTable[idx].localPid);
        cur = (cur + 1) % QUEUE_SIZE;
    }
    logWrite(" ]\n");
}

/* ── Signal handling / IPC cleanup ─────────────────────────────────────── */
static void cleanup(void) {
    /* Send SIGTERM to all live children */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shm && shm->processTable[i].occupied)
            kill(shm->processTable[i].pid, SIGTERM);
    }
    /* Reap children (non-blocking) */
    int st;
    while (waitpid(-1, &st, WNOHANG) > 0);

    /* Detach and remove shared memory */
    if (shm)    { shmdt(shm); shm = NULL; }
    if (shmId != -1) { shmctl(shmId, IPC_RMID, NULL); shmId = -1; }

    /* Remove message queue */
    if (msgId != -1) { msgctl(msgId, IPC_RMID, NULL); msgId = -1; }

    if (logFp)  { fclose(logFp); logFp = NULL; }
}

static void sigHandler(int sig) {
    if (sig == SIGALRM) {
        /* Set flag; main loop detects it and exits cleanly after current dispatch */
        timeExpired = 1;
        return;
    }
    /* SIGINT / SIGTERM: hard stop with cleanup */
    fprintf(stderr, "\nOSS: caught signal %d - cleaning up and terminating.\n", sig);
    if (logFp) {
        fprintf(logFp,
                "\nOSS: caught signal %d - cleaning up and terminating.\n", sig);
        fflush(logFp);
    }
    printFinalReport();
    cleanup();
    exit(1);
}

/* ── Check blocked queue: move any process whose wake time has arrived ── */
static void checkBlocked(void) {
    if (queue_empty(&blockedQueue)) return;

    unsigned long long now  = clockToNs(&shm->clock);
    int size  = blockedQueue.count;
    int moved = 0;

    for (int i = 0; i < size; i++) {
        int idx = dequeue(&blockedQueue);
        PCB *p  = &shm->processTable[idx];
        unsigned long long wakeNs =
            (unsigned long long)p->eventWaitSec  * 1000000000ULL
          + (unsigned long long)p->eventWaitNano;

        if (now >= wakeNs) {
            /* Unblock: move to back of ready queue */
            p->blocked       = 0;
            p->eventWaitSec  = 0;
            p->eventWaitNano = 0;
            enqueue(&readyQueue, idx);
            logWrite("OSS: Unblocking process P%d, moving to ready queue at %u:%09u\n",
                     p->localPid, shm->clock.seconds, shm->clock.nanoseconds);
            /* Overhead: moving a process from blocked→ready costs a little time */
            addNano(&shm->clock, 1000);
            totalOverheadNs += 1000;
            moved++;
        } else {
            enqueue(&blockedQueue, idx);
        }
    }
    if (moved) {
        /* Additional overhead for the unblocking bookkeeping */
        unsigned int oh = (unsigned int)(moved * 500);
        addNano(&shm->clock, oh);
        totalOverheadNs += oh;
    }
}

/* ── Fork and exec a worker child ───────────────────────────────────────── */
static int launchChild(int idx, unsigned long long cpuBurstNs) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        /* Child: exec worker with its total cpu burst as argument */
        char burstStr[32];
        snprintf(burstStr, sizeof(burstStr), "%llu", cpuBurstNs);
        execl("./worker", "worker", burstStr, NULL);
        perror("execl worker");
        exit(1);
    }

    /* Parent: initialise PCB */
    PCB *p           = &shm->processTable[idx];
    p->occupied          = 1;
    p->pid               = pid;
    p->startSeconds      = (int)shm->clock.seconds;
    p->startNano         = (int)shm->clock.nanoseconds;
    p->serviceTimeSeconds = 0;
    p->serviceTimeNano    = 0;
    p->eventWaitSec       = 0;
    p->eventWaitNano      = 0;
    p->blocked            = 0;
    p->localPid           = idx + 1;   /* 1-based simulated PID */

    enqueue(&readyQueue, idx);

    logWrite("OSS: Generating process with PID %d (table[%d]) and putting it"
             " in ready queue at time %u:%09u\n",
             p->localPid, idx,
             shm->clock.seconds, shm->clock.nanoseconds);
    return 0;
}

/* Find first free PCB slot */
static int findFreePCB(void) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (!shm->processTable[i].occupied) return i;
    return -1;
}

/* ── Final report (called on normal exit AND signal exit) ───────────────── */
static void printFinalReport(void) {
    totalClockNs = clockToNs(&shm->clock);

    /*
     * CPU utilization per spec:
     *   "calculated by storing up the total time you incremented the clock
     *    that was NOT from getting a message back from a process"
     * = totalOverheadNs / totalClockNs  (overhead + idle, not worker time)
     */
    double cpuUtil = 0.0;
    if (totalClockNs > 0)
        cpuUtil = (double)totalOverheadNs / (double)totalClockNs * 100.0;

    /* Write to log */
    logWrite("\n=== Final Report ===\n");
    logWrite("Total processes launched:    %d\n", totalLaunched);
    logWrite("Total processes finished:    %d\n", totalFinished);
    logWrite("Total simulated time:        %u.%09u s\n",
             shm->clock.seconds, shm->clock.nanoseconds);
    logWrite("Total worker CPU time:       %llu ns\n", totalWorkerNs);
    logWrite("Total OSS overhead/idle:     %llu ns\n", totalOverheadNs);
    logWrite("CPU Utilization (overhead):  %.2f%%\n", cpuUtil);

    /* Mirror to stdout */
    printf("\n=== OSS Final Report ===\n");
    printf("Total processes launched:    %d\n", totalLaunched);
    printf("Total processes finished:    %d\n", totalFinished);
    printf("Total simulated time:        %u.%09u s\n",
           shm->clock.seconds, shm->clock.nanoseconds);
    printf("Total worker CPU time:       %llu ns\n", totalWorkerNs);
    printf("Total OSS overhead/idle:     %llu ns\n", totalOverheadNs);
    printf("CPU Utilization (overhead):  %.2f%%\n", cpuUtil);
}

/* ── Usage ───────────────────────────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-h] [-n proc] [-s simul] [-t timelimit]"
        " [-i fraction] [-f logfile]\n"
        "  -h            Help\n"
        "  -n proc       Total processes to launch (default 18)\n"
        "  -s simul      Max simultaneous processes (default 18, cap 18)\n"
        "  -t timelimit  Upper bound cpu burst in seconds (default 2.5)\n"
        "  -i fraction   Min fraction-of-second between launches (default 0.5)\n"
        "  -f logfile    Log file name (default oss.log)\n",
        prog);
}

/* ═══════════════════════════════ MAIN ══════════════════════════════════════ */
int main(int argc, char *argv[]) {

    /* ── Parse command-line arguments ── */
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
        case 'h': usage(argv[0]); return 0;
        case 'n': opt_n = atoi(optarg); break;
        case 's': opt_s = atoi(optarg); break;
        case 't': opt_t = atof(optarg); break;
        case 'i': opt_i = atof(optarg); break;
        case 'f': strncpy(logFile, optarg, sizeof(logFile) - 1); break;
        default:  usage(argv[0]); return 1;
        }
    }
    if (opt_s > 18) opt_s = 18;
    if (opt_s < 1)  opt_s = 1;

    /* ── Signal handlers ── */
    /* SIGINT and SIGTERM: use SA_RESTART so syscalls resume normally */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigHandler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* SIGALRM: do NOT set SA_RESTART so that msgrcv() is interrupted
     * with EINTR when the alarm fires. This is how we break out of
     * the blocking msgrcv call when the 3-second limit expires. */
    struct sigaction sa_alrm;
    memset(&sa_alrm, 0, sizeof(sa_alrm));
    sa_alrm.sa_handler = sigHandler;
    sa_alrm.sa_flags = 0;  /* no SA_RESTART - interrupt blocking syscalls */
    sigaction(SIGALRM, &sa_alrm, NULL);
    alarm(3);

    wallStart = time(NULL);

    /* ── Open log file (fresh each run, not appended) ── */
    logFp = fopen(logFile, "w");
    if (!logFp) { perror("fopen logfile"); return 1; }

    /* ── IPC setup using ftok (safe on shared server) ── */
    key_t shmKey = ftok(SHM_KEY_PATH, SHM_KEY_ID);
    if (shmKey == -1) { perror("ftok shm"); cleanup(); return 1; }

    key_t mqKey = ftok(SHM_KEY_PATH, MQ_OSS_KEY_ID);
    if (mqKey == -1)  { perror("ftok mq");  cleanup(); return 1; }

    /* Shared memory: clock + process table */
    shmId = shmget(shmKey, sizeof(SharedData), IPC_CREAT | 0644);
    if (shmId == -1) { perror("shmget"); cleanup(); return 1; }
    shm = (SharedData *)shmat(shmId, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); cleanup(); return 1; }
    memset(shm, 0, sizeof(SharedData));

    /* Message queue */
    msgId = msgget(mqKey, IPC_CREAT | 0644);
    if (msgId == -1) { perror("msgget"); cleanup(); return 1; }

    /* ── Initialize scheduling queues ── */
    queue_init(&readyQueue);
    queue_init(&blockedQueue);

    /* ── Timing state ── */
    unsigned long long nextLaunchNs    = 0;
    unsigned long long intervalNs      = (unsigned long long)(opt_i * 1e9);
    unsigned long long lastTablePrintNs = 0;
    const unsigned long long TABLE_INTERVAL = 500000000ULL; /* 500 ms sim-time */

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    /* ════════════════════ Main scheduling loop ════════════════════ */
    while (totalFinished < opt_n) {

        /* ── 3-second real-time wall-clock check ── */
        if (timeExpired) {
            logWrite("OSS: 3-second real-time limit reached - terminating.\n");
            printf("OSS: 3-second real-time limit reached - terminating.\n");
            break;
        }

        unsigned long long nowNs = clockToNs(&shm->clock);

        /* ── Check if any blocked process should wake up ── */
        checkBlocked();

        /* ── Launch a new child if conditions allow ── */
        int canLaunch = (totalLaunched < opt_n)
                     && (countActive()  < opt_s)
                     && (nowNs          >= nextLaunchNs);

        if (canLaunch) {
            int idx = findFreePCB();
            if (idx >= 0) {
                /* Random cpu burst in [1 ns, opt_t seconds] */
                unsigned long long maxBurstNs =
                    (unsigned long long)(opt_t * 1e9);
                unsigned long long burstNs =
                    (unsigned long long)rand() % maxBurstNs + 1;

                if (launchChild(idx, burstNs) == 0) {
                    totalLaunched++;
                    /* Stagger the next launch by intervalNs + random jitter */
                    unsigned long long jitter = (intervalNs > 0)
                        ? (unsigned long long)rand() % intervalNs : 0;
                    nextLaunchNs = clockToNs(&shm->clock) + intervalNs + jitter;
                    /* Overhead: forking and setting up PCB */
                    addNano(&shm->clock, 500);
                    totalOverheadNs += 500;
                }
            }
        }

        /* ── Schedule the process at the front of the ready queue ── */
        if (!queue_empty(&readyQueue)) {

            /* Print ready queue BEFORE removing the process (shows who runs) */
            printQueue(&readyQueue, "Ready queue");

            int idx = dequeue(&readyQueue);
            PCB *p  = &shm->processTable[idx];

            /* Overhead: time OSS spends making the scheduling decision */
            unsigned long long dispatchStart = clockToNs(&shm->clock);
            unsigned int dispatchOh = (unsigned int)(500 + rand() % 9501);
            addNano(&shm->clock, dispatchOh);
            totalOverheadNs += dispatchOh;
            unsigned long long dispatchTime = clockToNs(&shm->clock) - dispatchStart;

            logWrite("OSS: Dispatching process with PID %d from ready queue"
                     " at time %u:%09u,\n"
                     "OSS: total time this dispatch was %llu nanoseconds\n",
                     p->localPid,
                     shm->clock.seconds, shm->clock.nanoseconds,
                     dispatchTime);

            /* Send time quantum to worker (addressed by worker's real PID) */
            Message outMsg;
            outMsg.mtype = (long)p->pid;
            outMsg.value = (long)BASE_QUANTUM_NS;
            if (msgsnd(msgId, &outMsg, sizeof(outMsg) - sizeof(long), 0) == -1) {
                perror("msgsnd to worker");
                p->occupied = 0;
                totalFinished++;
                continue;
            }

            /* Block until worker replies (addressed to OSS's PID) */
            Message inMsg;
            if (msgrcv(msgId, &inMsg, sizeof(inMsg) - sizeof(long),
                       (long)getpid(), 0) == -1) {
                if (errno == EINTR) {
                    /* SIGALRM interrupted msgrcv - check the flag */
                    if (timeExpired) {
                        logWrite("OSS: 3-second real-time limit reached - terminating.\n");
                        printf("OSS: 3-second real-time limit reached - terminating.\n");
                    }
                    goto done;
                }
                perror("msgrcv from worker");
                p->occupied = 0;
                totalFinished++;
                continue;
            }

            /* Re-check wall clock immediately after msgrcv returns.
             * The simulated clock can run much faster than real time, so
             * many scheduling iterations complete before the top-of-loop
             * check fires. Checking here ensures we catch the 3s limit
             * even mid-dispatch on long runs. */
            if (timeExpired) {
                /* Put this process back so cleanup can kill it properly */
                enqueue(&readyQueue, idx);
                logWrite("OSS: 3-second real-time limit reached - terminating.\n");
                printf("OSS: 3-second real-time limit reached - terminating.\n");
                goto done;
            }

            long usedNs = inMsg.value;  /* positive = ran; negative = terminated */
            unsigned long long absUsed =
                (usedNs < 0) ? (unsigned long long)(-usedNs)
                             : (unsigned long long)  usedNs;

            /* Advance clock by the time the worker actually ran */
            addNano(&shm->clock, (unsigned int)absUsed);
            totalWorkerNs += absUsed;   /* track worker run time separately */

            logWrite("OSS: Receiving that process with PID %d ran for"
                     " %llu nanoseconds\n", p->localPid, absUsed);

            /* Update PCB service time */
            p->serviceTimeNano += (int)absUsed;
            while (p->serviceTimeNano >= 1000000000) {
                p->serviceTimeNano -= 1000000000;
                p->serviceTimeSeconds++;
            }

            /* ── Interpret the reply ── */
            if (usedNs < 0) {
                /* Negative → worker terminated */
                logWrite("OSS: Process P%d terminated after using %llu ns\n",
                         p->localPid, absUsed);
                int st;
                waitpid(p->pid, &st, 0);   /* reap zombie */
                p->occupied = 0;
                totalFinished++;

            } else if (absUsed < (unsigned long long)BASE_QUANTUM_NS) {
                /* Positive but < quantum → I/O block */
                logWrite("OSS: Process P%d not using its entire time quantum,"
                         " putting in blocked queue\n", p->localPid);
                p->blocked = 1;
                /* Calculate when this process becomes unblocked (100 ms later) */
                SimClock wake = shm->clock;
                addNano(&wake, BLOCKED_TIME_NS);
                p->eventWaitSec  = (int)wake.seconds;
                p->eventWaitNano = (int)wake.nanoseconds;
                enqueue(&blockedQueue, idx);
                /* Overhead: recording the block */
                addNano(&shm->clock, 1000);
                totalOverheadNs += 1000;

            } else {
                /* Positive == quantum → used full quantum, stays in rotation */
                logWrite("OSS: Putting process P%d into ready queue\n",
                         p->localPid);
                enqueue(&readyQueue, idx);
                /* Overhead: re-enqueue bookkeeping */
                addNano(&shm->clock, 500);
                totalOverheadNs += 500;
            }

        } else if (countActive() == 0 && totalLaunched >= opt_n) {
            /* Nothing running, nothing to launch - we are done */
            break;

        } else {
            /*
             * No process is ready (all blocked or waiting for launch interval).
             * Advance the clock by one quantum so we make progress toward
             * the next launch time or the next unblock time.
             */
            addNano(&shm->clock, BASE_QUANTUM_NS);
            totalOverheadNs += BASE_QUANTUM_NS;  /* idle time counts as overhead */
        }

        /* ── Print process table every 500 ms simulated time ── */
        nowNs = clockToNs(&shm->clock);
        if (nowNs - lastTablePrintNs >= TABLE_INTERVAL) {
            printProcessTable();
            lastTablePrintNs = nowNs;
        }

        totalClockNs = clockToNs(&shm->clock);
    }
    /* ═══════════════════ End of main loop ═══════════════════════ */

done:
    printFinalReport();
    cleanup();
    return 0;
}
