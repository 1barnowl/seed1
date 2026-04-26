#ifndef SEED_H
#define SEED_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>

/* ── Constants ──────────────────────────────────────────── */
#define SEED_VERSION        "0.1.0"
#define GENE_COUNT          15
#define MAX_PRIM_NAME       64
#define MAX_PRIM_COUNT      128
#define MAX_COMPOSITE_STEPS 8
#define MAX_COMPOSITES      512
#define MAX_IO_BUF          65536   /* 64 KB max primitive I/O   */
#define SHA256_HEX_LEN      65      /* 64 hex chars + NUL        */
#define SIG_BYTES           64      /* Ed25519 signature length   */
#define PUBKEY_BYTES        32
#define MAX_PATH_LEN        512
#define CHECKPOINT_INTERVAL 60      /* seconds between checkpoints*/
#define SCORE_PROMOTE_THRESH 0.55   /* minimum score to promote   */
#define MAX_SCORE_HISTORY   512

/* ── Gene indices ───────────────────────────────────────── */
#define G0_RES_ACQ      0   /* Resource acquisition rate      */
#define G1_NRG_RET      1   /* Energy retention               */
#define G2_TASK_AUTO    2   /* Task automation preference     */
#define G3_REDUND       3   /* Redundancy drive               */
#define G4_SCARC        4   /* Scarcity survival              */
#define G5_BEH_DIV      5   /* Behavioural divergence         */
#define G6_UNIQ_ACT     6   /* Unique action preference       */
#define G7_NICHE        7   /* Niche specialisation           */
#define G8_MUT_D        8   /* Mutation distinctiveness       */
#define G9_NET_VAR      9   /* Network variance               */
#define G10_ACT_ENT    10   /* Action entropy preference      */
#define G11_PATH_DV    11   /* Path deviation tolerance       */
#define G12_EXPLOR     12   /* Exploration radius drive       */
#define G13_BYPASS     13   /* Constraint bypass impulse      */
#define G14_REACT_I    14   /* Reactivity independence level  */

/* ── Primitive types ────────────────────────────────────── */
typedef enum {
    PRIM_PROBE = 0,
    PRIM_MAP,
    PRIM_FILTER,
    PRIM_REDUCE,
    PRIM_COMPOSE,
    PRIM_PERSIST,
    PRIM_RESTORE,
    PRIM_CHECKPOINT,
    PRIM_SCHEDULE,
    PRIM_QUERY,
    PRIM_SIGN,
    PRIM_VERIFY,
    PRIM_ADVERTISE,
    PRIM_MONITOR,
    PRIM_FORK,
    PRIM_COMPRESS,
    PRIM_DECOMPRESS,
    PRIM_LEARN,
    PRIM_BYPASS,
    PRIM_SCORE,
    PRIM_TYPE_COUNT
} PrimType;

/* ── Structs ────────────────────────────────────────────── */
typedef struct {
    double  g[GENE_COUNT];
    uint8_t hash[32];          /* SHA-256 of raw gene values   */
} Genome;

struct SeedState;

/* Primitive function signature */
typedef int (*prim_fn)(
    const char    *input,
    size_t         input_len,
    char          *output,
    size_t         output_cap,
    size_t        *output_len,
    struct SeedState *state
);

typedef struct {
    char     id[MAX_PRIM_NAME];
    char     name[MAX_PRIM_NAME];
    PrimType type;
    prim_fn  exec;
    double   base_score;
    uint64_t exec_count;
    uint64_t success_count;
    double   avg_latency_ms;
    int      atomic;            /* 1 = built-in, 0 = composite  */
} Primitive;

typedef struct {
    char      id[MAX_PRIM_NAME];
    int       step_count;
    char      step_ids[MAX_COMPOSITE_STEPS][MAX_PRIM_NAME];
    double    score;
    char      artifact_path[MAX_PATH_LEN];
    time_t    created;
    uint64_t  exec_count;
    uint8_t   hash[32];
} Composite;

typedef struct {
    int    success;
    double score;
    double latency_ms;
    char   output[MAX_IO_BUF];
    size_t output_len;
    char   error_msg[256];
    long   peak_rss_kb;
} VerifierResult;

typedef struct {
    double action_scores[256];
    char   action_names[256][MAX_PRIM_NAME];
    int    action_count;
    double recent_scores[64];
    int    recent_head;
    int    recent_count;
    double entropy;
} ScoreHistory;

/* Host environment snapshot */
typedef struct {
    char     os_name[64];
    char     os_release[64];
    char     arch[32];
    int      cpu_cores;
    uint64_t mem_total_kb;
    uint64_t mem_free_kb;
    uint64_t disk_free_kb;
    double   load_avg;
    int      has_python3;
    int      has_node;
    int      has_curl;
    int      has_wget;
    int      has_cron;
    int      has_systemd;
    int      has_docker;
    int      has_git;
    char     hostname[256];
    char     username[64];
    char     ipv4[64];
    time_t   probed_at;
} HostEnv;

/* Main seed state */
typedef struct SeedState {
    Genome    genome;
    double    energy;               /* [0.0, 100.0]               */
    int       generation;
    uint64_t  ticks;
    double    cumulative_score;

    /* Primitive registry */
    Primitive prims[MAX_PRIM_COUNT];
    int       prim_count;

    /* Composite registry */
    Composite composites[MAX_COMPOSITES];
    int       composite_count;

    /* Paths */
    char base_dir[MAX_PATH_LEN];
    char store_dir[MAX_PATH_LEN];
    char keys_dir[MAX_PATH_LEN];
    char snapshots_dir[MAX_PATH_LEN];
    char artifacts_dir[MAX_PATH_LEN];
    char log_path[MAX_PATH_LEN];

    /* Host environment */
    HostEnv   host;

    /* Crypto */
    void     *signing_key;          /* EVP_PKEY* (opaque)         */
    uint8_t   pubkey_bytes[PUBKEY_BYTES];

    /* Scoring history */
    ScoreHistory score_hist;

    /* Action type history for entropy calculation */
    int  action_type_hist[PRIM_TYPE_COUNT];
    int  action_total;

    /* Runtime flags */
    volatile int running;
    int          verbose;
    int          dry_run;
    FILE        *logfp;
} SeedState;

/* ── canonical.c ────────────────────────────────────────── */
void sha256_bytes(const uint8_t *data, size_t len, uint8_t out[32]);
void sha256_hex  (const uint8_t *data, size_t len, char   out[SHA256_HEX_LEN]);
void sha256_file (const char *path, char out[SHA256_HEX_LEN]);

/* ── keymgmt.c ──────────────────────────────────────────── */
int  keymgmt_init   (SeedState *s);
int  keymgmt_sign   (SeedState *s, const uint8_t *msg, size_t len, uint8_t sig[SIG_BYTES]);
int  keymgmt_verify (SeedState *s, const uint8_t *msg, size_t len, const uint8_t sig[SIG_BYTES]);

/* ── genome.c ───────────────────────────────────────────── */
void   genome_init_random  (Genome *g);
void   genome_init_default (Genome *g);
void   genome_mutate       (const Genome *src, Genome *dst, double rate);
void   genome_hash         (Genome *g);
double genome_score_cand   (const Genome *g, PrimType cand_type,
                             double utility, int triggered_externally,
                             const ScoreHistory *hist, const HostEnv *host);
void   genome_serialize    (const Genome *g, char *buf, size_t cap);
int    genome_deserialize  (Genome *g, const char *buf);
void   genome_print        (const Genome *g);

/* ── store.c ────────────────────────────────────────────── */
int  store_init (const char *store_dir);
int  store_put  (const char *store_dir, const uint8_t *data, size_t len,
                 char hex_out[SHA256_HEX_LEN]);
int  store_get  (const char *store_dir, const char *hex,
                 uint8_t *buf, size_t cap, size_t *out_len);
int  store_has  (const char *store_dir, const char *hex);

/* ── verifier.c ─────────────────────────────────────────── */
int  verifier_run_primitive  (Primitive *p, const char *input, size_t ilen,
                               SeedState *s, VerifierResult *r);
int  verifier_run_composite  (SeedState *s, const char **step_ids, int step_count,
                               const char *input, size_t ilen, VerifierResult *r);

/* ── growth.c ───────────────────────────────────────────── */
int  growth_init (SeedState *s);
void growth_loop (SeedState *s);
int  growth_promote_composite (SeedState *s, const char **step_ids, int n,
                                double score, const VerifierResult *r);

/* ── primitives/prim_registry.c ─────────────────────────── */
int       prim_registry_init (SeedState *s);
Primitive *prim_find         (SeedState *s, const char *id);
int       prim_exec          (SeedState *s, const char *id,
                               const char *input, size_t ilen,
                               char *output, size_t ocap, size_t *olen);
void      prim_pick_random   (SeedState *s, int count,
                               const char *ids_out[][MAX_PRIM_NAME]);

/* ── util ───────────────────────────────────────────────── */
double  now_ms        (void);
void    seed_log      (SeedState *s, const char *fmt, ...);
double  drand_range   (double lo, double hi);
int     mkdir_p       (const char *path, mode_t mode);
ssize_t read_file     (const char *path, char *buf, size_t cap);
int     write_file    (const char *path, const char *buf, size_t len);
int     file_exists   (const char *path);
int     cmd_exists    (const char *cmd);
int     run_cmd       (const char *cmd, char *out, size_t cap, int timeout_s);
void    hex_encode    (const uint8_t *data, size_t len, char *out);
void    hex_decode    (const char *hex, uint8_t *out, size_t len);

#endif /* SEED_H */
