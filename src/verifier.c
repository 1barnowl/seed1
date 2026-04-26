#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include "seed.h"

#define VERIFIER_TIMEOUT_S  8
#define VERIFIER_MEM_MB     64
#define VERIFIER_CPU_S      6

/*
 * Run a single primitive in the calling process (no fork).
 * Used for low-risk atomics (map, filter, reduce, etc.)
 */
int verifier_run_primitive(Primitive *p, const char *input, size_t ilen,
                            SeedState *s, VerifierResult *r) {
    memset(r, 0, sizeof(*r));
    if (!p || !p->exec) {
        snprintf(r->error_msg, sizeof(r->error_msg), "null primitive");
        return -1;
    }

    double t0 = now_ms();

    int ret = p->exec(input, ilen,
                       r->output, sizeof(r->output) - 1,
                       &r->output_len, s);

    r->latency_ms = now_ms() - t0;
    r->success    = (ret == 0);
    r->score      = r->success ? (0.5 + p->base_score * 0.5) : 0.0;

    p->exec_count++;
    if (r->success) {
        p->success_count++;
        p->avg_latency_ms = (p->avg_latency_ms * (p->exec_count - 1) + r->latency_ms)
                             / p->exec_count;
    }

    return ret;
}

/*
 * Run a composite pipeline (sequence of primitives) in a forked subprocess.
 * Uses pipe for output, setrlimit for resource bounds.
 * This is the core verifier: real execution, real scoring.
 */
int verifier_run_composite(SeedState *s, const char **step_ids, int step_count,
                            const char *input, size_t ilen, VerifierResult *r) {
    memset(r, 0, sizeof(*r));

    if (step_count <= 0 || step_count > MAX_COMPOSITE_STEPS) {
        snprintf(r->error_msg, sizeof(r->error_msg), "invalid step count %d", step_count);
        return -1;
    }

    /* Resolve all primitives before forking */
    Primitive *steps[MAX_COMPOSITE_STEPS];
    for (int i = 0; i < step_count; i++) {
        steps[i] = prim_find(s, step_ids[i]);
        if (!steps[i]) {
            snprintf(r->error_msg, sizeof(r->error_msg),
                     "primitive not found: %s", step_ids[i]);
            return -1;
        }
    }

    /* Pipe: child writes result to parent */
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        snprintf(r->error_msg, sizeof(r->error_msg), "pipe failed: %s", strerror(errno));
        return -1;
    }

    double t0 = now_ms();
    pid_t  pid = fork();

    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        snprintf(r->error_msg, sizeof(r->error_msg), "fork failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* ── Child process ── */
        close(pipefd[0]);

        /* Resource limits */
        struct rlimit rl;
        rl.rlim_cur = rl.rlim_max = VERIFIER_CPU_S;
        setrlimit(RLIMIT_CPU, &rl);

        rl.rlim_cur = rl.rlim_max = (rlim_t)VERIFIER_MEM_MB * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);

        rl.rlim_cur = rl.rlim_max = 4 * 1024 * 1024; /* 4MB output files */
        setrlimit(RLIMIT_FSIZE, &rl);

        /* Execute pipeline */
        char buf_a[MAX_IO_BUF], buf_b[MAX_IO_BUF];
        size_t buf_a_len = ilen < MAX_IO_BUF ? ilen : MAX_IO_BUF - 1;
        memcpy(buf_a, input, buf_a_len);
        buf_a[buf_a_len] = '\0';

        char  *cur_in  = buf_a, *cur_out = buf_b;
        size_t cur_ilen = buf_a_len, cur_olen = 0;
        int    ok = 1;

        for (int i = 0; i < step_count && ok; i++) {
            memset(cur_out, 0, MAX_IO_BUF);
            cur_olen = 0;
            int ret = steps[i]->exec(cur_in, cur_ilen,
                                      cur_out, MAX_IO_BUF - 1,
                                      &cur_olen, s);
            if (ret != 0) { ok = 0; break; }

            /* Swap buffers */
            char *tmp = cur_in; cur_in = cur_out; cur_out = tmp;
            cur_ilen = cur_olen;
        }

        /* Write success byte + output length + output */
        uint8_t status = ok ? 1 : 0;
        write(pipefd[1], &status, 1);
        uint32_t outlen = (uint32_t)cur_ilen;
        write(pipefd[1], &outlen, 4);
        if (ok && cur_ilen > 0)
            write(pipefd[1], cur_in, cur_ilen);

        close(pipefd[1]);
        _exit(ok ? 0 : 1);
    }

    /* ── Parent process ── */
    close(pipefd[1]);

    /* Set up timeout via alarm */
    alarm(VERIFIER_TIMEOUT_S);

    /* Read result */
    uint8_t  status = 0;
    uint32_t outlen = 0;
    ssize_t  nr;

    nr = read(pipefd[0], &status, 1);
    if (nr == 1 && status == 1) {
        read(pipefd[0], &outlen, 4);
        if (outlen > 0 && outlen < sizeof(r->output)) {
            r->output_len = read(pipefd[0], r->output, outlen);
        }
    }
    close(pipefd[0]);
    alarm(0);

    /* Wait for child */
    int wstatus;
    waitpid(pid, &wstatus, 0);

    r->latency_ms = now_ms() - t0;
    r->success    = (status == 1 && WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0);

    /* Score based on success, latency, and output quality */
    if (r->success) {
        double latency_penalty = r->latency_ms > 1000.0 ? 0.1 : 0.0;
        double output_bonus    = r->output_len > 0 ? 0.1 : 0.0;
        r->score = 0.6 + output_bonus - latency_penalty;
    } else {
        r->score = 0.0;
        snprintf(r->error_msg, sizeof(r->error_msg),
                 "composite failed (status=%d exit=%d)",
                 status, WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
    }

    /* Update primitive stats */
    for (int i = 0; i < step_count; i++) {
        steps[i]->exec_count++;
        if (r->success) steps[i]->success_count++;
    }

    return r->success ? 0 : -1;
}
