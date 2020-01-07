#include "threadpool.h"
#include "err.h"
#include <stdio.h>
#include <signal.h>

typedef struct thread_pool_handler {
    pthread_mutex_t lock;
    int8_t terminate;
    list_t list;
    struct sigaction act;
    struct sigaction oldact;
    sigset_t sigint_block;
} thread_pool_handler_t;

thread_pool_handler_t tp_handler;

void thread_pool_handler_terminate(int sig, siginfo_t *siginfo, void *discard);

void set_sigint_block() {
    sigemptyset(&tp_handler.sigint_block);
    sigaddset(&tp_handler.sigint_block, SIGINT);
}

void set_sigaction() {
    tp_handler.act.sa_sigaction = thread_pool_handler_terminate;
    tp_handler.act.sa_flags = SA_SIGINFO;
    if (sigaction(SIGINT, &tp_handler.act, &tp_handler.oldact) != 0) syserr("sigaction error\n");
}

__attribute__((constructor))
void thread_pool_handler_init() {
    if (pthread_mutex_init(&tp_handler.lock, 0) != 0) syserr("pthread_mutex_init error\n");
    tp_handler.terminate = 0;
    list_init(&tp_handler.list);
    set_sigint_block();
    set_sigaction();
}

__attribute__((destructor))
void thread_pool_handler_destroy() {
    list_destroy(&tp_handler.list);
    if (pthread_mutex_destroy(&tp_handler.lock) != 0) syserr("pthread_mutex_destroy error\n");
}

void thread_pool_destroy_all() {
    void *pool = list_front(&tp_handler.list);
    while (pool) {
        thread_pool_destroy(pool);
        pool = list_front(&tp_handler.list);
    }
}

void thread_pool_handler_terminate(__attribute__((unused)) int sig, __attribute__((unused)) siginfo_t *siginfo,
                                   __attribute__((unused)) void *discard) {
    if (pthread_mutex_lock(&tp_handler.lock) != 0) syserr("pthread_mutex_lock error\n");
    tp_handler.terminate = 1;
    if (pthread_mutex_unlock(&tp_handler.lock) != 0) syserr("pthread_mutex_unlock error\n");

    thread_pool_destroy_all();
    thread_pool_handler_destroy();

    signal(SIGINT, tp_handler.oldact.sa_handler);
    raise(SIGINT);
}

int global_terminated() {
    if (pthread_mutex_lock(&tp_handler.lock) != 0) syserr("pthread_mutex_lock error\n");

    int8_t terminated = tp_handler.terminate;

    if (pthread_mutex_unlock(&tp_handler.lock) != 0) syserr("pthread_mutex_unlock error\n");

    return terminated;
}

void thread_pool_work(void *data) {
    if (pthread_sigmask(SIG_BLOCK, &tp_handler.sigint_block, 0) != 0) syserr("pthread_sigmask error/n");
    thread_pool_t *pool = (thread_pool_t *) data;

    if (pthread_mutex_lock(&pool->lock) != 0) syserr("pthread_mutex_lock error\n");

    while (!(pool->terminate || global_terminated()) || !list_is_empty(&pool->task_queue)) {
        while (!(pool->terminate || global_terminated()) && list_is_empty(&pool->task_queue)) {
            if (pthread_cond_wait(&pool->idle, &pool->lock) != 0) syserr("pthread_cond_wait error\n");
        }
        runnable_t *runnable = (runnable_t *) list_pop_front(&pool->task_queue);
        if (pthread_mutex_unlock(&pool->lock) != 0) syserr("pthread_mutex_unlock error\n");

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
    pool->terminate = 0;
    pool->num_threads = num_threads;

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) return -1;

    list_init(&pool->task_queue);

    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], 0, (void *) thread_pool_work, pool) != 0)
            syserr("pthread_create error\n");
    }

    if (list_push_back(&tp_handler.list, pool) != 0) {
        thread_pool_destroy(pool);
        return -1;
    }

    return 0;
}

void thread_pool_stop(thread_pool_t *pool) {
    if (pthread_mutex_lock(&pool->lock) != 0) syserr("pthread_mutex_lock error\n");

    pool->terminate = 1;
    if (pthread_cond_broadcast(&pool->idle) != 0) syserr("pthread_cond_broadcast error\n");

    if (pthread_mutex_unlock(&pool->lock) != 0) syserr("pthread_mutex_unlock error\n");
}

void thread_pool_destroy(thread_pool_t *pool) {
    thread_pool_stop(pool);
    list_erase(&tp_handler.list, pool);

    for (size_t i = 0; i < pool->num_threads; i++) {
        if (pthread_join(pool->threads[i], 0) != 0) syserr("pthread_join error\n");
    }

    if (pthread_cond_destroy(&pool->idle) != 0) syserr("pthread_cond_destroy error\n");
    if (pthread_mutex_destroy(&pool->lock) != 0) syserr("pthread_mutex_destroy error\n");

    list_destroy(&pool->task_queue);
    free(pool->threads);
}

int thread_pool_terminated(thread_pool_t *pool) {
    if (pthread_mutex_lock(&pool->lock) != 0) syserr("pthread_mutex_lock error\n");

    int terminated = pool->terminate;

    if (pthread_mutex_unlock(&pool->lock) != 0) syserr("pthread_mutex_unlock error\n");

    return terminated;
}

int defer(struct thread_pool *pool, runnable_t runnable) {
    if (global_terminated() || thread_pool_terminated(pool)) return -1;

    runnable_t *task = malloc(sizeof(runnable_t));
    if (!task) return -1;

    task->function = runnable.function;
    task->arg = runnable.arg;
    task->argsz = runnable.argsz;

    if (list_push_back(&pool->task_queue, (void *) task) != 0) {
        free(task);
        return -1;
    }
    if (pthread_cond_signal(&pool->idle) != 0) syserr("pthread_cond_signal error\n");

    return 0;
}
