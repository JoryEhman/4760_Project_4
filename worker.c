/*
 * worker.c
 * Author: student
 * Date: 2026-03-27
 * Environment: Linux, gcc
 *
 * Description:
 *   Simulated user process for the OSS round-robin scheduler.
 *   - Receives its total cpu-burst time (ns) as argv[1].
 *   - Waits on the shared message queue for a time quantum from OSS.
 *   - Each scheduling round, decides one of three outcomes:
 *       1. Terminate  — if remaining burst <= quantum (sends negative value)
 *       2. I/O block  — 20% chance; uses random portion of quantum (sends
 *                       positive value < quantum)
 *       3. Full run   — uses entire quantum (sends value == quantum)
 *   - RNG seeded from PID so each process produces independent decisions.
 *   - Uses ftok() to derive the same IPC keys as OSS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "shared.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "worker: usage: worker <cpuBurstNs>\n");
        return 1;
    }

    unsigned long long totalBurstNs = strtoull(argv[1], NULL, 10);
    unsigned long long usedNs       = 0;   /* cpu time consumed so far */

    /* Seed RNG uniquely per process so blocking decisions are independent */
    srand((unsigned)(time(NULL) ^ ((unsigned long)getpid() * 7919UL)));

    /* ── Attach to the message queue created by OSS ── */
    key_t mqKey = ftok(SHM_KEY_PATH, MQ_OSS_KEY_ID);
    if (mqKey == -1) { perror("worker: ftok mq"); return 1; }

    int msgId = msgget(mqKey, 0);
    if (msgId == -1) { perror("worker: msgget"); return 1; }

    pid_t myPid     = getpid();
    pid_t parentPid = getppid();   /* OSS's PID — used as reply mtype */

    /* ── Main scheduling loop ── */
    while (1) {

        /* Wait for a quantum message addressed specifically to this process */
        Message inMsg;
        if (msgrcv(msgId, &inMsg, sizeof(inMsg) - sizeof(long),
                   (long)myPid, 0) == -1) {
            perror("worker: msgrcv");
            return 1;
        }

        unsigned long long quantum = (unsigned long long)inMsg.value;

        /* ── Outcome 1: terminate if burst is used up ── */
        if (usedNs + quantum >= totalBurstNs) {
            unsigned long long remaining = totalBurstNs - usedNs;
            if (remaining == 0) remaining = 1; /* send at least 1 ns */
            usedNs += remaining;

            Message outMsg;
            outMsg.mtype = (long)parentPid;
            outMsg.value = -(long)remaining;   /* negative signals termination */
            msgsnd(msgId, &outMsg, sizeof(outMsg) - sizeof(long), 0);
            break;
        }

        /* ── Outcome 2 or 3: 20% chance of I/O block, else full quantum ── */
        int willBlock = ((rand() % 100) < 20);

        Message outMsg;
        outMsg.mtype = (long)parentPid;

        if (willBlock) {
            /* Use a random amount of the quantum (at least 1 ns, less than full) */
            unsigned long long used =
                (unsigned long long)(rand() % (int)quantum) + 1;
            if (used >= quantum) used = quantum - 1;
            if (used == 0)       used = 1;
            usedNs       += used;
            outMsg.value  = (long)used;   /* positive but < quantum → I/O block */
        } else {
            /* Use the entire quantum */
            usedNs       += quantum;
            outMsg.value  = (long)quantum; /* positive == quantum → full run */
        }

        if (msgsnd(msgId, &outMsg, sizeof(outMsg) - sizeof(long), 0) == -1) {
            perror("worker: msgsnd");
            return 1;
        }
    }

    return 0;
}
