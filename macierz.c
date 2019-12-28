#include "future.h"
#include <stdio.h>
#include <unistd.h> //TODO

typedef struct cell_data {
    u_int64_t time;
    int64_t retval;
} cell_data_t;

void *calc_cell(void *arg, __attribute__((unused)) size_t size, __attribute__((unused)) size_t* retsz) {
    cell_data_t *cell_data = (cell_data_t *) arg;
    usleep(1000 * cell_data->time);
    return cell_data;
}

int main() { //TODO exception handling
    thread_pool_t pool;
    thread_pool_init(&pool, 4);

    u_int64_t k, n;
    scanf("%lu %lu", &k, &n);

    future_t futures[k][n];

    for (u_int64_t i = 0; i < k; i++) {
        for (u_int64_t j = 0; j < n; j++) {
            cell_data_t *cell_data = malloc(sizeof(cell_data_t));
            scanf("%ld %lu", &cell_data->retval, &cell_data->time);
            async(&pool, &futures[i][j], (callable_t){.function = calc_cell,
                                                            .arg = cell_data,
                                                            .argsz = 0});
        }
    }

    for (u_int64_t i = 0; i < k; i++) {
        int64_t retval = 0;
        for (u_int64_t j = 0; j < n; j++) {
            cell_data_t *cell_data = (cell_data_t *) await(&futures[i][j]);
            retval += cell_data->retval;
            free(cell_data);
        }
        printf("%ld\n", retval);
    }

    thread_pool_destroy(&pool);
    return 0;
}