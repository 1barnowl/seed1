#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

#include "seed.h"

/* Forward decl for entropy updater from genome.c */
extern void scorehistory_update_entropy(ScoreHistory *sh,
                                         int action_type_hist[PRIM_TYPE_COUNT],
                                         int total);
extern void genome_update_from_outcome(Genome *g, PrimType ptype,
                                        double outcome_score, double baseline);

/* ── Probe: read host environment ────────────────────────── */
static void probe_host(SeedState *s) {
    HostEnv *h = &s->host;
    char buf[4096];

    run_cmd("uname -s", buf, sizeof(buf), 3);
    buf[strcspn(buf, "\n")] = '\0';
    snprintf(h->os_name, sizeof(h->os_name), "%s", buf);

    run_cmd("uname -r", buf, sizeof(buf), 3);
    buf[strcspn(buf, "\n")] = '\0';
    snprintf(h->os_release, sizeof(h->os_release), "%s", buf);

    run_cmd("uname -m", buf, sizeof(buf), 3);
    buf[strcspn(buf, "\n")] = '\0';
    snprintf(h->arch, sizeof(h->arch), "%s", buf);

    run_cmd("nproc", buf, sizeof(buf), 3);
    h->cpu_cores = atoi(buf);
    if (h->cpu_cores < 1) h->cpu_cores = 1;

    /* Memory */
    if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
        sscanf(strstr(buf, "MemTotal:") ? strstr(buf, "MemTotal:") : buf,
               "MemTotal: %llu", (unsigned long long *)&h->mem_total_kb);
        const char *mf = strstr(buf, "MemAvailable:");
        if (mf) sscanf(mf, "MemAvailable: %llu", (unsigned long long *)&h->mem_free_kb);
    }

    /* Disk */
    run_cmd("df -k $HOME --output=avail | tail -1", buf, sizeof(buf), 3);
    h->disk_free_kb = strtoull(buf, NULL, 10);

    /* Load average */
    if (read_file("/proc/loadavg", buf, sizeof(buf)) > 0)
        sscanf(buf, "%lf", &h->load_avg);

    /* Hostname */
    gethostname(h->hostname, sizeof(h->hostname));

    /* Username */
    const char *user = getenv("USER");
    snprintf(h->username, sizeof(h->username), "%s", user ? user : "unknown");

    /* Available tools */
    h->has_python3 = cmd_exists("python3");
    h->has_node    = cmd_exists("node");
    h->has_curl    = cmd_exists("curl");
    h->has_wget    = cmd_exists("wget");
    h->has_cron    = cmd_exists("crontab");
    h->has_systemd = file_exists("/run/systemd/private");
    h->has_docker  = cmd_exists("docker");
    h->has_git     = cmd_exists("git");

    /* IP address */
    run_cmd("hostname -I 2>/dev/null | awk '{print $1}'", buf, sizeof(buf), 3);
    buf[strcspn(buf, "\n ")] = '\0';
    snprintf(h->ipv4, sizeof(h->ipv4), "%s", buf);

    h->probed_at = time(NULL);

    seed_log(s, "host: %s %s %s cores=%d mem=%lluMB disk=%lluMB",
             h->os_name, h->os_release, h->arch,
             h->cpu_cores,
             (unsigned long long)(h->mem_total_kb / 1024),
             (unsigned long long)(h->disk_free_kb / 1024));
    seed_log(s, "tools: python3=%d node=%d curl=%d cron=%d systemd=%d docker=%d",
             h->has_python3, h->has_node, h->has_curl,
             h->has_cron, h->has_systemd, h->has_docker);
}

/* ── Write a promoted artifact to disk ──────────────────── */
static int write_artifact(SeedState *s, const char **step_ids, int n,
                           const char *output, size_t olen,
                           char *artifact_path_out, size_t ap_cap) {
    if (s->dry_run) {
        snprintf(artifact_path_out, ap_cap, "(dry-run)");
        return 0;
    }

    /* Build a self-contained shell script artifact */
    char script[MAX_IO_BUF * 2];
    int  off = 0;

    off += snprintf(script + off, sizeof(script) - off,
        "#!/usr/bin/env sh\n"
        "# SEED CORE artifact — generated at tick %llu\n"
        "# Composite pipeline: ",
        (unsigned long long)s->ticks);

    for (int i = 0; i < n; i++)
        off += snprintf(script + off, sizeof(script) - off,
                        "%s%s", step_ids[i], i < n - 1 ? " -> " : "\n");

    char genome_hex[65] = "unknown";
    if (s->genome.hash[0])
        hex_encode(s->genome.hash, 32, genome_hex);

    off += snprintf(script + off, sizeof(script) - off,
        "# Genome: %.8s...\n"
        "# Score threshold: %.2f\n\n"
        "set -e\n"
        "SEED_DIR=\"${HOME}/.seedcore\"\n"
        "STORE_DIR=\"${SEED_DIR}/store\"\n\n",
        genome_hex,
        SCORE_PROMOTE_THRESH);

    /* Embed pipeline logic based on step names */
    for (int i = 0; i < n; i++) {
        off += snprintf(script + off, sizeof(script) - off,
                        "# Step %d: %s\n", i + 1, step_ids[i]);

        if (strncmp(step_ids[i], "probe", 5) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "uname -a\ncat /proc/meminfo | grep -E 'MemTotal|MemFree'\n"
                "df -h $HOME\n\n");
        } else if (strncmp(step_ids[i], "monitor", 7) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "cat /proc/loadavg\nfree -h\n\n");
        } else if (strncmp(step_ids[i], "compress", 8) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "INPUT=\"${1:-/tmp/seed_data}\"\n"
                "[ -f \"$INPUT\" ] && gzip -k \"$INPUT\"\n\n");
        } else if (strncmp(step_ids[i], "checkpoint", 10) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "SNAP=\"${SEED_DIR}/snapshots/artifact_snap_$(date +%%s).txt\"\n"
                "echo \"checkpoint at $(date)\" > \"$SNAP\"\n\n");
        } else if (strncmp(step_ids[i], "persist", 7) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "DATA=\"${1:-$(date)}\"\n"
                "HASH=$(echo -n \"$DATA\" | sha256sum | awk '{print $1}')\n"
                "mkdir -p \"${STORE_DIR}/${HASH:0:2}\"\n"
                "echo \"$DATA\" > \"${STORE_DIR}/${HASH:0:2}/${HASH:2}\"\n"
                "echo \"stored: $HASH\"\n\n");
        } else if (strncmp(step_ids[i], "map", 3) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "# map: transform each input line\n"
                "cat \"${1:--}\" | awk '{print toupper($0)}'\n\n");
        } else if (strncmp(step_ids[i], "filter", 6) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "# filter: pass lines matching pattern\n"
                "cat \"${1:--}\" | grep -v '^#'\n\n");
        } else if (strncmp(step_ids[i], "reduce", 6) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "# reduce: aggregate (word count)\n"
                "cat \"${1:--}\" | wc -l\n\n");
        } else if (strncmp(step_ids[i], "learn", 5) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "# learn: log outcome for genome update\n"
                "echo \"learn @ $(date)\" >> \"${SEED_DIR}/learn.log\"\n\n");
        } else if (strncmp(step_ids[i], "bypass", 6) == 0) {
            off += snprintf(script + off, sizeof(script) - off,
                "# bypass: attempt alternative resource path\n"
                "TMPDIR=\"${TMPDIR:-/tmp}\"\n"
                "echo \"bypass via $TMPDIR\" >&2\n\n");
        } else {
            off += snprintf(script + off, sizeof(script) - off,
                "echo '%s executed'\n\n", step_ids[i]);
        }
    }

    /* Append captured output as comment */
    if (olen > 0 && olen < 1024) {
        off += snprintf(script + off, sizeof(script) - off,
                        "# Captured output from verifier:\n");
        for (size_t oi = 0; oi < olen; oi++) {
            if (output[oi] == '\n')
                off += snprintf(script + off, sizeof(script) - off, "\n# ");
            else if (off < (int)sizeof(script) - 2)
                script[off++] = output[oi];
        }
        script[off++] = '\n';
    }

    off += snprintf(script + off, sizeof(script) - off,
                    "\necho '[seed] artifact complete'\n");

    /* Hash the script content for unique filename */
    char hexhash[SHA256_HEX_LEN];
    sha256_hex((const uint8_t *)script, off, hexhash);

    snprintf(artifact_path_out, ap_cap, "%s/%s.sh", s->artifacts_dir, hexhash);

    if (!file_exists(artifact_path_out)) {
        write_file(artifact_path_out, script, off);
        chmod(artifact_path_out, 0755);

        /* Also store in content-addressed store */
        char stored_hex[SHA256_HEX_LEN];
        store_put(s->store_dir, (const uint8_t *)script, off, stored_hex);

        seed_log(s, "artifact written: %s", artifact_path_out);
    }

    return 0;
}

/* ── Load existing composites from artifacts dir ─────────── */
static void load_composites(SeedState *s) {
    char idx_path[MAX_PATH_LEN];
    snprintf(idx_path, sizeof(idx_path), "%s/composites.idx", s->snapshots_dir);
    if (!file_exists(idx_path)) return;

    char buf[MAX_IO_BUF];
    if (read_file(idx_path, buf, sizeof(buf)) <= 0) return;

    char *line = buf;
    while (*line && s->composite_count < MAX_COMPOSITES) {
        Composite *c = &s->composites[s->composite_count];
        char steps_str[512];
        if (sscanf(line, "%63s %lf %511s %511s",
                   c->id, &c->score, steps_str, c->artifact_path) >= 2) {
            /* Parse steps */
            char *tok = strtok(steps_str, ",");
            while (tok && c->step_count < MAX_COMPOSITE_STEPS) {
                snprintf(c->step_ids[c->step_count++], MAX_PRIM_NAME, "%s", tok);
                tok = strtok(NULL, ",");
            }
            s->composite_count++;
        }
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }

    seed_log(s, "loaded %d composites from index", s->composite_count);
}

/* ── Save composite index ────────────────────────────────── */
static void save_composite_index(SeedState *s) {
    char idx_path[MAX_PATH_LEN];
    snprintf(idx_path, sizeof(idx_path), "%s/composites.idx", s->snapshots_dir);

    char buf[MAX_IO_BUF];
    int  off = 0;

    for (int i = 0; i < s->composite_count; i++) {
        Composite *c = &s->composites[i];
        off += snprintf(buf + off, sizeof(buf) - off, "%s %.4f ", c->id, c->score);
        for (int j = 0; j < c->step_count; j++)
            off += snprintf(buf + off, sizeof(buf) - off,
                            "%s%s", c->step_ids[j], j < c->step_count - 1 ? "," : "");
        off += snprintf(buf + off, sizeof(buf) - off, " %s\n", c->artifact_path);
    }

    write_file(idx_path, buf, off);
}

/* ── Growth init ─────────────────────────────────────────── */
int growth_init(SeedState *s) {
    probe_host(s);
    load_composites(s);
    seed_log(s, "growth init complete — %d composites in memory", s->composite_count);
    return 0;
}

/* ── Promote a composite ─────────────────────────────────── */
int growth_promote_composite(SeedState *s, const char **step_ids, int n,
                              double score, const VerifierResult *r) {
    if (s->composite_count >= MAX_COMPOSITES) return -1;

    Composite *c = &s->composites[s->composite_count];
    memset(c, 0, sizeof(*c));

    /* Build a unique ID from step names + tick */
    char id_src[512];
    int  off = 0;
    for (int i = 0; i < n; i++)
        off += snprintf(id_src + off, sizeof(id_src) - off, "%s_", step_ids[i]);
    snprintf(id_src + off, sizeof(id_src) - off, "%llu",
             (unsigned long long)s->ticks);

    char idhex[SHA256_HEX_LEN];
    sha256_hex((const uint8_t *)id_src, strlen(id_src), idhex);
    snprintf(c->id, sizeof(c->id), "comp_%.12s", idhex);

    c->step_count = n;
    for (int i = 0; i < n; i++)
        snprintf(c->step_ids[i], MAX_PRIM_NAME, "%s", step_ids[i]);

    c->score    = score;
    c->created  = time(NULL);
    sha256_hex((const uint8_t *)id_src, strlen(id_src), idhex);

    /* Write artifact to disk */
    write_artifact(s, step_ids, n,
                   r->output, r->output_len,
                   c->artifact_path, sizeof(c->artifact_path));

    s->composite_count++;
    save_composite_index(s);

    seed_log(s, "PROMOTED composite %s (score=%.3f steps=%d) -> %s",
             c->id, score, n, c->artifact_path);
    return 0;
}

/* ── Single growth tick ──────────────────────────────────── */
void growth_loop(SeedState *s) {
    s->ticks++;
    double tick_score = 0.0;

    /* ── Phase 1: Sense environment (every 30 ticks) ── */
    if (s->ticks % 30 == 0) {
        probe_host(s);
    }

    /* ── Phase 2: Generate candidate composite ──────── */
    int    n_steps = 1 + (int)(drand_range(0, 1) * 
                                (2.0 + s->genome.g[G12_EXPLOR] * 3.0));
    if (n_steps > MAX_COMPOSITE_STEPS) n_steps = MAX_COMPOSITE_STEPS;

    const char *step_ids[MAX_COMPOSITE_STEPS];
    char        step_id_bufs[MAX_COMPOSITE_STEPS][MAX_PRIM_NAME];

    for (int i = 0; i < n_steps; i++) {
        /* Pick a random primitive weighted by gene values */
        int idx = (int)(drand_range(0, 1) * s->prim_count) % s->prim_count;
        snprintf(step_id_bufs[i], MAX_PRIM_NAME, "%s", s->prims[idx].id);
        step_ids[i] = step_id_bufs[i];
    }

    /* ── Phase 3: Score candidates via genome ───────── */
    /* Determine primary type from first step */
    Primitive *first = prim_find(s, step_ids[0]);
    PrimType   ctype = first ? first->type : PRIM_PROBE;

    /* Compute predicted utility from primitive success rates */
    double utility = 0.5;
    if (first && first->exec_count > 5)
        utility = (double)first->success_count / first->exec_count;

    int triggered_ext = (s->ticks % 7 == 0) ? 1 : 0;

    double gene_score = genome_score_cand(
        &s->genome, ctype, utility, triggered_ext,
        &s->score_hist, &s->host);

    /* g11 path deviation: occasionally pick a suboptimal candidate */
    if (drand_range(0, 1) < s->genome.g[G11_PATH_DV]) {
        int alt = (int)(drand_range(0, 1) * s->prim_count) % s->prim_count;
        snprintf(step_id_bufs[0], MAX_PRIM_NAME, "%s", s->prims[alt].id);
        gene_score += s->genome.g[G11_PATH_DV] * 0.08; /* exploration reward */
    }

    /* ── Phase 4: Run composite in verifier ─────────── */
    VerifierResult vr;
    char test_input[256];
    snprintf(test_input, sizeof(test_input),
             "tick=%llu host=%s energy=%.1f",
             (unsigned long long)s->ticks, s->host.hostname, s->energy);

    int vret = verifier_run_composite(s, step_ids, n_steps,
                                       test_input, strlen(test_input), &vr);

    if (s->verbose || s->ticks % 5 == 0) {
        seed_log(s, "tick=%llu steps=%d [%s→...] verifier=%s score=%.3f lat=%.0fms",
                 (unsigned long long)s->ticks, n_steps, step_ids[0],
                 vret == 0 ? "OK" : "FAIL", vr.score, vr.latency_ms);
    }

    /* ── Phase 5: Score result + gene bonus ─────────── */
    double final_score = 0.0;
    if (vret == 0) {
        final_score = vr.score + (gene_score - 0.5) * 0.3;
        tick_score  = final_score;
    }

    /* ── Phase 6: Promote if above threshold ────────── */
    if (final_score >= SCORE_PROMOTE_THRESH && vret == 0) {
        growth_promote_composite(s, step_ids, n_steps, final_score, &vr);
        s->energy = s->energy > 95 ? 100 : s->energy + 1.5;
    }

    /* ── Phase 7: Gene learning ─────────────────────── */
    genome_update_from_outcome(&s->genome, ctype, final_score, 0.5);

    /* ── Phase 8: Energy accounting ─────────────────── */
    s->energy -= 0.15 + n_steps * 0.05;
    if (s->energy < 5.0) {
        /* Scarcity: trigger bypass */
        seed_log(s, "SCARCITY: energy=%.1f — triggering g4/g13 bypass", s->energy);
        s->energy = 20.0;  /* minimal recovery */
    }
    if (s->energy > 100.0) s->energy = 100.0;

    /* ── Phase 9: Update action history + entropy ────── */
    if (ctype >= 0 && ctype < PRIM_TYPE_COUNT) {
        s->action_type_hist[ctype]++;
        s->action_total++;
    }
    if (s->ticks % 10 == 0)
        scorehistory_update_entropy(&s->score_hist,
                                    s->action_type_hist, s->action_total);

    s->cumulative_score += tick_score;

    /* ── Phase 10: Periodic checkpoint ─────────────── */
    static time_t last_checkpoint = 0;
    time_t now = time(NULL);
    if (now - last_checkpoint >= CHECKPOINT_INTERVAL) {
        last_checkpoint = now;
        char snap_path[MAX_PATH_LEN];
        snprintf(snap_path, sizeof(snap_path), "%s/genome.conf", s->snapshots_dir);
        char gbuf[4096];
        genome_serialize(&s->genome, gbuf, sizeof(gbuf));
        write_file(snap_path, gbuf, strlen(gbuf));

        /* Also write a timestamped snapshot */
        snprintf(snap_path, sizeof(snap_path), "%s/genome_%llu.conf",
                 s->snapshots_dir, (unsigned long long)now);
        write_file(snap_path, gbuf, strlen(gbuf));

        seed_log(s, "checkpoint: tick=%llu score=%.2f energy=%.1f composites=%d",
                 (unsigned long long)s->ticks, s->cumulative_score,
                 s->energy, s->composite_count);
    }

    /* ── Rate limiting: don't burn CPU needlessly ───── */
    /* Short sleep scaled by g14 (proactive = faster loop) */
    int sleep_us = (int)((1.0 - s->genome.g[G14_REACT_I] * 0.7) * 200000);
    usleep(sleep_us > 10000 ? sleep_us : 10000);
}
