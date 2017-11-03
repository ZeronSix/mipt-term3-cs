#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>

#define SYNC_FIFO "/tmp/task1.sync"
#define DATA_FIFO "/tmp/task1.data"
#define BLOCK_SIZE 1024 * 16
#define FIFO_MAX_NAME_LENGTH 256
#define POLL_TIMEOUT 2000

int sender(const char *filename);
int receiver(void);
int setfdflag(int fd, int flag, int value);

int main(int argc, char *argv[]) {
    if (argc == 1) {
        return receiver();
    }
    else if (argc == 2) {
        return sender(argv[1]);
    }
    else {
        fprintf(stderr, "Wrong arguments!\n");
        return EXIT_FAILURE;
    }
}

#define THROW(msg, label) { \
    perror(msg); \
    retval = EXIT_FAILURE; \
    goto label; \
}
#define THROW_FPRINTF(msg, label) { \
    fprintf(stderr, "%s\n", msg); \
    retval = EXIT_FAILURE; \
    goto label; \
}

static void mkfifoname(char *buf, pid_t pid) {
    snprintf(buf, FIFO_MAX_NAME_LENGTH, "%s%d", DATA_FIFO, pid);
}

int sender(const char *filename) {
    int retval = EXIT_SUCCESS;
    int sync_fifo_fd = -1;
    int fifo_fd = -1;
    int file_fd = -1;
    ssize_t readsize = 0;
    ssize_t writesize = 0;
    char buf[BLOCK_SIZE];
    char fifoname[FIFO_MAX_NAME_LENGTH] = { '\0' };
    pid_t pid = -1;

    if (mkfifo(SYNC_FIFO, 0666) < 0) {
        if (errno != EEXIST) {
            THROW("mkfifo", SYNC_FIFO_FAILURE);
        }
    }
    if ((sync_fifo_fd = open(SYNC_FIFO, O_RDWR)) < 0) {
        THROW("open", SYNC_FIFO_FAILURE);
    }
    if (read(sync_fifo_fd, &pid, sizeof(pid)) < 0) {
        THROW("read pid", SYNC_FIFO_OPEN_FAILURE);
    }

    sleep(3);

    mkfifoname(fifoname, pid);
    if ((fifo_fd = open(fifoname, O_WRONLY | O_NONBLOCK)) < 0) {
        THROW("open data fifo", FIFO_FAILURE);
    }
    if ((setfdflag(fifo_fd, O_NONBLOCK, 0) < 0)) {
        THROW("setfdflag", FIFO_SETFL_FAILURE);
    }

    if ((file_fd = open(filename, O_RDONLY)) < 0) {
        THROW("open file", FILE_OPEN_FAILURE);
    }
    while ((readsize = read(file_fd, buf, BLOCK_SIZE)) != 0) {
        if (readsize < 0) {
            THROW("read", RW_FAILURE);
        }

        if ((writesize = write(fifo_fd, buf, readsize)) < 0) {
            THROW("write", RW_FAILURE);
        }
    }

RW_FAILURE:
    close(file_fd);
FILE_OPEN_FAILURE:
    close(fifo_fd);
FIFO_SETFL_FAILURE:
FIFO_FAILURE:
SYNC_FIFO_OPEN_FAILURE:
    close(sync_fifo_fd);
SYNC_FIFO_FAILURE:
    return retval;
}

int receiver(void) {
    int retval = EXIT_SUCCESS;
    int fifo_fd = -1;
    int sync_fifo_fd = -1;
    ssize_t readsize = 0;
    ssize_t writesize = 0;
    char buf[BLOCK_SIZE];
    char fifoname[FIFO_MAX_NAME_LENGTH] = { '\0' };
    pid_t pid = getpid();

    mkfifoname(fifoname, pid);
    if (mkfifo(fifoname, 0666) < 0) {
        THROW("mkfifo", DATA_MKFIFO_FAILURE);
    }

    if ((fifo_fd = open(fifoname, O_RDONLY | O_NONBLOCK)) < 0) {
        THROW("open", DATA_FIFO_FAILURE);
    }
    if ((setfdflag(fifo_fd, O_NONBLOCK, 0) < 0)) {
        THROW("setfdflag", SYNC_FIFO_FAILURE);
    }

    if (mkfifo(SYNC_FIFO, 0666) < 0) {
        if (errno != EEXIST) {
            THROW("mkfifo", SYNC_FIFO_FAILURE);
        }
    }

    if ((sync_fifo_fd = open(SYNC_FIFO, O_WRONLY)) < 0) {
        THROW("open sync", RW_FAILURE);
    }
    if (write(sync_fifo_fd, &pid, sizeof(pid)) < 0) {
        THROW("read pid", RW_FAILURE);
    }

    struct pollfd pfd;
    pfd.fd = fifo_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int poll_result = poll(&pfd, 1, POLL_TIMEOUT);
    if (poll_result < 0) {
        THROW("poll error", RW_FAILURE);
    }
    else if (poll_result == 0) {
        fprintf(stderr, "poll timeout!\n");
        exit(EXIT_FAILURE);
    }

    while ((readsize = read(fifo_fd, buf, BLOCK_SIZE)) != 0) {
        if (readsize < 0) {
            THROW("read", RW_FAILURE);
        }

        if ((writesize = write(STDOUT_FILENO, buf, readsize)) < 0) {
            THROW("write", RW_FAILURE);
        }
    }

RW_FAILURE:
    close(sync_fifo_fd);
SYNC_FIFO_FAILURE:
    close(fifo_fd);
DATA_FIFO_FAILURE:
DATA_MKFIFO_FAILURE:
    unlink(fifoname);
    return retval;
}

int setfdflag(int fd, int flag, int value) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    if (value) {
        flags |= flag;
    } else {
        flags &= ~flag;
    }

    return fcntl(fd, F_SETFL, flags);
}

