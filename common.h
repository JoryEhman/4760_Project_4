#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

/* Keys (temporary — will switch to ftok later) */
#define SHM_KEY 0x1234
#define MSG_KEY 0x2345

#define BILLION 1000000000

/* Simulated clock */
typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimClock;

/* Process Control Block */
typedef struct {
    int occupied;
    pid_t pid;

    int startSeconds;
    int startNano;

    int serviceTimeSeconds;
    int serviceTimeNano;

    int blocked;
} PCB;

/* Message structure */
typedef struct {
    long mtype;
    int data;
} Message;

#endif