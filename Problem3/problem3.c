#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <fcntl.h>

#define KEY_FILENAME "/tmp/problem3_key"
#define PROJ_ID 1
#define BLOCK_SIZE 1024
#define SEM_COUNT 8
#define MEMORY_SIZE BLOCK_SIZE + sizeof(ssize_t)

#define THROW(msg, label) { \
    perror(msg); \
    retval = EXIT_FAILURE; \
    goto label; \
}
#define THROW_EXIT(msg) { \
    perror(msg); \
    exit(EXIT_FAILURE); \
}
#define THROW_FPRINTF(msg, label) { \
    fprintf(stderr, "%s\n", msg); \
    retval = EXIT_FAILURE; \
    goto label; \
}

enum {
    READER_LOCK,
    WRITER_LOCK,
    CLOSE_LOCK,
    WORKING,
    MUTEX,
    EMPTY,
    FULL,
    INIT
};

#define SEMBUF_SET(sembuf, num, op, flg) { \
    sembuf.sem_num = num; sembuf.sem_op = op; sembuf.sem_flg = flg; \
}

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short * array;
    struct seminfo* __buf;
};

int initsem(int semid);
int sender(const char *filename);
int receiver(void);

int main(int argc, char *argv[]) {
    int retval = EXIT_SUCCESS;

    if (argc == 2) {
        return sender(argv[1]);
    }
    else if (argc == 1) {
        return receiver();
    }
    else {
        fprintf(stderr, "Wrong argument count!\n");
        return EXIT_FAILURE;
    }

    return retval;
}

int initsem(int semid) {
    struct sembuf sem[6] = { { 0 } };
    SEMBUF_SET(sem[0], READER_LOCK, 1, 0);
    SEMBUF_SET(sem[1], WRITER_LOCK, 1, 0);
    SEMBUF_SET(sem[2], MUTEX, 1, 0);
    SEMBUF_SET(sem[3], EMPTY, 1, 0);
    SEMBUF_SET(sem[4], INIT, 0, IPC_NOWAIT);
    SEMBUF_SET(sem[5], INIT, 1, IPC_NOWAIT);

    return semop(semid, sem, 6);
}

static int createsem(key_t *out_key) {
    int key_fd = open(KEY_FILENAME, O_WRONLY | O_CREAT);
    if (key_fd < 0) {
        perror("Failed to open key filename:");
        return -1;
    }
    close(key_fd);

    key_t key = ftok(KEY_FILENAME, PROJ_ID);
    if (key < 0) {
        perror("Failed ftok\n");
        return -1;
    }

    int semid = semget(key, SEM_COUNT, IPC_CREAT | 0666);
    if (semid < 0) {
        perror("Failed semget");
        return -1;
    }

    if (initsem(semid) < 0) {
        if (errno != EAGAIN) {
            perror("Semaphore initialization");
            return -1;
        }
        else {
            puts("Semaphore is already initialized");
        }
    }
    else {
        puts("Init successful");
    }

    *out_key = key;
    return semid;
}

static void *createshmem(key_t key) {
    int shmid = shmget(key, MEMORY_SIZE, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("Shmget");
        return (void *)-1;
    }

    void *mem = shmat(shmid, 0, 0);
    if (mem == (void *)-1) {
        perror("Shmat");
    }

    return mem;
}

int sender(const char *filename) {
    int retval = EXIT_SUCCESS;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        THROW("open", OPEN_FAILURE);
    }

    key_t key = 0;
    int semid = -1;
    void *mem = NULL;
    if ((semid = createsem(&key)) < 0) {
        goto CREATESEM_FAILURE;
    }
    if ((mem = createshmem(key)) < 0) {
        goto CREATESHMEM_FAILURE;
    }

    struct sembuf wlock[2], lockwait[2];
    SEMBUF_SET(wlock[0], WORKING, 0, 0);
    SEMBUF_SET(wlock[1], WRITER_LOCK, -1, SEM_UNDO);
    if (semop(semid, wlock, 2) < 0) {
        THROW("Failed writer lock", SEMOP_FAILURE);
    }

    union semun arg;
    arg.val = 1;
    semctl(semid, EMPTY, SETVAL, arg);
    semctl(semid, MUTEX, SETVAL, arg);
    arg.val = 0;
    semctl(semid, FULL, SETVAL, arg);

    SEMBUF_SET(lockwait[0], READER_LOCK, 0, 0);
    SEMBUF_SET(lockwait[1], WORKING, 1, SEM_UNDO);
    if (semop(semid, lockwait, 2) < 0) {
        THROW("Failed reader lock wait", SEMOP_FAILURE);
    }

    int running = 1;
    struct sembuf empty[2], mutex[2], full[2];
    SEMBUF_SET(empty[0], READER_LOCK, 0, IPC_NOWAIT);
    SEMBUF_SET(empty[1], EMPTY, -1, 0);
    SEMBUF_SET(mutex[0], READER_LOCK, 0, IPC_NOWAIT);
    SEMBUF_SET(mutex[1], MUTEX, -1, 0);
    SEMBUF_SET(full[0], READER_LOCK, 0, IPC_NOWAIT);
    SEMBUF_SET(full[1], FULL, 1, 0);
    while (running) {
        if (semop(semid, empty, 2) < 0) {
            THROW("semop empty down", SEMOP_FAILURE);
        }
        mutex[1].sem_op = -1;
        if (semop(semid, mutex, 2) < 0) {
            THROW("semop mutex down", SEMOP_FAILURE);
        }

        ssize_t readsize = read(fd, ((ssize_t *)mem) + 1, BLOCK_SIZE);
        *(ssize_t *)mem = readsize;
        if (readsize < 0) {
            THROW_EXIT("read");
        }
        else if (readsize == 0) {
            running = 0;
        }

        mutex[1].sem_op = 1;
        if (semop(semid, mutex, 2) < 0) {
            THROW("semop mutex up", SEMOP_FAILURE);
        }
        if (semop(semid, full, 2) < 0) {
            THROW("semop full up", SEMOP_FAILURE);
        }
    }

    struct sembuf closelock;
    SEMBUF_SET(closelock, CLOSE_LOCK, 0, SEM_UNDO);
    if (semop(semid, &closelock, 1) < 0) {
        THROW("Failed close lock", SEMOP_FAILURE);
    }
SEMOP_FAILURE:
CREATESHMEM_FAILURE:
CREATESEM_FAILURE:
    close(fd);
OPEN_FAILURE:
    return retval;
}

int receiver(void) {
    int retval = EXIT_SUCCESS;

    key_t key = 0;
    int semid = -1;
    void *mem = NULL;
    if ((semid = createsem(&key)) < 0) {
        goto CREATESEM_FAILURE;
    }
    if ((mem = createshmem(key)) == (void *)-1) {
        goto CREATESHMEM_FAILURE;
    }

    struct sembuf rlock[2], lockwait[5];
    SEMBUF_SET(rlock[0], WORKING, 0, 0);
    SEMBUF_SET(rlock[1], READER_LOCK, -1, SEM_UNDO);
    if (semop(semid, rlock, 2) < 0) {
        THROW("Failed reader lock", SEMOP_FAILURE);
    }

    SEMBUF_SET(lockwait[0], WORKING, -1, 0);
    SEMBUF_SET(lockwait[1], WORKING, 0, 0);
    SEMBUF_SET(lockwait[2], WORKING, 1, 0);
    SEMBUF_SET(lockwait[3], WORKING, 1, SEM_UNDO);
    SEMBUF_SET(lockwait[4], WRITER_LOCK, 0, IPC_NOWAIT);
    if (semop(semid, lockwait, 5) < 0) {
        THROW("Failed reader lock wait", SEMOP_FAILURE);
    }

    struct sembuf closelock;
    SEMBUF_SET(closelock, CLOSE_LOCK, 1, SEM_UNDO);
    if (semop(semid, &closelock, 1) < 0) {
        THROW("Failed close lock", SEMOP_FAILURE);
    }

    int running = 1;
    struct sembuf empty[2], mutex[2], full[2];
    SEMBUF_SET(empty[0], WRITER_LOCK, 0, IPC_NOWAIT);
    SEMBUF_SET(empty[1], EMPTY, 1, 0);
    SEMBUF_SET(mutex[0], WRITER_LOCK, 0, IPC_NOWAIT);
    SEMBUF_SET(mutex[1], MUTEX, -1, 0);
    SEMBUF_SET(full[0], WRITER_LOCK, 0, IPC_NOWAIT);
    SEMBUF_SET(full[1], FULL, -1, 0);

    while (running) {
        if (semop(semid, full, 2) < 0) {
            THROW("semop full down", SEMOP_FAILURE);
        }
        mutex[1].sem_op = -1;
        if (semop(semid, mutex, 2) < 0) {
            THROW("semop mutex down", SEMOP_FAILURE);
        }

        if (*(ssize_t *)mem <= 0) {
            running = 0;
        }
        else {
            write(STDOUT_FILENO, (ssize_t *)mem + 1, (size_t)*(ssize_t *)mem);
        }

        mutex[1].sem_op = 1;
        if (semop(semid, mutex, 2) < 0) {
            THROW("semop mutex up", SEMOP_FAILURE);
        }
        if (semop(semid, empty, 2) < 0) {
            THROW("semop empty up", SEMOP_FAILURE);
        }
    }

SEMOP_FAILURE:
CREATESHMEM_FAILURE:
CREATESEM_FAILURE:
    return retval;
}
