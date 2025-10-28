#ifdef EP_CONFIG_TEST

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

#include "defs.h"
#include "test.h"

enum {
    ansi_color_red = 31,
    ansi_color_green = 32,
    ansi_color_default = 0,
};

static void printlnf_colored(int color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    printf("\x1b[%02dm", color);
    vprintf(fmt, args);
    printf("\x1b[0m\n");

    va_end(args);
}

static char* read_into_buf(int fd)
{
    size_t cap = 1024, len = 0;
    char* buf = malloc(cap);
    if (!buf) {
        return NULL;
    }

    ssize_t n;
    while ((n = read(fd, buf + len, cap - len)) > 0) {
        len += n;
        if (len >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) {
                free(buf);
                return NULL;
            }
        }
    }

    buf[len] = '\0';
    return buf;
}

static bool run_isolated_test(struct ep_test_entry* te, char** out_stdout, char** out_stderr)
{
    int opipe[2];
    int epipe[2];

    ep_verify(pipe(opipe) >= 0);
    ep_verify(pipe(epipe) >= 0);

    pid_t pid = fork();
    ep_verify(pid >= 0);

    if (pid == 0) {
        close(opipe[0]);
        close(epipe[0]);

        dup2(opipe[1], STDOUT_FILENO);
        dup2(epipe[1], STDERR_FILENO);

        (te->runner)();

        exit(0);
    }

    close(opipe[1]);
    close(epipe[1]);

    char* obuf = read_into_buf(opipe[0]);
    ep_verify(obuf);

    char* ebuf = read_into_buf(epipe[0]);
    ep_verify(ebuf);

    int status = 0;
    ep_verify(waitpid(pid, &status, 0) >= 0);

    *out_stdout = obuf;
    *out_stderr = ebuf;

    close(opipe[0]);
    close(epipe[0]);

    if (WIFSIGNALED(status)) {
        return false;
    } else if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        return (code == 0);
    } else {
        ep_verify(false);
    }
}

ep_noreturn void _ep_test_fail(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}

int main(int argc, char** argv)
{
    struct ep_test_entry* te = _ep_test_start;
    size_t ntests = _ep_test_end - _ep_test_start;
    size_t testnum = 1;
    size_t npassed = 0;
    char* obuf = NULL;
    char* ebuf = NULL;

    while (te != _ep_test_end) {
        bool res = run_isolated_test(te, &obuf, &ebuf);
        printlnf_colored((res ? ansi_color_green : ansi_color_red),
                         "[%zu/%zu] %s %s",
                         testnum,
                         ntests,
                         te->name,
                         (res ? "passed" : "failed"));
        
        fprintf(stdout, "%s", obuf);
        fprintf(stderr, "%s", ebuf);

        free(obuf);
        free(ebuf);

        npassed += !!res;
        te++;
        testnum++;

    }

    printf("\n---\n");
    printlnf_colored((npassed != ntests ? ansi_color_red : ansi_color_green),
                     "%zu/%zu tests passed",
                     npassed,
                     ntests);
}

#endif
