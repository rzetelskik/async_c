#include "queue.h"
#include "err.h"
#include <stdlib.h>

typedef struct queue_node {
    struct queue_node *prev;
    struct queue_node *next;
    void *elem;
} queue_node_t;

void queue_init(queue_t *queue) {
    if (pthread_mutex_init(&queue->lock, 0) != 0) syserr("pthread_mutex_init error\n");

    queue->head = queue->tail = NULL;
}

void queue_node_init(queue_node_t *queue_node, void *elem) {
    queue_node->elem = elem;
    queue_node->next = queue_node->prev = NULL;
}

int queue_push_back(queue_t *queue, void *elem) {
    queue_node_t *new_node = malloc(sizeof(queue_node_t));
    if (!new_node) return 1;

    queue_node_init(new_node, elem);

    if (pthread_mutex_lock(&queue->lock) != 0) syserr("pthread_mutex_lock error\n");

    if (queue->head) {
        queue->tail->next = new_node;
        new_node->prev = queue->tail;
    } else {
        queue->head = new_node;
    }
    queue->tail = new_node;

    if (pthread_mutex_unlock(&queue->lock) != 0) syserr("pthread_mutex_unlock error\n");

    return 0;
}

void *queue_pop_front(queue_t *queue) {
    void *retval = NULL;

    if (pthread_mutex_lock(&queue->lock) != 0) syserr("pthread_mutex_lock error\n");
    queue_node_t *node = queue->head;

    if (node) {
        retval = node->elem;
        if (node->next) {
            queue->head = node->next;
            queue->head->prev = NULL;
        } else {
            queue->head = queue->tail = NULL;
        }
    }

    if (pthread_mutex_unlock(&queue->lock) != 0) syserr("pthread_mutex_unlock error\n");
    free(node);

    return retval;
}

int queue_is_empty(queue_t *queue) {
    if (pthread_mutex_lock(&queue->lock) != 0) syserr("pthread_mutex_lock error\n");
    int is_empty = !queue->head;
    if (pthread_mutex_unlock(&queue->lock) != 0) syserr("pthread_mutex_unlock error\n");

    return is_empty;
}

void queue_destroy(queue_t *queue) {
    if (pthread_mutex_lock(&queue->lock) != 0) syserr("pthread_mutex_lock error\n");

    queue_node_t *node = queue->head, *tmp;
    while (node) {
        tmp = node->next;
        free(node->elem);
        free(node);
        node = tmp;
    }

    if (pthread_mutex_unlock(&queue->lock) != 0) syserr("pthread_mutex_unlock error\n");
    if (pthread_mutex_destroy(&queue->lock) != 0) syserr("pthread_mutex_destroy error\n");
}