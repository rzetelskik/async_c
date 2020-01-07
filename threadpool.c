#include "threadpool.h"
#include "err.h"
#include <stdio.h>
#include <signal.h>
        
pthread_rwlock_t no_defer_lock;
int8_t no_defer;
pthread_t threadpool_manager;
list_t threadpool_list;
sigset_t block_mask;

void threadpool_manager_work(void *discard);

__attribute__((constructor))
void threadpool_manager_init() {
    no_defer = 0;
    if (sigemptyset(&block_mask) != 0) syserr("sigemptyset error\n");
    if (sigaddset(&block_mask, SIGINT) != 0) syserr("sigaddset error\n");
    if (pthread_rwlock_init(&no_defer_lock, 0) != 0) syserr("pthread_rwlock_init error\n");
    if (sigprocmask(SIG_BLOCK, &block_mask, 0) != 0) syserr("sigprocmask error\n");
    list_init(&threadpool_list);
    if (pthread_create(&threadpool_manager, 0, (void*) threadpool_manager_work, NULL) != 0)
        syserr("pthread_create error\n");
    printf("constructor finished\n");
}

__attribute__((destructor))
void thread_pool_handler_destroy() {
    list_destroy(&threadpool_list);
    if (pthread_cancel(threadpool_manager) != 0) syserr("pthread_cancel error\n");
    if (pthread_join(threadpool_manager, 0) != 0) syserr("pthread_join error\n");
    if (pthread_rwlock_destroy(&no_defer_lock) != 0) syserr("pthread_rwlock_destroy error\n");
}

void thread_pool_destroy_all() {
    void *pool = list_front(&threadpool_list);
    while (pool) {
        thread_pool_destroy(pool);
        pool = list_front(&threadpool_list);
    }
}

void threadpool_manager_work(__attribute__((unused)) void *discard) {
    int sig_discard;
//    if (pthread_sigmask(SIG_UNBLOCK, &block_mask, 0) != 0) syserr("pthread_sigmask error\n");
    if (sigwait(&block_mask, &sig_discard) != 0) syserr("sigwait error\n");
    printf("got sigint\n");
//    if (pthread_sigmask(SIG_BLOCK, &block_mask, 0) != 0) syserr("pthread_sigmask error\n");

    if (pthread_rwlock_wrlock(&no_defer_lock) != 0) syserr("pthread_rwlock_wrlock error\n");
    no_defer = 1;
    if (pthread_rwlock_unlock(&no_defer_lock) != 0) syserr("pthread_rwlock_unlock error\n");

    thread_pool_destroy_all();
    thread_pool_handler_destroy();
    exit(SIGINT);
}

int get_no_defer() {
    if (pthread_rwlock_rdlock(&no_defer_lock) != 0) syserr("pthread_rwlock_rdlock error\n");
    int retval = no_defer;
    if (pthread_rwlock_unlock(&no_defer_lock) != 0) syserr("pthread_rwlock_unlock error\n");

    return retval;
}

void thread_pool_work(void *data) {
    thread_pool_t *pool = (thread_pool_t *) data;

    if (pthread_mutex_lock(&pool->lock) != 0) syserr("pthread_mutex_lock error\n");

    while (!(pool->terminate || get_no_defer()) || !list_is_empty(&pool->task_queue)) {
        while (!(pool->terminate || get_no_defer()) && list_is_empty(&pool->task_queue)) {
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
        if (pthread_create(&pool->threads[i], 0, (void*) thread_pool_work, pool) != 0)
            syserr("pthread_create error\n");
    }

    if (list_push_back(&threadpool_list, pool) != 0) {
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
    list_erase(&threadpool_list, pool);

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
    if (get_no_defer() || thread_pool_terminated(pool)) return -1;

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
