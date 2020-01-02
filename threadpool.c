#include "threadpool.h"
#include "err.h"
#include <stdio.h>

void work_thread(void *data) {
    //TODO signals
    thread_pool_t *pool = (thread_pool_t *) data;

    if (pthread_mutex_lock(&pool->lock) != 0) syserr("pthread_mutex_lock error\n");
    while (!pool->stop) {
        while (!pool->stop && queue_is_empty(&pool->task_queue)) {
            if (pthread_cond_wait(&pool->idle, &pool->lock) != 0) syserr("pthread_cond_wait error\n");
        }
        if (pool->stop) break;
        if (pthread_mutex_unlock(&pool->lock) != 0) syserr("pthread_mutex_unlock error\n");

        runnable_t *runnable = (runnable_t *) queue_pull(&pool->task_queue);
        if (runnable) {
            runnable->function(runnable->arg, runnable->argsz);
            free(runnable);
        }

        if (pthread_mutex_lock(&pool->lock) != 0) syserr("pthread_mutex_lock error\n");
    }
    if (pthread_mutex_unlock(&pool->lock) != 0) syserr("pthread_mutex_unlock error\n");
}

int thread_pool_init(thread_pool_t *pool, size_t num_threads) {
    if (pthread_mutex_init(&pool->lock, 0) != 0) syserr("pthread_mutex_init error\n");
    if (pthread_cond_init(&pool->idle, 0) != 0) syserr("pthread_cond_init error\n");
    pool->stop = 0;
    pool->num_threads = num_threads;

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) return -1;

    queue_init(&pool->task_queue);

    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], 0, (void *) work_thread, pool) != 0)
            syserr("pthread_create error\n");
    }

    return 0;
}

void thread_pool_stop(thread_pool_t *pool) {
    if (pthread_mutex_lock(&pool->lock) != 0) syserr("pthread_mutex_lock error\n");
    pool->stop = 1;
    if (pthread_cond_broadcast(&pool->idle) != 0) syserr("pthread_cond_broadcast error\n");
    if (pthread_mutex_unlock(&pool->lock) != 0) syserr("pthread_mutex_unlock error\n");
}

void thread_pool_destroy(thread_pool_t *pool) {
    thread_pool_stop(pool);

    for (size_t i = 0; i < pool->num_threads; i++) {
        if (pthread_join(pool->threads[i], 0) != 0) syserr("pthread_join error\n");
    }
    if (pthread_cond_destroy(&pool->idle) != 0) syserr("pthread_cond_destroy error\n");
    if (pthread_mutex_destroy(&pool->lock) != 0) syserr("pthread_mutex_destroy error\n");

    queue_destroy(&pool->task_queue);
    free(pool->threads);
}

int defer(struct thread_pool *pool, runnable_t runnable) { //TODO block after signal
    runnable_t *task = malloc(sizeof(runnable_t));
    if (!task) return -1;

    task->function = runnable.function;
    task->arg = runnable.arg;
    task->argsz = runnable.argsz;

    if (queue_push(&pool->task_queue, (void *) task) != 0) {
        free(task);
        return -1;
    }

    if (pthread_cond_signal(&pool->idle) != 0) syserr("pthread_cond_signal error\n");

    return 0;
}
