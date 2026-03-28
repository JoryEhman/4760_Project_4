/*
 * Name:
 * Date:
 * Project: CS 4760 Project 4
 * File: oss.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

#include "common.h"

/* Global resources */
int shm_id = -1;
int msg_id = -1;
SimClock *clock = NULL;

PCB processTable[20];

FILE *logFile = NULL;

/* Cleanup function */
void cleanup(int sig) {
    int i;

    printf("Cleaning up...\n");

    for (i = 0; i < 20; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0);

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
    }
}

int main(int argc, char *argv[]) {

    signal(SIGINT, cleanup);

    logFile = fopen("log.txt", "w");

    init_clock();
    init_msg_queue();
    init_process_table();

    printf("OSS started\n");

    /* Main loop placeholder */
    while (1) {

        /* Scheduler logic will go here */

        break;  /* temporary */
    }

    cleanup(0);
    return 0;
}