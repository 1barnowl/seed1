#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <math.h>
#include <pwd.h>

#include "seed.h"

/* ── Global state (single seed instance per process) ─────── */
static SeedState G_state;

/* ── Signal handler ─────────────────────────────────────── */
static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        G_state.running = 0;
        fprintf(stderr, "\n[seed] caught signal %d — checkpointing before exit\n", sig);
    }
}

/* ── Utility: now_ms ────────────────────────────────────── */
double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ── Utility: seed_log ──────────────────────────────────── */
void seed_log(SeedState *s, const char *fmt, ...) {
    va_list ap;
    char    buf[1024];
    time_t  now = time(NULL);
    struct tm *tm = localtime(&now);
    char   ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (s->verbose) printf("[%s] %s\n", ts, buf);
    if (s->logfp)  fprintf(s->logfp, "[%s] %s\n", ts, buf);
}

/* ── Utility: drand_range ───────────────────────────────── */
double drand_range(double lo, double hi) {
    return lo + ((double)rand() / RAND_MAX) * (hi - lo);
}

/* ── Utility: mkdir_p ───────────────────────────────────── */
int mkdir_p(const char *path, mode_t mode) {
    char tmp[MAX_PATH_LEN];
    char *p;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode) == 0 || errno == EEXIST ? 0 : -1;
}

/* ── Utility: file I/O ──────────────────────────────────── */
ssize_t read_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (ssize_t)n;
}

int write_file(const char *path, const char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}

int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* ── Utility: check command exists ─────────────────────── */
int cmd_exists(const char *cmd) {
    char buf[256];
    snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

/* ── Utility: run command, capture output ───────────────── */
int run_cmd(const char *cmd, char *out, size_t cap, int timeout_s) {
    char  full[1024];
    char  tmpf[] = "/tmp/seed_cmd_XXXXXX";
    int   fd = mkstemp(tmpf);
    if (fd < 0) return -1;
    close(fd);

    snprintf(full, sizeof(full), "timeout %d sh -c '%s' >%s 2>&1", timeout_s, cmd, tmpf);
    int ret = system(full);

    if (out && cap > 0) {
        read_file(tmpf, out, cap);
    }
    unlink(tmpf);
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

/* ── Utility: hex encode ────────────────────────────────── */
void hex_encode(const uint8_t *data, size_t len, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0xf];
    }
    out[len * 2] = '\0';
}

void hex_decode(const char *hex, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned int b;
        sscanf(hex + i * 2, "%02x", &b);
        out[i] = (uint8_t)b;
    }
}

/* ── Setup base directory ───────────────────────────────── */
static int setup_dirs(SeedState *s) {
    /* Base: ~/.seedcore */
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }

    snprintf(s->base_dir,      sizeof(s->base_dir),      "%s/.seedcore",              home);
    snprintf(s->store_dir,     sizeof(s->store_dir),     "%s/.seedcore/store",        home);
    snprintf(s->keys_dir,      sizeof(s->keys_dir),      "%s/.seedcore/keys",         home);
    snprintf(s->snapshots_dir, sizeof(s->snapshots_dir), "%s/.seedcore/snapshots",    home);
    snprintf(s->artifacts_dir, sizeof(s->artifacts_dir), "%s/.seedcore/artifacts",    home);
    snprintf(s->log_path,      sizeof(s->log_path),      "%s/.seedcore/seed.log",     home);

    mkdir_p(s->base_dir,      0700);
    mkdir_p(s->store_dir,     0700);
    mkdir_p(s->keys_dir,      0700);
    mkdir_p(s->snapshots_dir, 0700);
    mkdir_p(s->artifacts_dir, 0755);

    return 0;
}

/* ── Print usage ────────────────────────────────────────── */
static void usage(const char *argv0) {
    fprintf(stderr,
        "SEED CORE v%s — autonomous digital organism\n\n"
        "Usage: %s [OPTIONS]\n\n"
        "Options:\n"
        "  -v            Verbose output (print every tick)\n"
        "  -d            Dry-run (don't write artifacts or schedule)\n"
        "  -g <file>     Load genome from file\n"
        "  -r            Reset all state and start fresh\n"
        "  -p            Print current genome and exit\n"
        "  -s <seconds>  Run for N seconds then exit (0 = forever)\n"
        "  -h            Show this help\n\n"
        "Data directory: ~/.seedcore/\n"
        "  store/        Content-addressed blob store\n"
        "  keys/         Ed25519 signing keypair\n"
        "  snapshots/    Genome + state checkpoints\n"
        "  artifacts/    Generated scripts & executables\n"
        "  seed.log      Execution log\n",
        SEED_VERSION, argv0);
}

/* ── main ───────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    memset(&G_state, 0, sizeof(G_state));
    G_state.running = 1;
    G_state.energy  = 80.0;

    int opt;
    int reset_flag   = 0;
    int print_genome = 0;
    int run_seconds  = 0;
    char genome_file[MAX_PATH_LEN] = {0};

    while ((opt = getopt(argc, argv, "vdg:rps:h")) != -1) {
        switch (opt) {
            case 'v': G_state.verbose  = 1; break;
            case 'd': G_state.dry_run  = 1; break;
            case 'g': snprintf(genome_file, sizeof(genome_file), "%s", optarg); break;
            case 'r': reset_flag       = 1; break;
            case 'p': print_genome     = 1; break;
            case 's': run_seconds      = atoi(optarg); break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
        }
    }

    /* ── Directory setup ── */
    setup_dirs(&G_state);

    /* ── Open log file ── */
    G_state.logfp = fopen(G_state.log_path, "a");

    /* ── Signal handlers ── */
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    /* ── Seed RNG ── */
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    /* ── Genome: load or init ── */
    char snap_path[MAX_PATH_LEN];
    snprintf(snap_path, sizeof(snap_path), "%s/genome.conf", G_state.snapshots_dir);

    if (reset_flag) {
        unlink(snap_path);
        fprintf(stdout, "[seed] reset — starting fresh genome\n");
    }

    if (genome_file[0]) {
        char buf[4096];
        if (read_file(genome_file, buf, sizeof(buf)) > 0) {
            if (genome_deserialize(&G_state.genome, buf) != 0) {
                fprintf(stderr, "[seed] failed to parse genome file\n");
                return 1;
            }
        }
    } else if (file_exists(snap_path)) {
        char buf[4096];
        if (read_file(snap_path, buf, sizeof(buf)) > 0) {
            genome_deserialize(&G_state.genome, buf);
            fprintf(stdout, "[seed] resumed genome from %s\n", snap_path);
        } else {
            genome_init_default(&G_state.genome);
        }
    } else {
        genome_init_default(&G_state.genome);
    }
    genome_hash(&G_state.genome);

    if (print_genome) {
        genome_print(&G_state.genome);
        if (G_state.logfp) fclose(G_state.logfp);
        return 0;
    }

    /* ── Crypto init ── */
    if (keymgmt_init(&G_state) != 0) {
        fprintf(stderr, "[seed] key management init failed\n");
        if (G_state.logfp) fclose(G_state.logfp);
        return 1;
    }

    /* ── Store init ── */
    store_init(G_state.store_dir);

    /* ── Primitive registry ── */
    if (prim_registry_init(&G_state) != 0) {
        fprintf(stderr, "[seed] primitive registry init failed\n");
        if (G_state.logfp) fclose(G_state.logfp);
        return 1;
    }

    /* ── Growth init (probe host, load composites) ── */
    if (growth_init(&G_state) != 0) {
        fprintf(stderr, "[seed] growth init failed\n");
        if (G_state.logfp) fclose(G_state.logfp);
        return 1;
    }

    /* ── Print banner ── */
    printf("\n");
    printf("  ╔══════════════════════════════════════════╗\n");
    printf("  ║  SEED CORE v%-6s — organism online     ║\n", SEED_VERSION);
    printf("  ║  dir  : %s\n", G_state.base_dir);
    printf("  ║  os   : %s %s\n", G_state.host.os_name, G_state.host.arch);
    printf("  ║  prims: %d atomic primitives registered  \n", G_state.prim_count);
    printf("  ║  Press Ctrl+C to checkpoint and exit     ║\n");
    printf("  ╚══════════════════════════════════════════╝\n\n");

    genome_print(&G_state.genome);
    printf("\n");

    /* ── Optional run-for-N-seconds mode ── */
    time_t deadline = run_seconds > 0 ? time(NULL) + run_seconds : 0;

    /* ── Main growth loop ── */
    while (G_state.running) {
        if (deadline && time(NULL) >= deadline) {
            printf("[seed] time limit reached — exiting\n");
            break;
        }
        growth_loop(&G_state);
    }

    /* ── Final checkpoint ── */
    {
        char buf[4096];
        genome_serialize(&G_state.genome, buf, sizeof(buf));
        write_file(snap_path, buf, strlen(buf));
        printf("[seed] genome checkpointed to %s\n", snap_path);
        printf("[seed] ticks: %llu  score: %.2f  composites: %d\n",
               (unsigned long long)G_state.ticks,
               G_state.cumulative_score,
               G_state.composite_count);
    }

    if (G_state.logfp) fclose(G_state.logfp);
    return 0;
}
