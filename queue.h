/*
 * queue.h
 * Author: student
 * Date: 2026-03-27
 * Description: Simple fixed-size circular FIFO queue used for the OSS
 *              ready queue and blocked queue.  Stores PCB table indices
 *              (0-based).  All operations are inline for performance.
 */

#ifndef QUEUE_H
#define QUEUE_H

#include "shared.h"

#define QUEUE_SIZE (MAX_PROCESSES + 1)  /* +1 so full and empty states differ */

typedef struct {
    int data[QUEUE_SIZE];
    int head;
    int tail;
    int count;
} Queue;

/* ── Initialise queue to empty ── */
static inline void queue_init(Queue *q) {
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
}

static inline int queue_empty(const Queue *q) { return q->count == 0; }
static inline int queue_full(const Queue *q)  { return q->count == QUEUE_SIZE; }
static inline int queue_size(const Queue *q)  { return q->count; }

/* Enqueue an index; returns 0 on success, -1 if full */
static inline int enqueue(Queue *q, int idx) {
    if (queue_full(q)) return -1;
    q->data[q->tail] = idx;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    return 0;
}

/* Dequeue and return the front index; returns -1 if empty */
static inline int dequeue(Queue *q) {
    if (queue_empty(q)) return -1;
    int val  = q->data[q->head];
    q->head  = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    return val;
}

/* Peek at the front without removing; returns -1 if empty */
static inline int peek(const Queue *q) {
    return queue_empty(q) ? -1 : q->data[q->head];
}

/*
 * Remove the first occurrence of idx from the queue.
 * Used when a process needs to be pulled out of the blocked queue.
 * Returns 0 if found and removed, -1 if not found.
 */
static inline int queue_remove(Queue *q, int idx) {
    if (queue_empty(q)) return -1;
    int tmp[QUEUE_SIZE];
    int n = 0, found = 0;
    int cur = q->head;
    for (int i = 0; i < q->count; i++) {
        if (q->data[cur] == idx && !found) {
            found = 1;   /* skip this element once */
        } else {
            tmp[n++] = q->data[cur];
        }
        cur = (cur + 1) % QUEUE_SIZE;
    }
    if (!found) return -1;
    queue_init(q);
    for (int i = 0; i < n; i++) enqueue(q, tmp[i]);
    return 0;
}

#endif /* QUEUE_H */
