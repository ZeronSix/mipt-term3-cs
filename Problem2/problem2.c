#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

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

typedef struct Msg {
    long type;
} Msg;

int parse_arg(const char *str, long *ptr);

int main(int argc, char *argv[]) {
    int retval = EXIT_SUCCESS;

    if (argc != 2) {
        fprintf(stderr, "Wrong argument count!\n");
        return EXIT_FAILURE;
    }

    long n = 0;
    if (parse_arg(argv[1], &n) < 0) {
        return EXIT_FAILURE;
    }

    Msg msg = { 1 };
    int msgq = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msgq < 0) {
        THROW("msgget", MSGGET_FAILURE);
    }

    pid_t pid = -1;
    for (long i = 1; i <= n; i++) {
        pid = fork();
        if (pid == 0) {
            if (msgrcv(msgq, &msg, 0, i, 0) < 0) {
                THROW_EXIT("msgrcv");
            }
            printf("%ld ", i);
            fflush(stdout);
            msg.type++;
            if (msgsnd(msgq, &msg, 0, 0) < 0) {
                THROW_EXIT("msgsnd");
            }
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0) {
            THROW("fork", FORK_FAILURE);
        }
    }

    if (msgsnd(msgq, &msg, 0, 0) < 0) {
        THROW("msgsnd", MSGSND_FAILURE);
    }

    if (msgrcv(msgq, &msg, 0, n + 1, 0) < 0) {
        THROW("msgsnd", MSGSND_FAILURE);
    }
FORK_FAILURE:
MSGSND_FAILURE:
MSGRCV_FAILURE:
    if (msgctl(msgq, IPC_RMID, NULL) < 0) {
        THROW_EXIT("msgctl");
    }
MSGGET_FAILURE:
    return retval;
}

int parse_arg(const char *str, long *ptr) {
    int retval = EXIT_SUCCESS;

    long n = 0;
    char *endptr = NULL;
    errno = 0;
    n = strtol(str, &endptr, 10);

    if (errno != 0) {
        THROW("strtol", STRTOL_FAILURE);
    }
    if (n <= 0) {
        THROW_FPRINTF("n <= 0!\n", RANGE_FAILURE);
    }
    if (*endptr != '\0') {
        THROW("further chars!", FURTHER_CHARS);
    }

    *ptr = n;
FURTHER_CHARS:
RANGE_FAILURE:
STRTOL_FAILURE:
    return retval;
}
