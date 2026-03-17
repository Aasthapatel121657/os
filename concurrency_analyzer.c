#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define BUFFER_SIZE   8192
#define TOTAL_ITEMS   2000000LL
#define POISON        (-1LL)
#define N_PRODUCERS   4
#define N_CONSUMERS   4

typedef struct {
    int64_t value;
    uint64_t produced_at;
} Item;

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* ────────────────────────────────────────
   THREAD VERSION
──────────────────────────────────────── */

typedef struct {
    Item buffer[BUFFER_SIZE];
    int head, tail, count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    int finished;
} ThreadPC;

static void thread_pc_init(ThreadPC *pc) {
    memset(pc, 0, sizeof(*pc));
    pthread_mutex_init(&pc->mutex, NULL);
    pthread_cond_init(&pc->not_full, NULL);
    pthread_cond_init(&pc->not_empty, NULL);
}

static void* thread_producer(void *arg) {
    ThreadPC *pc = arg;
    for (int64_t i = 1; i <= TOTAL_ITEMS / N_PRODUCERS; i++) {
        pthread_mutex_lock(&pc->mutex);
        while (pc->count == BUFFER_SIZE && !pc->finished)
            pthread_cond_wait(&pc->not_full, &pc->mutex);

        if (pc->finished) {
            pthread_mutex_unlock(&pc->mutex);
            return NULL;
        }

        pc->buffer[pc->tail].value = i;
        pc->buffer[pc->tail].produced_at = get_time_ns();
        pc->tail = (pc->tail + 1) % BUFFER_SIZE;
        pc->count++;
        pthread_cond_signal(&pc->not_empty);
        pthread_mutex_unlock(&pc->mutex);
    }
    return NULL;
}

static void* thread_consumer(void *arg) {
    ThreadPC *pc = arg;
    uint64_t total_latency = 0;
    long count = 0;

    while (1) {
        pthread_mutex_lock(&pc->mutex);
        while (pc->count == 0)
            pthread_cond_wait(&pc->not_empty, &pc->mutex);

        Item item = pc->buffer[pc->head];
        pc->head = (pc->head + 1) % BUFFER_SIZE;
        pc->count--;
        pthread_cond_signal(&pc->not_full);
        pthread_mutex_unlock(&pc->mutex);

        if (item.value == POISON)
            break;

        total_latency += get_time_ns() - item.produced_at;
        count++;
    }

    if (count > 0) {
        printf("  Thread consumers → avg latency: %.0f ns (%ld items)\n",
               (double)total_latency / count, count);
    }
    return NULL;
}

static void run_threads(void) {
    ThreadPC pc;
    thread_pc_init(&pc);

    uint64_t start = get_time_ns();

    pthread_t producers[N_PRODUCERS];
    pthread_t consumers[N_CONSUMERS];

    for (int i = 0; i < N_PRODUCERS; i++)
        pthread_create(&producers[i], NULL, thread_producer, &pc);

    for (int i = 0; i < N_CONSUMERS; i++)
        pthread_create(&consumers[i], NULL, thread_consumer, &pc);

    for (int i = 0; i < N_PRODUCERS; i++)
        pthread_join(producers[i], NULL);

    uint64_t poison_start = 0;
    for (int i = 0; i < N_CONSUMERS; i++) {
        pthread_mutex_lock(&pc.mutex);
        while (pc.count == BUFFER_SIZE)
            pthread_cond_wait(&pc.not_full, &pc.mutex);

        pc.buffer[pc.tail].value = POISON;
        pc.tail = (pc.tail + 1) % BUFFER_SIZE;
        pc.count++;
        if (i == 0) poison_start = get_time_ns();
        pthread_cond_signal(&pc.not_empty);
        pthread_mutex_unlock(&pc.mutex);
    }

    for (int i = 0; i < N_CONSUMERS; i++)
        pthread_join(consumers[i], NULL);

    uint64_t end = get_time_ns();

    double seconds = (end - start) / 1e9;
    double shutdown_ms = (end - poison_start) / 1e6;

    printf("Threads:\n");
    printf("  throughput:     %.0f items/sec\n", TOTAL_ITEMS / seconds);
    printf("  shutdown time:  %.2f ms\n\n", shutdown_ms);
}

/* ────────────────────────────────────────
   SHARED MEMORY + SEMAPHORES VERSION
──────────────────────────────────────── */

typedef struct {
    Item buffer[BUFFER_SIZE];
    int head, tail, count;
    sem_t mutex, slots_empty, slots_full;
} ShmPC;

static ShmPC* shm_pc_create(void) {
    ShmPC *pc = mmap(NULL, sizeof(ShmPC), PROT_READ|PROT_WRITE,
                     MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if (pc == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    sem_init(&pc->mutex, 1, 1);
    sem_init(&pc->slots_empty, 1, BUFFER_SIZE);
    sem_init(&pc->slots_full, 1, 0);
    pc->head = pc->tail = pc->count = 0;
    return pc;
}

static void shm_producer(ShmPC *pc) {
    for (int64_t i = 1; i <= TOTAL_ITEMS / N_PRODUCERS; i++) {
        sem_wait(&pc->slots_empty);
        sem_wait(&pc->mutex);

        pc->buffer[pc->tail].value = i;
        pc->buffer[pc->tail].produced_at = get_time_ns();
        pc->tail = (pc->tail + 1) % BUFFER_SIZE;
        pc->count++;

        sem_post(&pc->mutex);
        sem_post(&pc->slots_full);
    }
}

static void shm_consumer(ShmPC *pc) {
    uint64_t total_latency = 0;
    long count = 0;

    while (1) {
        sem_wait(&pc->slots_full);
        sem_wait(&pc->mutex);

        Item item = pc->buffer[pc->head];
        pc->head = (pc->head + 1) % BUFFER_SIZE;
        pc->count--;

        sem_post(&pc->mutex);
        sem_post(&pc->slots_empty);

        if (item.value == POISON)
            break;

        total_latency += get_time_ns() - item.produced_at;
        count++;
    }

    if (count > 0) {
        printf("  SHM consumers → avg latency: %.0f ns\n", (double)total_latency / count);
    }
}

static void run_shm(void) {
    ShmPC *pc = shm_pc_create();
    uint64_t start = get_time_ns();

    pid_t children[N_PRODUCERS + N_CONSUMERS];

    for (int i = 0; i < N_PRODUCERS; i++) {
        if ((children[i] = fork()) == 0) {
            shm_producer(pc);
            exit(0);
        }
    }

    for (int i = 0; i < N_CONSUMERS; i++) {
        if ((children[N_PRODUCERS + i] = fork()) == 0) {
            shm_consumer(pc);
            exit(0);
        }
    }

    uint64_t poison_start = 0;
    for (int i = 0; i < N_CONSUMERS; i++) {
        sem_wait(&pc->slots_empty);
        sem_wait(&pc->mutex);

        pc->buffer[pc->tail].value = POISON;
        pc->tail = (pc->tail + 1) % BUFFER_SIZE;
        pc->count++;

        if (i == 0) poison_start = get_time_ns();

        sem_post(&pc->mutex);
        sem_post(&pc->slots_full);
    }

    for (int i = 0; i < N_PRODUCERS + N_CONSUMERS; i++)
        waitpid(children[i], NULL, 0);

    uint64_t end = get_time_ns();

    double seconds = (end - start) / 1e9;
    double shutdown_ms = (end - poison_start) / 1e6;

    printf("Shared memory + semaphores:\n");
    printf("  throughput:     %.0f items/sec\n", TOTAL_ITEMS / seconds);
    printf("  shutdown time:  %.2f ms\n\n", shutdown_ms);

    munmap(pc, sizeof(ShmPC));
}

/* ────────────────────────────────────────
   PIPE VERSION
──────────────────────────────────────── */

static void run_pipes(void) {
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(1);
    }

    uint64_t start = get_time_ns();
    pid_t children[N_PRODUCERS + N_CONSUMERS];

    // Producers
    for (int i = 0; i < N_PRODUCERS; i++) {
        if ((children[i] = fork()) == 0) {
            close(fd[0]);
            for (int64_t v = 1; v <= TOTAL_ITEMS / N_PRODUCERS; v++) {
                Item item = {v, get_time_ns()};
                write(fd[1], &item, sizeof(item));
            }
            close(fd[1]);
            exit(0);
        }
    }

    // Consumers
    for (int i = 0; i < N_CONSUMERS; i++) {
        if ((children[N_PRODUCERS + i] = fork()) == 0) {
            close(fd[1]);
            uint64_t total_latency = 0;
            long count = 0;
            Item item;
            while (read(fd[0], &item, sizeof(item)) == sizeof(item)) {
                if (item.value == POISON)
                    break;
                total_latency += get_time_ns() - item.produced_at;
                count++;
            }
            if (count > 0) {
                printf("  Pipe consumers → avg latency: %.0f ns\n", (double)total_latency / count);
            }
            close(fd[0]);
            exit(0);
        }
    }

    close(fd[0]);  // parent doesn't read

    uint64_t poison_start = 0;
    for (int i = 0; i < N_CONSUMERS; i++) {
        Item item = {POISON, 0};
        write(fd[1], &item, sizeof(item));
        if (i == 0) poison_start = get_time_ns();
    }
    close(fd[1]);

    for (int i = 0; i < N_PRODUCERS + N_CONSUMERS; i++)
        waitpid(children[i], NULL, 0);

    uint64_t end = get_time_ns();

    double seconds = (end - start) / 1e9;
    double shutdown_ms = (end - poison_start) / 1e6;

    printf("Pipes:\n");
    printf("  throughput:     %.0f items/sec\n", TOTAL_ITEMS / seconds);
    printf("  shutdown time:  %.2f ms\n\n", shutdown_ms);
}

/* ────────────────────────────────────────
   MAIN
──────────────────────────────────────── */

int main(int argc, char *argv[]) {
    printf("Concurrency Analyzer (Producer-Consumer)\n");
    printf("----------------------------------------\n\n");

    if (argc == 1 || strcmp(argv[1], "all") == 0) {
        run_threads();
        run_shm();
        run_pipes();
    }
    else if (strcmp(argv[1], "thread") == 0) {
        run_threads();
    }
    else if (strcmp(argv[1], "shm") == 0) {
        run_shm();
    }
    else if (strcmp(argv[1], "pipe") == 0) {
        run_pipes();
    }
    else {
        printf("Usage: %s [all | thread | shm | pipe]\n", argv[0]);
        return 1;
    }

    return 0;
}