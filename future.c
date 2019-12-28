#include "future.h"

typedef void *(*function_t)(void *);

typedef struct call_data {
    callable_t callable;
    future_t *future;
} call_data_t;

int future_init(future_t *future) {
    future->ready = 0;
    future->retval = NULL;
    pthread_mutex_init(&future->lock, 0);
    pthread_cond_init(&future->cond, 0);

    return 0;
}

void call(void *arg, __attribute__((unused)) size_t argsz) { //TODO except handling
    call_data_t *call_data = (call_data_t *) arg;
    size_t discard;
    pthread_mutex_lock(&call_data->future->lock);
    call_data->future->retval =
            (void*) call_data->callable.function(call_data->callable.arg, call_data->callable.argsz, &discard);
    call_data->future->ready = 1;
    pthread_cond_broadcast(&call_data->future->cond);
    pthread_mutex_unlock(&call_data->future->lock);
    free(call_data);
}

int async(thread_pool_t *pool, future_t *future, callable_t callable) { //TODO except handling
    future_init(future);

    call_data_t *call_data = malloc(sizeof(call_data_t));
    call_data->future = future;
    call_data->callable = callable;

    return defer(pool, (runnable_t){.function = call, .arg = call_data, .argsz = sizeof(call_data)});
}

void *await(future_t *future) { //TODO exception handling
    pthread_mutex_lock(&future->lock);
    while (!future->ready) {
        pthread_cond_wait(&future->cond, &future->lock);
    }
    pthread_mutex_unlock(&future->lock);

    return future->retval;
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) { //TODO exception handling, ogarnac to
    future_init(future);
    void *result = await(from);

    call_data_t *call_data = malloc(sizeof(call_data_t));
    call_data->future = future;
    call_data->callable = (callable_t){.function = function, .arg = result, .argsz = sizeof(result)};

    return defer(pool, (runnable_t){.function = call, .arg = call_data, .argsz = sizeof(call_data)});
}


