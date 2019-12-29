#include "threadpool.h"
#include <stdio.h>

#define err_exit(err) {perror(err); exit(1);}

typedef struct queue_node {
    struct queue_node *prev;
    struct queue_node *next;
    void *elem;
} queue_node_t;

typedef struct queue {
    pthread_mutex_t lock;
    queue_node_t *head;
    queue_node_t *tail;
} queue_t;

void queue_init(queue_t *queue) {
    if (pthread_mutex_init(&queue->lock, 0) != 0) err_exit("pthread_mutex_init error");

    queue->head = queue->tail = NULL;
}

void queue_node_init(queue_node_t *queue_node, void *elem) {
    queue_node->elem = elem;
    queue_node->next = queue_node->prev = NULL;
}

int queue_push(queue_t *queue, void *elem) {
    queue_node_t *new_node = malloc(sizeof(queue_node_t));
    if (!new_node) return 1;

    queue_node_init(new_node, elem);

    if (pthread_mutex_lock(&queue->lock) != 0) err_exit("pthread_mutex_lock error")

    if (queue->head) {
        queue->tail->next = new_node;
        new_node->prev = queue->tail;
    } else {
        queue->head = new_node;
    }
    queue->tail = new_node;

    if (pthread_mutex_unlock(&queue->lock) != 0) err_exit("pthread_mutex_unlock error");

    return 0;
}

void *queue_pull(queue_t *queue) {
    void *retval = NULL;

    if (pthread_mutex_lock(&queue->lock) != 0) err_exit("pthread_mutex_lock error");
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

    if (pthread_mutex_unlock(&queue->lock) != 0) err_exit("pthread_mutex_unlock error");
    free(node);

    return retval;
}

int queue_is_empty(queue_t *queue) {
    if (pthread_mutex_lock(&queue->lock) != 0) err_exit("pthread_mutex_lock error");
    int is_empty = !queue->head;
    if (pthread_mutex_unlock(&queue->lock) != 0) err_exit("pthread_mutex_unlock error");

    return is_empty;
}

void queue_destroy(queue_t *queue) {
    if (pthread_mutex_lock(&queue->lock) != 0) err_exit("pthread_mutex_lock error");

    queue_node_t *node = queue->head, *tmp;
    while (node) {
        tmp = node->next;
        free(node->elem);
        free(node);
        node = tmp;
    }

    if (pthread_mutex_unlock(&queue->lock) != 0) err_exit("pthread_mutex_unlock error");
    if (pthread_mutex_destroy(&queue->lock) != 0) err_exit("pthread_mutex_destroy error");
}

void work_thread(void *data) {
    //TODO signals
    thread_pool_t *pool = (thread_pool_t *) data;

    if (pthread_mutex_lock(&pool->lock) != 0) err_exit("pthread_mutex_lock error");
    while (!pool->stop) {
        while (!pool->stop && queue_is_empty(pool->task_queue)) {
            if (pthread_cond_wait(&pool->idle, &pool->lock) != 0) err_exit("pthread_cond_wait error");
        }
        if (pool->stop) break;
        if (pthread_mutex_unlock(&pool->lock) != 0) err_exit("pthread_mutex_unlock error");

        runnable_t *runnable = (runnable_t *) queue_pull(pool->task_queue);
        if (runnable) {
            runnable->function(runnable->arg, runnable->argsz);
            free(runnable);
        }

        if (pthread_mutex_lock(&pool->lock) != 0) err_exit("pthread_mutex_lock error");
    }
    if (pthread_mutex_unlock(&pool->lock) != 0) err_exit("pthread_mutex_unlock error");
}

int thread_pool_init(thread_pool_t *pool, size_t num_threads) {
    pool->stop = 0;
    pool->num_threads = num_threads;

    if (pthread_mutex_init(&pool->lock, 0) != 0) err_exit("pthread_mutex_init error");
    if (pthread_cond_init(&pool->idle, 0) != 0) err_exit("pthread_cond_init error");
    if (pthread_attr_init(&pool->pthread_attr) != 0) err_exit("pthread_attr_init error");
    if (pthread_attr_setdetachstate(&pool->pthread_attr, PTHREAD_CREATE_JOINABLE) != 0) err_exit(
            "pthread_attr_setdetachstate error");

    pool->task_queue = malloc(sizeof(queue_t));
    if (!pool->task_queue) return 1;

    queue_init(pool->task_queue);

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool->task_queue);
        return 1;
    }

    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], &pool->pthread_attr, (void *) work_thread, pool) != 0)
            err_exit("pthread_create error");
    }

    return 0;
}

void thread_pool_stop(thread_pool_t *pool) {
    if (pthread_mutex_lock(&pool->lock) != 0) err_exit("pthread_mutex_lock error");
    pool->stop = 1;
    if (pthread_cond_broadcast(&pool->idle) != 0) err_exit("pthread_cond_broadcast error");
    if (pthread_mutex_unlock(&pool->lock) != 0) err_exit("pthread_mutex_unlock error");
}

void thread_pool_destroy(thread_pool_t *pool) {
    thread_pool_stop(pool);

    for (size_t i = 0; i < pool->num_threads; i++) {
        if (pthread_join(pool->threads[i], 0) != 0) err_exit("pthread_join error");
    }
    if (pthread_attr_destroy(&pool->pthread_attr) != 0) err_exit("pthread_attr_destroy error");
    if (pthread_mutex_destroy(&pool->lock) != 0) err_exit("pthread_mutex_destroy error");

    queue_destroy(pool->task_queue);
    free(pool->task_queue);
    free(pool->threads);
}

int defer(struct thread_pool *pool, runnable_t runnable) { //TODO block after signal
    runnable_t *task = malloc(sizeof(runnable_t));
    if (!task) return 1;

    task->function = runnable.function;
    task->arg = runnable.arg;
    task->argsz = runnable.argsz;

    if (queue_push(pool->task_queue, (void *) task) != 0) {
        free(task);
        return 1;
    }

    if (pthread_cond_signal(&pool->idle) != 0) err_exit("pthread_cond_signal error");

    return 0;
}
