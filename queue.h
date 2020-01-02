#ifndef ASYNC_QUEUE_H
#define ASYNC_QUEUE_H

#include <pthread.h>

typedef struct queue {
    pthread_mutex_t lock;
    struct queue_node *head;
    struct queue_node *tail;
} queue_t;

void queue_init(queue_t *queue);

int queue_push(queue_t *queue, void *elem);

void *queue_pull(queue_t *queue);

int queue_is_empty(queue_t *queue);

void queue_destroy(queue_t *queue);



#endif //ASYNC_QUEUE_H
