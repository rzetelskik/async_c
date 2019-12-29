#include "future.h"
#include <stdio.h>

#define err_exit(err) {perror(err); exit(1);}

typedef struct call_data {
    callable_t callable;
    future_t *future;
} call_data_t;

void future_init(future_t *future) {
    if (pthread_mutex_init(&future->lock, 0) != 0) err_exit("pthread_mutex_init error");
    if (pthread_cond_init(&future->cond, 0) != 0) err_exit("pthread_cond_init error");

    future->ready = 0;
    future->retval = NULL;
}

void call(void *arg, __attribute__((unused)) size_t argsz) {
    call_data_t *call_data = (call_data_t *) arg;
    size_t discard;

    if (pthread_mutex_lock(&call_data->future->lock) != 0) err_exit("pthread_mutex_lock error");

    call_data->future->retval =
            (void*) call_data->callable.function(call_data->callable.arg, call_data->callable.argsz, &discard);
    call_data->future->ready = 1;

    if (pthread_cond_broadcast(&call_data->future->cond) != 0) err_exit("pthread_cond_broadcast error");
    if (pthread_mutex_unlock(&call_data->future->lock) != 0) err_exit("pthread_mutex_unlock error");
    free(call_data);
}

int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    call_data_t *call_data = malloc(sizeof(call_data_t));
    if (!call_data) return 1;

    future_init(future);
    call_data->future = future;
    call_data->callable = callable;

    return defer(pool, (runnable_t){.function = call, .arg = call_data, .argsz = sizeof(call_data)});
}

void *await(future_t *future) {
    if (pthread_mutex_lock(&future->lock) != 0) err_exit("pthread_mutex_lock error");

    while (!future->ready) {
        if (pthread_cond_wait(&future->cond, &future->lock) != 0) err_exit("pthread_cond_wait error");
    }

    if (pthread_mutex_unlock(&future->lock) != 0) err_exit("pthread_mutex_unlock error");
    //TODO destroy resources

    return future->retval;
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
    void *result = await(from);

    call_data_t *call_data = malloc(sizeof(call_data_t));
    if (!call_data) return 1;

    future_init(future);
    call_data->future = future;
    call_data->callable = (callable_t){.function = function, .arg = result, .argsz = sizeof(result)};

    return defer(pool, (runnable_t){.function = call, .arg = call_data, .argsz = sizeof(call_data)});
}


