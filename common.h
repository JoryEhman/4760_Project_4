#ifndef COMMON_H
#define COMMON_H

#define BILLION 1000000000ULL

#define MAX_PROCESSES 20
#define QUEUE_SIZE 20

#define QUANTUM 25000000   /* 25ms */
#define DISPATCH_TIME 10000

/* ftok file path (must exist!) */
#define FTOK_PATH "./oss.c"
#define SHM_PROJ_ID 'S'
#define MSG_PROJ_ID 'M'

typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimClock;

typedef struct {
    int occupied;
    pid_t pid;
    int blocked;

    unsigned int startSeconds;
    unsigned int startNano;

    unsigned int serviceTimeSeconds;
    unsigned int serviceTimeNano;

    unsigned int eventWaitSec;
    unsigned int eventWaitNano;
} PCB;

typedef struct {
    long mtype;
    int data;
} Message;

#endif