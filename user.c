/*
* Name:
 * Date:
 * Project: CS 4760 Project 4
 * File: user.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>

#include "common.h"

int main(int argc, char *argv[]) {

    int shm_id, msg_id;
    SimClock *clock;
    Message msg;

    /* Attach shared memory */
    shm_id = shmget(SHM_KEY, sizeof(SimClock), 0666);
    if (shm_id == -1) {
        perror("user shmget");
        exit(1);
    }

    clock = (SimClock *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
        perror("user shmat");
        exit(1);
    }

    /* Connect to message queue */
    msg_id = msgget(MSG_KEY, 0666);
    if (msg_id == -1) {
        perror("user msgget");
        exit(1);
    }

    printf("User %d: waiting for message...\n", getpid());

    /* WAIT for OSS */
    if (msgrcv(msg_id, &msg, sizeof(msg.data), getpid(), 0) == -1) {
        perror("msgrcv");
        exit(1);
    }

    printf("User %d: received message from OSS\n", getpid());

    /* Simulate work */
    printf("User %d: running...\n", getpid());

    /* Send response back */
    msg.mtype = 1;  // OSS listens on type 1
    msg.data = getpid();

    if (msgsnd(msg_id, &msg, sizeof(msg.data), 0) == -1) {
        perror("msgsnd");
        exit(1);
    }

    printf("User %d: finished and sent response\n", getpid());

    shmdt(clock);
    return 0;
}