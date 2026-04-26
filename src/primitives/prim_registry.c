#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seed.h"

/* Forward declarations for all primitive exec functions */
int prim_probe_exec     (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_map_exec       (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_filter_exec    (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_reduce_exec    (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_compose_exec   (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_persist_exec   (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_restore_exec   (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_checkpoint_exec(const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_schedule_exec  (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_query_exec     (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_sign_exec      (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_verify_exec    (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_advertise_exec (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_monitor_exec   (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_fork_exec      (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_compress_exec  (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_decompress_exec(const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_learn_exec     (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_bypass_exec    (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);
int prim_score_exec     (const char *in, size_t ilen, char *out, size_t ocap, size_t *olen, SeedState *s);

/* ── Static primitive table ──────────────────────────────── */
static const struct {
    const char *id;
    const char *name;
    PrimType    type;
    prim_fn     exec;
    double      base_score;
} PRIM_TABLE[] = {
    { "probe",      "Environment Probe",      PRIM_PROBE,      prim_probe_exec,      0.70 },
    { "map",        "Line Map Transform",      PRIM_MAP,        prim_map_exec,        0.55 },
    { "filter",     "Line Filter",             PRIM_FILTER,     prim_filter_exec,     0.50 },
    { "reduce",     "Aggregation Reduce",      PRIM_REDUCE,     prim_reduce_exec,     0.50 },
    { "compose",    "Pipeline Compose",        PRIM_COMPOSE,    prim_compose_exec,    0.60 },
    { "persist",    "Store Persist",           PRIM_PERSIST,    prim_persist_exec,    0.65 },
    { "restore",    "Store Restore",           PRIM_RESTORE,    prim_restore_exec,    0.55 },
    { "checkpoint", "State Checkpoint",        PRIM_CHECKPOINT, prim_checkpoint_exec, 0.72 },
    { "schedule",   "Task Scheduler",          PRIM_SCHEDULE,   prim_schedule_exec,   0.60 },
    { "query",      "Store Query",             PRIM_QUERY,      prim_query_exec,      0.50 },
    { "sign",       "Cryptographic Sign",      PRIM_SIGN,       prim_sign_exec,       0.65 },
    { "verify",     "Signature Verify",        PRIM_VERIFY,     prim_verify_exec,     0.65 },
    { "advertise",  "Network Advertise",       PRIM_ADVERTISE,  prim_advertise_exec,  0.55 },
    { "monitor",    "Resource Monitor",        PRIM_MONITOR,    prim_monitor_exec,    0.60 },
    { "fork",       "Speciation Fork",         PRIM_FORK,       prim_fork_exec,       0.45 },
    { "compress",   "Data Compress",           PRIM_COMPRESS,   prim_compress_exec,   0.60 },
    { "decompress", "Data Decompress",         PRIM_DECOMPRESS, prim_decompress_exec, 0.55 },
    { "learn",      "Genome Learn Update",     PRIM_LEARN,      prim_learn_exec,      0.68 },
    { "bypass",     "Constraint Bypass",       PRIM_BYPASS,     prim_bypass_exec,     0.35 },
    { "score",      "Candidate Score",         PRIM_SCORE,      prim_score_exec,      0.55 },
};

#define N_PRIMS ((int)(sizeof(PRIM_TABLE)/sizeof(PRIM_TABLE[0])))

int prim_registry_init(SeedState *s) {
    s->prim_count = 0;
    for (int i = 0; i < N_PRIMS && s->prim_count < MAX_PRIM_COUNT; i++) {
        Primitive *p = &s->prims[s->prim_count++];
        snprintf(p->id,   sizeof(p->id),   "%s", PRIM_TABLE[i].id);
        snprintf(p->name, sizeof(p->name), "%s", PRIM_TABLE[i].name);
        p->type       = PRIM_TABLE[i].type;
        p->exec       = PRIM_TABLE[i].exec;
        p->base_score = PRIM_TABLE[i].base_score;
        p->atomic     = 1;
    }
    seed_log(s, "registered %d atomic primitives", s->prim_count);
    return 0;
}

Primitive *prim_find(SeedState *s, const char *id) {
    for (int i = 0; i < s->prim_count; i++) {
        if (strcmp(s->prims[i].id, id) == 0)
            return &s->prims[i];
    }
    return NULL;
}

int prim_exec(SeedState *s, const char *id,
              const char *input, size_t ilen,
              char *output, size_t ocap, size_t *olen) {
    Primitive *p = prim_find(s, id);
    if (!p) return -1;
    VerifierResult r;
    int ret = verifier_run_primitive(p, input, ilen, s, &r);
    if (ret == 0 && output && olen) {
        size_t copy = r.output_len < ocap ? r.output_len : ocap - 1;
        memcpy(output, r.output, copy);
        output[copy] = '\0';
        *olen = copy;
    }
    return ret;
}
