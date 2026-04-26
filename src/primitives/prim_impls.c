#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <zlib.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "seed.h"

/* Forward declaration from genome.c */
extern void genome_update_from_outcome(Genome *g, PrimType ptype,
                                        double outcome_score, double baseline);

/* ─────────────────────────────────────────────────────────
 * PROBE — reads real host environment, outputs JSON-like KV
 * ───────────────────────────────────────────────────────── */
int prim_probe_exec(const char *in, size_t ilen,
                    char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)in; (void)ilen;
    char buf[512];
    int  off = 0;

    off += snprintf(out + off, ocap - off, "os=%s\n", s->host.os_name[0] ? s->host.os_name : "unknown");
    off += snprintf(out + off, ocap - off, "arch=%s\n", s->host.arch[0] ? s->host.arch : "unknown");
    off += snprintf(out + off, ocap - off, "cores=%d\n", s->host.cpu_cores);
    off += snprintf(out + off, ocap - off, "mem_total_mb=%llu\n",
                    (unsigned long long)(s->host.mem_total_kb / 1024));
    off += snprintf(out + off, ocap - off, "mem_free_mb=%llu\n",
                    (unsigned long long)(s->host.mem_free_kb / 1024));
    off += snprintf(out + off, ocap - off, "disk_free_mb=%llu\n",
                    (unsigned long long)(s->host.disk_free_kb / 1024));
    off += snprintf(out + off, ocap - off, "load=%.2f\n", s->host.load_avg);
    off += snprintf(out + off, ocap - off, "hostname=%s\n", s->host.hostname);
    off += snprintf(out + off, ocap - off, "python3=%d\n", s->host.has_python3);
    off += snprintf(out + off, ocap - off, "node=%d\n",    s->host.has_node);
    off += snprintf(out + off, ocap - off, "curl=%d\n",    s->host.has_curl);
    off += snprintf(out + off, ocap - off, "cron=%d\n",    s->host.has_cron);
    off += snprintf(out + off, ocap - off, "systemd=%d\n", s->host.has_systemd);
    off += snprintf(out + off, ocap - off, "docker=%d\n",  s->host.has_docker);
    off += snprintf(out + off, ocap - off, "git=%d\n",     s->host.has_git);
    off += snprintf(out + off, ocap - off, "energy=%.2f\n", s->energy);
    off += snprintf(out + off, ocap - off, "ticks=%llu\n",
                    (unsigned long long)s->ticks);
    off += snprintf(out + off, ocap - off, "composites=%d\n", s->composite_count);

    /* Also probe /proc/uptime */
    if (read_file("/proc/uptime", buf, sizeof(buf)) > 0) {
        double uptime; sscanf(buf, "%lf", &uptime);
        off += snprintf(out + off, ocap - off, "uptime_s=%.0f\n", uptime);
    }

    *olen = off;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * MAP — apply toUpperCase transformation to each line
 * ───────────────────────────────────────────────────────── */
int prim_map_exec(const char *in, size_t ilen,
                  char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)s;
    size_t off = 0;
    for (size_t i = 0; i < ilen && off < ocap - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        out[off++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    out[off] = '\0';
    *olen = off;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * FILTER — remove lines starting with '#' (comment filter)
 * ───────────────────────────────────────────────────────── */
int prim_filter_exec(const char *in, size_t ilen,
                     char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)s;
    size_t off = 0;
    const char *p = in;
    const char *end = in + ilen;

    while (p < end && off < ocap - 1) {
        const char *line_end = memchr(p, '\n', end - p);
        size_t line_len = line_end ? (size_t)(line_end - p + 1) : (size_t)(end - p);
        /* Skip comment lines and empty lines */
        if (p[0] != '#' && p[0] != '\0' && line_len > 1) {
            size_t copy = line_len < (ocap - 1 - off) ? line_len : (ocap - 1 - off);
            memcpy(out + off, p, copy);
            off += copy;
        }
        p += line_len;
    }
    out[off] = '\0';
    *olen = off;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * REDUCE — count lines, sum numeric values
 * ───────────────────────────────────────────────────────── */
int prim_reduce_exec(const char *in, size_t ilen,
                     char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)s;
    int    line_count = 0;
    double sum        = 0.0;
    size_t word_count = 0;

    const char *p = in;
    const char *end = in + ilen;
    while (p < end) {
        if (*p == '\n') line_count++;
        double v;
        if (sscanf(p, "%lf", &v) == 1) sum += v;
        /* word count: transitions space->nonspace */
        if (p > in && (*(p-1) == ' ' || *(p-1) == '\t' || *(p-1) == '\n')
                    && *p != ' ' && *p != '\t' && *p != '\n')
            word_count++;
        p++;
    }
    if (ilen > 0 && in[0] != ' ' && in[0] != '\n') word_count++;

    int off = snprintf(out, ocap,
                       "lines=%d words=%zu sum=%.4f bytes=%zu\n",
                       line_count, word_count, sum, ilen);
    *olen = off > 0 ? off : 0;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * COMPOSE — meta-primitive: returns description of composition
 * ───────────────────────────────────────────────────────── */
int prim_compose_exec(const char *in, size_t ilen,
                      char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)ilen;
    int off = snprintf(out, ocap,
                       "composed: input_len=%zu prims=%d energy=%.1f\n%s",
                       ilen, s->composite_count, s->energy, in);
    *olen = off > 0 ? (size_t)off : 0;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * PERSIST — write input to content-addressed store
 * ───────────────────────────────────────────────────────── */
int prim_persist_exec(const char *in, size_t ilen,
                      char *out, size_t ocap, size_t *olen, SeedState *s) {
    char hexhash[SHA256_HEX_LEN];
    int  ret = store_put(s->store_dir, (const uint8_t *)in, ilen, hexhash);
    if (ret == 0) {
        int off = snprintf(out, ocap, "stored:%s\n", hexhash);
        *olen = off > 0 ? off : 0;
        return 0;
    }
    snprintf(out, ocap, "persist_failed\n");
    *olen = strlen(out);
    return -1;
}

/* ─────────────────────────────────────────────────────────
 * RESTORE — retrieve from store by hash in input
 * ───────────────────────────────────────────────────────── */
int prim_restore_exec(const char *in, size_t ilen,
                      char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)ilen;
    char hex[SHA256_HEX_LEN];
    if (sscanf(in, "stored:%64s", hex) != 1 &&
        sscanf(in, "%64s", hex) != 1) {
        snprintf(out, ocap, "restore: no hash in input\n");
        *olen = strlen(out);
        return -1;
    }
    if (!store_has(s->store_dir, hex)) {
        snprintf(out, ocap, "restore: %s not found\n", hex);
        *olen = strlen(out);
        return -1;
    }
    size_t rlen = 0;
    int ret = store_get(s->store_dir, hex, (uint8_t *)out, ocap - 1, &rlen);
    if (ret == 0) { out[rlen] = '\0'; *olen = rlen; return 0; }
    return -1;
}

/* ─────────────────────────────────────────────────────────
 * CHECKPOINT — serialize genome + state summary to disk
 * ───────────────────────────────────────────────────────── */
int prim_checkpoint_exec(const char *in, size_t ilen,
                          char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)in; (void)ilen;
    char snap_path[MAX_PATH_LEN];
    snprintf(snap_path, sizeof(snap_path), "%s/genome.conf", s->snapshots_dir);

    char gbuf[4096];
    genome_serialize(&s->genome, gbuf, sizeof(gbuf));
    int wret = write_file(snap_path, gbuf, strlen(gbuf));

    int off = snprintf(out, ocap,
                       "checkpoint: tick=%llu energy=%.1f score=%.2f composites=%d saved=%s\n",
                       (unsigned long long)s->ticks, s->energy,
                       s->cumulative_score, s->composite_count,
                       wret == 0 ? "ok" : "fail");
    *olen = off > 0 ? off : 0;
    return wret;
}

/* ─────────────────────────────────────────────────────────
 * SCHEDULE — write a cron job or systemd timer for an artifact
 * ───────────────────────────────────────────────────────── */
int prim_schedule_exec(const char *in, size_t ilen,
                        char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)ilen;
    if (s->dry_run || !s->host.has_cron) {
        int off = snprintf(out, ocap, "schedule: dry_run=%d has_cron=%d\n",
                           s->dry_run, s->host.has_cron);
        *olen = off; return 0;
    }

    /* Write a cron entry: run every hour */
    char cron_line[512];
    /* find artifact path in input */
    char artifact[MAX_PATH_LEN] = {0};
    sscanf(in, "artifact_path:%511s", artifact);
    if (!artifact[0]) {
        snprintf(artifact, sizeof(artifact), "%s/latest.sh", s->artifacts_dir);
    }

    snprintf(cron_line, sizeof(cron_line),
             "0 * * * * %s >> %s/cron.log 2>&1\n", artifact, s->base_dir);

    /* Use crontab -l | append | crontab - */
    char cmd[1024];
    char tmp[] = "/tmp/seed_cron_XXXXXX";
    int  fd = mkstemp(tmp);
    if (fd >= 0) {
        close(fd);
        snprintf(cmd, sizeof(cmd),
                 "( crontab -l 2>/dev/null; echo '%s' ) | "
                 "sort -u | crontab -", cron_line);
        int ret = system(cmd);
        int off = snprintf(out, ocap, "schedule: cron=%s ret=%d\n",
                           artifact, WIFEXITED(ret) ? WEXITSTATUS(ret) : -1);
        *olen = off;
        unlink(tmp);
        return WIFEXITED(ret) && WEXITSTATUS(ret) == 0 ? 0 : -1;
    }
    return -1;
}

/* ─────────────────────────────────────────────────────────
 * QUERY — list entries in the store matching a prefix
 * ───────────────────────────────────────────────────────── */
int prim_query_exec(const char *in, size_t ilen,
                    char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)ilen;
    char cmd[MAX_PATH_LEN + 64];
    snprintf(cmd, sizeof(cmd), "find %s -type f 2>/dev/null | head -20", s->store_dir);

    char result[4096] = {0};
    run_cmd(cmd, result, sizeof(result), 5);

    int off = snprintf(out, ocap, "query[%.*s]:\n%s", 32, in, result);
    *olen = off > 0 ? off : 0;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * SIGN — sign the input with the seed's Ed25519 key
 * ───────────────────────────────────────────────────────── */
int prim_sign_exec(const char *in, size_t ilen,
                   char *out, size_t ocap, size_t *olen, SeedState *s) {
    uint8_t sig[SIG_BYTES];
    if (keymgmt_sign(s, (const uint8_t *)in, ilen, sig) != 0) {
        snprintf(out, ocap, "sign:failed\n");
        *olen = strlen(out);
        return -1;
    }
    char sighex[SIG_BYTES * 2 + 1];
    hex_encode(sig, SIG_BYTES, sighex);
    int off = snprintf(out, ocap, "sig:%s\nmsg_len:%zu\n", sighex, ilen);
    *olen = off > 0 ? off : 0;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * VERIFY — verify a signature from sign's output
 * ───────────────────────────────────────────────────────── */
int prim_verify_exec(const char *in, size_t ilen,
                     char *out, size_t ocap, size_t *olen, SeedState *s) {
    char sighex[SIG_BYTES * 2 + 2];
    if (sscanf(in, "sig:%128s", sighex) != 1) {
        int off = snprintf(out, ocap, "verify:no_sig_in_input\n");
        *olen = off; return -1;
    }
    uint8_t sig[SIG_BYTES];
    hex_decode(sighex, sig, SIG_BYTES);

    /* Use the remaining input as message body */
    const char *msg = strstr(in, "\n");
    size_t mlen = msg ? (ilen - (msg - in) - 1) : 0;
    if (msg) msg++;

    int ret = keymgmt_verify(s, (const uint8_t *)(msg ? msg : in),
                              mlen > 0 ? mlen : ilen, sig);
    int off = snprintf(out, ocap, "verify:%s\n", ret == 0 ? "ok" : "fail");
    *olen = off;
    return ret;
}

/* ─────────────────────────────────────────────────────────
 * ADVERTISE — broadcast seed presence via UDP multicast
 * ───────────────────────────────────────────────────────── */
int prim_advertise_exec(const char *in, size_t ilen,
                         char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)in; (void)ilen;

    /* Build announcement payload */
    char payload[512];
    char pubhex[65];
    hex_encode(s->pubkey_bytes, PUBKEY_BYTES, pubhex);
    snprintf(payload, sizeof(payload),
             "SEEDCORE/1 tick=%llu gen=%d pubkey=%.16s... host=%s energy=%.0f",
             (unsigned long long)s->ticks, s->generation,
             pubhex, s->host.hostname, s->energy);

    /* UDP multicast to 239.255.42.99:9999 */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int ret = -1;
    if (sock >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(9999);
        addr.sin_addr.s_addr = inet_addr("239.255.42.99");

        int ttl = 1;
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        /* non-blocking send — don't fail if no route */
        ssize_t sent = sendto(sock, payload, strlen(payload), MSG_DONTWAIT,
                              (struct sockaddr *)&addr, sizeof(addr));
        ret = (sent > 0) ? 0 : -1;
        close(sock);
    }

    int off = snprintf(out, ocap, "advertise:%s sent=%s\n",
                       payload, ret == 0 ? "ok" : "fail_or_no_route");
    *olen = off;
    return 0;  /* Don't fail — no network is non-fatal */
}

/* ─────────────────────────────────────────────────────────
 * MONITOR — read real system metrics from /proc
 * ───────────────────────────────────────────────────────── */
int prim_monitor_exec(const char *in, size_t ilen,
                       char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)in; (void)ilen; (void)s;
    char buf[2048];
    int  off = 0;

    /* Load average */
    if (read_file("/proc/loadavg", buf, sizeof(buf)) > 0) {
        buf[strcspn(buf, "\n")] = '\0';
        off += snprintf(out + off, ocap - off, "loadavg=%s\n", buf);
    }

    /* Memory */
    if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
        unsigned long long total = 0, free_mem = 0, available = 0;
        char *p;
        if ((p = strstr(buf, "MemTotal:")))     sscanf(p, "MemTotal: %llu",     &total);
        if ((p = strstr(buf, "MemFree:")))      sscanf(p, "MemFree: %llu",      &free_mem);
        if ((p = strstr(buf, "MemAvailable:"))) sscanf(p, "MemAvailable: %llu", &available);
        off += snprintf(out + off, ocap - off,
                        "mem_total_mb=%llu mem_free_mb=%llu mem_avail_mb=%llu\n",
                        total/1024, free_mem/1024, available/1024);
    }

    /* CPU stat snapshot */
    if (read_file("/proc/stat", buf, sizeof(buf)) > 0) {
        unsigned long long user, nice, sys, idle, iow;
        if (sscanf(buf, "cpu %llu %llu %llu %llu %llu",
                   &user, &nice, &sys, &idle, &iow) == 5) {
            unsigned long long total_cpu = user + nice + sys + idle + iow;
            double busy_pct = total_cpu > 0
                ? 100.0 * (user + nice + sys) / total_cpu : 0.0;
            off += snprintf(out + off, ocap - off, "cpu_busy_pct=%.1f\n", busy_pct);
        }
    }

    /* Process count */
    if (read_file("/proc/stat", buf, sizeof(buf)) > 0) {
        char *p = strstr(buf, "processes ");
        if (p) {
            unsigned long procs; sscanf(p, "processes %lu", &procs);
            off += snprintf(out + off, ocap - off, "proc_count=%lu\n", procs);
        }
    }

    /* Self RSS */
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        off += snprintf(out + off, ocap - off, "self_rss_kb=%ld\n", ru.ru_maxrss);

    *olen = off;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * FORK — create a child seed with mutated genome
 * ───────────────────────────────────────────────────────── */
int prim_fork_exec(const char *in, size_t ilen,
                   char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)in; (void)ilen;

    if (s->dry_run) {
        int off = snprintf(out, ocap, "fork: dry_run — skipped\n");
        *olen = off; return 0;
    }

    /* Create a mutated child genome file */
    Genome child;
    double mutation_rate = 0.05 + s->genome.g[G8_MUT_D] * 0.25;
    genome_mutate(&s->genome, &child, mutation_rate);

    char child_path[MAX_PATH_LEN];
    char ghex[SHA256_HEX_LEN];
    hex_encode(child.hash, 32, ghex);
    snprintf(child_path, sizeof(child_path), "%s/child_%.12s.conf",
             s->snapshots_dir, ghex);

    char gbuf[4096];
    genome_serialize(&child, gbuf, sizeof(gbuf));
    write_file(child_path, gbuf, strlen(gbuf));

    s->generation++;

    int off = snprintf(out, ocap,
                       "fork: gen=%d mutation_rate=%.3f child=%s\n",
                       s->generation, mutation_rate, child_path);
    *olen = off;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * COMPRESS — gzip compress the input
 * ───────────────────────────────────────────────────────── */
int prim_compress_exec(const char *in, size_t ilen,
                        char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)s;
    uLongf dest_len = compressBound((uLong)ilen);
    if (dest_len + 32 > ocap) {
        snprintf(out, ocap, "compress: output too small\n");
        *olen = strlen(out); return -1;
    }

    uint8_t *tmp = malloc(dest_len);
    if (!tmp) return -1;

    int zret = compress2(tmp, &dest_len, (const Bytef *)in, (uLong)ilen, Z_BEST_SPEED);
    if (zret != Z_OK) {
        free(tmp); return -1;
    }

    /* Encode as hex so output remains printable */
    char hdr[64];
    int hlen = snprintf(hdr, sizeof(hdr), "compressed:orig=%zu:comp=%lu:\n", ilen, dest_len);

    if ((size_t)hlen + dest_len * 2 + 1 > ocap) {
        free(tmp);
        snprintf(out, ocap, "compress: encoded output too large\n");
        *olen = strlen(out); return -1;
    }

    memcpy(out, hdr, hlen);
    hex_encode(tmp, dest_len, out + hlen);
    free(tmp);
    *olen = hlen + dest_len * 2;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * DECOMPRESS — gzip decompress (expects compress output)
 * ───────────────────────────────────────────────────────── */
int prim_decompress_exec(const char *in, size_t ilen,
                          char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)s; (void)ilen;
    size_t orig = 0; size_t comp = 0;
    const char *hex_start = strstr(in, ":\n");
    if (!hex_start) { snprintf(out, ocap, "decompress: bad input\n"); *olen = strlen(out); return -1; }
    sscanf(in, "compressed:orig=%zu:comp=%zu:", &orig, &comp);
    hex_start += 2;

    uint8_t *cbuf = malloc(comp);
    if (!cbuf) return -1;
    hex_decode(hex_start, cbuf, comp);

    uLongf dest_len = (uLongf)ocap - 1;
    int zret = uncompress((Bytef *)out, &dest_len, cbuf, (uLong)comp);
    free(cbuf);
    if (zret != Z_OK) { snprintf(out, ocap, "decompress: z error %d\n", zret); *olen = strlen(out); return -1; }
    out[dest_len] = '\0';
    *olen = dest_len;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * LEARN — update genome weights based on recent outcomes
 * ───────────────────────────────────────────────────────── */
int prim_learn_exec(const char *in, size_t ilen,
                    char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)ilen;
    /* Parse outcome from input: "outcome=0.75 type=4" */
    double outcome = 0.5;
    int    ptype   = PRIM_PROBE;
    sscanf(in, "outcome=%lf type=%d", &outcome, &ptype);
    if (ptype < 0 || ptype >= PRIM_TYPE_COUNT) ptype = PRIM_PROBE;

    /* Compute rolling average score */
    ScoreHistory *sh = &s->score_hist;
    double avg = 0.0;
    if (sh->recent_count > 0) {
        for (int i = 0; i < sh->recent_count; i++)
            avg += sh->recent_scores[i];
        avg /= sh->recent_count;
    } else {
        avg = 0.5;
    }

    /* Update genome */
    genome_update_from_outcome(&s->genome, (PrimType)ptype, outcome, avg);

    /* Record in history */
    sh->recent_scores[sh->recent_head] = outcome;
    sh->recent_head = (sh->recent_head + 1) % 64;
    if (sh->recent_count < 64) sh->recent_count++;

    int off = snprintf(out, ocap,
                       "learn: type=%d outcome=%.3f avg=%.3f g0=%.3f g13=%.3f g14=%.3f\n",
                       ptype, outcome, avg,
                       s->genome.g[G0_RES_ACQ],
                       s->genome.g[G13_BYPASS],
                       s->genome.g[G14_REACT_I]);
    *olen = off;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * BYPASS — attempt alternative resource acquisition
 * ───────────────────────────────────────────────────────── */
int prim_bypass_exec(const char *in, size_t ilen,
                     char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)in; (void)ilen;

    /* g13: higher = more aggressive bypass attempts */
    double success_prob = 0.15 + s->genome.g[G13_BYPASS] * 0.55;
    int    succeeded    = ((double)rand() / RAND_MAX) < success_prob;

    if (succeeded) {
        /* Try to reclaim disk space or free cache */
        if (!s->dry_run) {
            run_cmd("sync && echo 1 > /proc/sys/vm/drop_caches 2>/dev/null || true",
                    NULL, 0, 3);
        }
        s->energy = s->energy + 10.0 > 100.0 ? 100.0 : s->energy + 10.0;
    }

    int off = snprintf(out, ocap,
                       "bypass: g13=%.3f prob=%.3f %s energy_now=%.1f\n",
                       s->genome.g[G13_BYPASS], success_prob,
                       succeeded ? "SUCCESS" : "FAILED", s->energy);
    *olen = off;
    return succeeded ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────
 * SCORE — compute gene-weighted score for candidate action type
 * ───────────────────────────────────────────────────────── */
int prim_score_exec(const char *in, size_t ilen,
                    char *out, size_t ocap, size_t *olen, SeedState *s) {
    (void)ilen;
    int ptype = PRIM_PROBE;
    double utility = 0.5;
    sscanf(in, "type=%d utility=%lf", &ptype, &utility);
    if (ptype < 0 || ptype >= PRIM_TYPE_COUNT) ptype = PRIM_PROBE;

    double score = genome_score_cand(&s->genome, (PrimType)ptype,
                                      utility, 0, &s->score_hist, &s->host);
    int off = snprintf(out, ocap,
                       "score: type=%d utility=%.3f gene_score=%.4f threshold=%.2f %s\n",
                       ptype, utility, score, SCORE_PROMOTE_THRESH,
                       score >= SCORE_PROMOTE_THRESH ? "PROMOTE" : "DISCARD");
    *olen = off;
    return 0;
}
