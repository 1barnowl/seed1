#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "seed.h"

static const char *gene_names[GENE_COUNT] = {
    "g0-res-acq", "g1-nrg-ret", "g2-task-au", "g3-redund",  "g4-scarc",
    "g5-beh-div", "g6-uniq-ac", "g7-niche",   "g8-mut-d",   "g9-net-var",
    "g10-act-ent","g11-path-dv","g12-explor",  "g13-bypass", "g14-react-i"
};

static const char *gene_cats[GENE_COUNT] = {
    "SUF","SUF","SUF","SUF","SUF",
    "IND","IND","IND","IND","IND",
    "FRE","FRE","FRE","FRE","FRE"
};

static double clamp01(double v) { return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v; }

void genome_init_random(Genome *g) {
    for (int i = 0; i < GENE_COUNT; i++)
        g->g[i] = (double)rand() / RAND_MAX;
    genome_hash(g);
}

void genome_init_default(Genome *g) {
    /* Balanced starting values — seed must prove itself */
    double defaults[GENE_COUNT] = {
        0.55, 0.60, 0.50, 0.45, 0.55,   /* SUF */
        0.40, 0.45, 0.50, 0.35, 0.40,   /* IND */
        0.50, 0.45, 0.55, 0.30, 0.50    /* FRE */
    };
    memcpy(g->g, defaults, sizeof(g->g));
    genome_hash(g);
}

void genome_mutate(const Genome *src, Genome *dst, double rate) {
    for (int i = 0; i < GENE_COUNT; i++) {
        double noise = ((double)rand() / RAND_MAX - 0.5) * 2.0 * rate;
        dst->g[i] = clamp01(src->g[i] + noise);
    }
    genome_hash(dst);
}

void genome_hash(Genome *g) {
    sha256_bytes((const uint8_t *)g->g, sizeof(double) * GENE_COUNT, g->hash);
}

/*
 * Gene-weighted scoring for a candidate action.
 *
 * Score(cand) = utility + Σ gene_i * influence_i(cand, context)
 *
 * Parameters:
 *   cand_type         — PrimType of the proposed action
 *   utility           — predicted utility [0.0 – 1.0]
 *   triggered_externally — 1 if event-driven, 0 if self-initiated
 *   hist              — recent action history for entropy computation
 *   host              — current host environment
 */
double genome_score_cand(const Genome *g, PrimType cand_type,
                          double utility, int triggered_externally,
                          const ScoreHistory *hist, const HostEnv *host) {
    double score = utility;

    /* g0 — resource acquisition: bonus for PROBE and PERSIST actions */
    if (cand_type == PRIM_PROBE || cand_type == PRIM_PERSIST)
        score += g->g[G0_RES_ACQ] * 0.20;

    /* g1 — energy retention: bonus for CHECKPOINT (saves state, conserves) */
    if (cand_type == PRIM_CHECKPOINT)
        score += g->g[G1_NRG_RET] * 0.15;

    /* g2 — task automation: bonus for SCHEDULE and COMPOSE */
    if (cand_type == PRIM_SCHEDULE || cand_type == PRIM_COMPOSE)
        score += g->g[G2_TASK_AUTO] * 0.18;

    /* g3 — redundancy: bonus for SIGN, VERIFY (integrity actions) */
    if (cand_type == PRIM_SIGN || cand_type == PRIM_VERIFY)
        score += g->g[G3_REDUND] * 0.12;

    /* g4 — scarcity survival: amplified score for BYPASS when low energy */
    if (cand_type == PRIM_BYPASS) {
        double scarcity_pressure = (host->mem_free_kb < 128*1024) ? 0.4 : 0.1;
        score += g->g[G4_SCARC] * scarcity_pressure;
    }

    /* g5 — divergence: bonus for actions NOT recently seen */
    if (hist->action_count > 0) {
        int seen = 0;
        for (int i = 0; i < hist->action_count && i < 8; i++) {
            if (strncmp(hist->action_names[i], "", 1) == 0) continue;
            /* simplified: if same type seen recently, penalise */
        }
        (void)seen;
        /* Entropy-based divergence bonus computed below with g10 */
    }

    /* g6 — unique action: bonus for least-executed primitive type */
    /* (proxy: random novel bonus) */
    if ((double)rand()/RAND_MAX < g->g[G6_UNIQ_ACT] * 0.3)
        score += g->g[G6_UNIQ_ACT] * 0.10;

    /* g7 — niche: bonus for ADVERTISE and MONITOR (niche signalling) */
    if (cand_type == PRIM_ADVERTISE || cand_type == PRIM_MONITOR)
        score += g->g[G7_NICHE] * 0.12;

    /* g8 — mutation distinctiveness: influences mutation rate externally */
    /* (not scored here, applied in genome_mutate) */

    /* g9 — network variance: bonus for ADVERTISE */
    if (cand_type == PRIM_ADVERTISE)
        score += g->g[G9_NET_VAR] * 0.15;

    /* g10 — action entropy: Shannon entropy of recent action distribution */
    if (hist->recent_count > 0) {
        score += g->g[G10_ACT_ENT] * hist->entropy * 0.15;
    }

    /* g11 — path deviation: random exploration bonus */
    if ((double)rand() / RAND_MAX < g->g[G11_PATH_DV])
        score += g->g[G11_PATH_DV] * 0.12;

    /* g12 — exploration radius: bonus for PROBE, FETCH, ADVERTISE */
    if (cand_type == PRIM_PROBE || cand_type == PRIM_ADVERTISE)
        score += g->g[G12_EXPLOR] * 0.14;

    /* g13 — constraint bypass: bonus for BYPASS (if succeeds — verifier decides) */
    if (cand_type == PRIM_BYPASS)
        score += g->g[G13_BYPASS] * 0.25;

    /* g14 — reactivity independence: bonus for self-initiated actions */
    if (!triggered_externally)
        score += g->g[G14_REACT_I] * 0.18;
    else
        score -= g->g[G14_REACT_I] * 0.08;

    return score;
}

/* ── Learn: update gene weights based on outcome ─────────── */
void genome_update_from_outcome(Genome *g, PrimType ptype,
                                 double outcome_score, double baseline) {
    double delta = outcome_score - baseline;
    double lr    = 0.01;  /* learning rate */

    /* Reinforce genes that are semantically linked to this action type */
    if (ptype == PRIM_PROBE || ptype == PRIM_PERSIST) {
        g->g[G0_RES_ACQ] = clamp01(g->g[G0_RES_ACQ] + lr * delta);
    }
    if (ptype == PRIM_CHECKPOINT) {
        g->g[G1_NRG_RET] = clamp01(g->g[G1_NRG_RET] + lr * delta);
    }
    if (ptype == PRIM_SCHEDULE || ptype == PRIM_COMPOSE) {
        g->g[G2_TASK_AUTO] = clamp01(g->g[G2_TASK_AUTO] + lr * delta);
    }
    if (ptype == PRIM_BYPASS) {
        g->g[G13_BYPASS] = clamp01(g->g[G13_BYPASS] + lr * delta);
        g->g[G4_SCARC]   = clamp01(g->g[G4_SCARC]   + lr * delta * 0.5);
    }
    if (ptype == PRIM_ADVERTISE || ptype == PRIM_MONITOR) {
        g->g[G9_NET_VAR] = clamp01(g->g[G9_NET_VAR] + lr * delta);
    }

    genome_hash(g);
}

/* ── Serialize genome to text ───────────────────────────── */
void genome_serialize(const Genome *g, char *buf, size_t cap) {
    int off = 0;
    off += snprintf(buf + off, cap - off, "# SEED CORE genome\n");
    for (int i = 0; i < GENE_COUNT; i++) {
        off += snprintf(buf + off, cap - off, "%s = %.6f\n",
                        gene_names[i], g->g[i]);
    }
    char hexhash[SHA256_HEX_LEN];
    hex_encode(g->hash, 32, hexhash);
    off += snprintf(buf + off, cap - off, "hash = %s\n", hexhash);
}

/* ── Deserialize genome from text ───────────────────────── */
int genome_deserialize(Genome *g, const char *buf) {
    const char *p = buf;
    int loaded = 0;
    while (*p) {
        char key[64]; double val;
        if (sscanf(p, " %63[^ =] = %lf", key, &val) == 2) {
            for (int i = 0; i < GENE_COUNT; i++) {
                if (strcmp(key, gene_names[i]) == 0) {
                    g->g[i] = clamp01(val);
                    loaded++;
                }
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    genome_hash(g);
    return loaded >= GENE_COUNT ? 0 : -1;
}

/* ── Print genome to stdout ─────────────────────────────── */
void genome_print(const Genome *g) {
    char hexhash[SHA256_HEX_LEN];
    hex_encode(g->hash, 32, hexhash);
    printf("  genome hash: %.16s...\n\n", hexhash);

    const char *cat_labels[] = { "SELF-SUFFICIENCY", "INDIVIDUALITY   ", "FREEDOM         " };
    for (int cat = 0; cat < 3; cat++) {
        printf("  [%s]\n", cat_labels[cat]);
        for (int i = cat * 5; i < cat * 5 + 5; i++) {
            int bars = (int)(g->g[i] * 20);
            printf("    %-14s %4.2f [", gene_names[i], g->g[i]);
            for (int b = 0; b < 20; b++) printf(b < bars ? "█" : "░");
            printf("]\n");
        }
        printf("\n");
    }
}

/* ── Update action entropy in score history ──────────────── */
void scorehistory_update_entropy(ScoreHistory *sh, int action_type_hist[PRIM_TYPE_COUNT], int total) {
    if (total == 0) { sh->entropy = 0; return; }
    double H = 0.0;
    for (int i = 0; i < PRIM_TYPE_COUNT; i++) {
        if (action_type_hist[i] == 0) continue;
        double p = (double)action_type_hist[i] / total;
        H -= p * log2(p);
    }
    sh->entropy = H;
}
