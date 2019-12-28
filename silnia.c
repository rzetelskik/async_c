#include "future.h"
#include <stdio.h>

typedef struct iter {
    u_int64_t k;
    u_int64_t retval;
} iter_t;

void *multiply(void *arg, __attribute__((unused)) size_t size, __attribute__((unused)) size_t* retsz) {
    iter_t *iter = (iter_t *) arg;
    iter->retval *= iter->k++;
    return iter;
}

int main() {
    thread_pool_t pool;
    thread_pool_init(&pool, 3);

    u_int64_t n;
    future_t future[2];
    int8_t curr = 0;
    iter_t iter = {.k = 1, .retval = 1};

    scanf("%ld", &n);

    async(&pool, &future[curr], (callable_t){.function = multiply, .arg = &iter, .argsz = sizeof(iter_t)});
    while (iter.k < n) { //TODO change this
        curr ^= 1;
        map(&pool, &future[curr], &future[curr ^ 1], multiply);
    }

    await(&future[curr]);

    printf("%lu\n", iter.retval);

    thread_pool_destroy(&pool);
    return 0;
}