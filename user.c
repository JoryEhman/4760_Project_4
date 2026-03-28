#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>

#include "common.h"

int main(int argc, char *argv[]) {

    int shm_id;
    SimClock *clock;

    /* Attach to shared memory */
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

    printf("User process started\n");

    /* Placeholder loop */
    while (1) {
        break;
    }

    shmdt(clock);
    return 0;
}