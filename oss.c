/*
 * Name:
 * Date:
 * Project: CS 4760 Project 4
 * File: oss.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

#include "common.h"

/* Options struct */
typedef struct {
    int n;
    int s;
    double t;
    double i;
    char logfile[100];
} Options;

/* Global resources */
int shm_id = -1;
int msg_id = -1;
SimClock *clock = NULL;

PCB processTable[20];

FILE *logFile = NULL;

/* Cleanup function */
void cleanup(int sig) {
    int i;
    (void)sig;

    printf("Cleaning up...\n");

    for (i = 0; i < 20; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    if (clock != NULL)
        shmdt(clock);

    if (shm_id != -1)
        shmctl(shm_id, IPC_RMID, NULL);

    if (msg_id != -1)
        msgctl(msg_id, IPC_RMID, NULL);

    if (logFile != NULL)
        fclose(logFile);

    exit(0);
}

/* Parse command line options */
void parse_options(int argc, char *argv[], Options *opt) {
    int option;

    opt->n = 5;
    opt->s = 2;
    opt->t = 2.0;
    opt->i = 0.1;
    strcpy(opt->logfile, "log.txt");

    while ((option = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (option) {
            case 'h':
                printf("Usage: oss [-h] [-n proc] [-s simul] [-t timelimit] [-i interval] [-f logfile]\n");
                exit(0);
            case 'n':
                opt->n = atoi(optarg);
                break;
            case 's':
                opt->s = atoi(optarg);
                break;
            case 't':
                opt->t = atof(optarg);
                break;
            case 'i':
                opt->i = atof(optarg);
                break;
            case 'f':
                strncpy(opt->logfile, optarg, sizeof(opt->logfile) - 1);
                opt->logfile[sizeof(opt->logfile) - 1] = '\0';
                break;
            default:
                fprintf(stderr, "Invalid argument\n");
                exit(1);
        }
    }

    if (opt->n <= 0 || opt->s <= 0) {
        fprintf(stderr, "Error: n and s must be > 0\n");
        exit(1);
    }

    if (opt->n > 20) {
        fprintf(stderr, "Error: n must be <= 20\n");
        exit(1);
    }

    if (opt->s > opt->n) {
        opt->s = opt->n;
    }
}

/* Initialize shared memory clock */
void init_clock() {
    shm_id = shmget(SHM_KEY, sizeof(SimClock), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget");
        exit(1);
    }

    clock = (SimClock *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
        perror("shmat");
        exit(1);
    }

    clock->seconds = 0;
    clock->nanoseconds = 0;
}

/* Initialize message queue */
void init_msg_queue() {
    msg_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msg_id == -1) {
        perror("msgget");
        exit(1);
    }
}

/* Initialize PCB table */
void init_process_table() {
    int i;
    for (i = 0; i < 20; i++) {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
        processTable[i].serviceTimeSeconds = 0;
        processTable[i].serviceTimeNano = 0;
        processTable[i].blocked = 0;
    }
}

/* Find free PCB slot */
int find_free_slot() {
    int i;

    for (i = 0; i < 20; i++) {
        if (!processTable[i].occupied) {
            return i;
        }
    }

    return -1;
}

/* Clear PCB slot */
void clear_pcb(int index) {
    processTable[index].occupied = 0;
    processTable[index].pid = 0;
    processTable[index].startSeconds = 0;
    processTable[index].startNano = 0;
    processTable[index].serviceTimeSeconds = 0;
    processTable[index].serviceTimeNano = 0;
    processTable[index].blocked = 0;
}

int main(int argc, char *argv[]) {
    Options opt;
    int launched = 0;
    int running = 0;
    int finished = 0;

    signal(SIGINT, cleanup);

    parse_options(argc, argv, &opt);

    logFile = fopen(opt.logfile, "w");
    if (logFile == NULL) {
        perror("fopen");
        exit(1);
    }

    init_clock();
    init_msg_queue();
    init_process_table();

    printf("OSS started with:\n");
    printf("-n %d -s %d -t %.2f -i %.2f -f %s\n",
           opt.n, opt.s, opt.t, opt.i, opt.logfile);

    Message msg;

    while (launched < opt.n || running > 0) {

        /* Launch children (same as before) */
        while (launched < opt.n && running < opt.s) {
            pid_t pid;
            int slot;

            slot = find_free_slot();
            if (slot == -1) break;

            pid = fork();

            if (pid < 0) {
                perror("fork failed");
                cleanup(1);
            }

            if (pid == 0) {
                execl("./user", "user", (char *)NULL);
                perror("exec failed");
                exit(1);
            } else {
                processTable[slot].occupied = 1;
                processTable[slot].pid = pid;

                printf("OSS: created child %d in slot %d\n", pid, slot);

                launched++;
                running++;
            }
        }

        /* 🔥 NEW: send message to ONE child */
        int i;
        for (i = 0; i < 20; i++) {
            if (processTable[i].occupied) {

                msg.mtype = processTable[i].pid;
                msg.data = 1;

                printf("OSS: scheduling PID %d\n", processTable[i].pid);

                msgsnd(msg_id, &msg, sizeof(msg.data), 0);

                break; // only one process at a time
            }
        }

        /* 🔥 Wait for response */
        if (msgrcv(msg_id, &msg, sizeof(msg.data), 1, 0) == -1) {
            perror("msgrcv OSS");
            cleanup(1);
        }

        printf("OSS: received response from PID %d\n", msg.data);

        /* 🔥 Now reap that process */
        pid_t ended = waitpid(msg.data, NULL, 0);

        for (i = 0; i < 20; i++) {
            if (processTable[i].occupied && processTable[i].pid == ended) {
                clear_pcb(i);
                break;
            }
        }

        running--;
        finished++;
    }

    printf("OSS summary: launched %d, finished %d\n", launched, finished);
    fprintf(logFile, "OSS summary: launched %d, finished %d\n", launched, finished);

    cleanup(0);
    return 0;
}