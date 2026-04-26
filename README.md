# SEED CORE v0.1.0

**autonomous, self-growing digital organism for POSIX systems.**

SEED CORE is a C program that compiles to a native binary, runs on any POSIX OS, probes its real host environment, composes real programs (shell scripts, pipelines), tests them inside a sandboxed subprocess, and — when they pass — promotes them to executable artifacts on disk that persist across reboots.

```
  ╔══════════════════════════════════════════╗
  ║  SEED CORE v0.1.0 — organism online     ║
  ║  dir  : /home/user/.seedcore            ║
  ║  os   : Linux x86_64                   ║
  ║  prims: 20 atomic primitives registered ║
  ║  Press Ctrl+C to checkpoint and exit    ║
  ╚══════════════════════════════════════════╝
```

---

## Quick Start (Kali Linux)

```bash
# 1. Install build dependencies
sudo apt install build-essential libssl-dev zlib1g-dev

# 2. Build and install
bash install.sh

# 3. Run
./seed -v
```

---

## What It Does

Every **tick**, the seed:

1. **Probes** — reads `/proc/cpuinfo`, `/proc/meminfo`, `/proc/loadavg`, `uname`, disk stats, available tools (python3, curl, cron, docker…)
2. **Generates** — randomly composes a pipeline of 1–5 atomic primitives (e.g. `probe → compress → persist`)
3. **Scores** — the candidate pipeline is scored using a gene-weighted formula based on predicted utility and context
4. **Verifies** — the pipeline runs in a **forked subprocess** with real `setrlimit` CPU/memory bounds
5. **Promotes** — if score ≥ threshold AND the subprocess exits cleanly, the composite is written as a **real executable shell script** to `~/.seedcore/artifacts/`
6. **Learns** — gene values are nudged toward strategies that produce higher outcomes
7. **Checkpoints** — every 60 seconds, the genome is saved to `~/.seedcore/snapshots/genome.conf`

On the next boot, the seed resumes from the last checkpoint. All promoted artifacts remain on disk.

---

## Build Requirements

| Dependency | Package (Debian/Kali) | Purpose |
|---|---|---|
| gcc | `build-essential` | C11 compiler |
| make | `build-essential` | Build system |
| OpenSSL ≥ 1.1.1 | `libssl-dev` | Ed25519 signing, SHA-256 |
| zlib | `zlib1g-dev` | COMPRESS/DECOMPRESS primitives |

```bash
sudo apt install build-essential libssl-dev zlib1g-dev
make
```

---

## CLI Reference

```
./seed [OPTIONS]

  -v            Verbose — print every tick to stdout
  -d            Dry-run — don't write artifacts or install cron jobs
  -g <file>     Load genome from a .conf file
  -r            Reset — wipe state and start fresh
  -p            Print current genome and exit
  -s <seconds>  Run for N seconds then checkpoint and exit
  -h            Help
```

### Examples

```bash
# Run forever, verbose
./seed -v

# Test for 2 minutes, no disk writes
./seed -d -s 120 -v

# Load a specific genome
./seed -g config/genome_default.conf -v

# Inspect current genome (after some learning)
./seed -p

# Fresh start
./seed -r -v
```

---

## Data Directory Layout

```
~/.seedcore/
├── store/                  Content-addressed blob store (git-style)
│   └── ab/                 First 2 hex chars of SHA-256
│       └── cdef1234...     Rest of SHA-256 hash = file contents
├── keys/
│   ├── seed_priv.pem       Ed25519 private key (chmod 600)
│   └── seed_pub.pem        Ed25519 public key
├── snapshots/
│   ├── genome.conf         Latest genome checkpoint
│   ├── genome_<ts>.conf    Timestamped genome snapshots
│   ├── composites.idx      Index of all promoted composites
│   └── child_*.conf        Forked child genomes (from FORK primitive)
├── artifacts/
│   └── <sha256>.sh         Promoted composite scripts (chmod 755)
└── seed.log                Full execution log
```

---

## Genome Reference

The genome is 15 floating-point genes in `[0.0, 1.0]` that bias which actions the seed takes.

### Self-Sufficiency (g0–g4)

| Gene | Name | Effect |
|---|---|---|
| g0 | res-acq | Bonus score for PROBE and PERSIST actions |
| g1 | nrg-ret | Bonus score for CHECKPOINT; slows energy drain |
| g2 | task-au | Bonus score for SCHEDULE and COMPOSE |
| g3 | redund | Bonus score for SIGN and VERIFY |
| g4 | scarc | Amplifies BYPASS score under resource scarcity |

### Individuality (g5–g9)

| Gene | Name | Effect |
|---|---|---|
| g5 | beh-div | Divergence from population-mean action patterns |
| g6 | uniq-ac | Bonus for least-recently-used primitive types |
| g7 | niche | Bonus for ADVERTISE and MONITOR (niche signalling) |
| g8 | mut-d | Controls mutation rate in FORK offspring |
| g9 | net-var | Bonus for ADVERTISE; enables UDP multicast beacon |

### Freedom (g10–g14)

| Gene | Name | Effect |
|---|---|---|
| g10 | act-ent | Bonus proportional to Shannon entropy of action history |
| g11 | path-dv | Random exploration bonus; deviates from greedy policy |
| g12 | explor | Bonus for PROBE and ADVERTISE; expands pipeline depth |
| g13 | bypass | Bonus for BYPASS attempts (penalised on failure) |
| g14 | react-i | Bonus for self-initiated (proactive) actions; controls tick rate |

### Scoring Formula

```
Score(cand) = Utility(cand) + Σ gene_i × influence_i(cand, context)
```

Where `influence_i` are context-dependent bonuses. Two seeds with identical utilities but different genomes will make different choices — divergent evolution.

---

## Atomic Primitives

| ID | Type | What It Really Does |
|---|---|---|
| `probe` | PROBE | Reads `/proc/*`, `uname`, disk, tools |
| `map` | MAP | Transforms each line of input (toUpper) |
| `filter` | FILTER | Removes comment/blank lines |
| `reduce` | REDUCE | Counts lines, words, sums numbers |
| `compose` | COMPOSE | Meta: returns pipeline description |
| `persist` | PERSIST | SHA-256 content-addresses input to store |
| `restore` | RESTORE | Retrieves blob by hash from store |
| `checkpoint` | CHECKPOINT | Serializes genome to disk |
| `schedule` | SCHEDULE | Writes cron job for an artifact |
| `query` | QUERY | Lists store entries |
| `sign` | SIGN | Ed25519 signs input with seed's key |
| `verify` | VERIFY | Verifies an Ed25519 signature |
| `advertise` | ADVERTISE | Sends UDP multicast beacon (239.255.42.99:9999) |
| `monitor` | MONITOR | Reads CPU/mem/load from /proc |
| `fork` | FORK | Writes a mutated child genome to disk |
| `compress` | COMPRESS | zlib-compresses input |
| `decompress` | DECOMPRESS | zlib-decompresses compress output |
| `learn` | LEARN | Nudges genome weights based on outcome |
| `bypass` | BYPASS | Attempts to reclaim resources; risky |
| `score` | SCORE | Computes gene-weighted score for a type |

---

## Verifier (Sandbox)

Every composite pipeline runs in a **forked subprocess** with:

- `RLIMIT_CPU` — 6 seconds max CPU time
- `RLIMIT_AS` — 64 MB virtual memory
- `RLIMIT_FSIZE` — 4 MB max file write
- Pipe-based I/O (parent reads result, kills on timeout via `alarm(8)`)
- Exit code + pipe header determine success/failure

Failed composites are discarded. Passing ones are scored and may be promoted.

---

## Promoted Artifacts

Every promoted composite becomes a real executable `~/.seedcore/artifacts/<sha256>.sh`:

```bash
#!/usr/bin/env sh
# SEED CORE artifact — generated at tick 47
# Composite pipeline: probe → compress → persist
# ...

uname -a
cat /proc/meminfo | grep -E 'MemTotal|MemFree'
df -h $HOME

INPUT="${1:-/tmp/seed_data}"
[ -f "$INPUT" ] && gzip -k "$INPUT"

DATA="${1:-$(date)}"
HASH=$(echo -n "$DATA" | sha256sum | awk '{print $1}')
...
```

These scripts are standalone. They run after the seed process exits. They are a permanent record of what the seed learned.

---

## Security Notes

- The seed **does not connect to the internet** by default (only UDP multicast on the local subnet).
- The `bypass` primitive attempts `drop_caches` but fails gracefully without root.
- The `schedule` primitive writes cron jobs only if `crontab` exists and `-d` is not set.
- All artifacts are written under `~/.seedcore/` — the seed does not write outside the user's home directory.
- Ed25519 private key is stored at `~/.seedcore/keys/seed_priv.pem` with `chmod 600`.

---

## Architecture

```
seed.c          main(), signal handlers, CLI, util functions
canonical.c     SHA-256 via OpenSSL EVP
keymgmt.c       Ed25519 keygen, sign, verify via OpenSSL EVP_PKEY
genome.c        Gene representation, mutation, scoring formula, serialize
store.c         Content-addressed blob store (SHA-256 keyed files)
verifier.c      Fork+exec sandbox with rlimit, pipe I/O, timeout
growth.c        Main growth loop: probe → generate → score → verify → promote → learn
primitives/
  prim_registry.c   Static dispatch table, find-by-id, init
  prim_impls.c      All 20 primitive exec() implementations
```

---

## Extending the Seed

### Add a new primitive

1. Declare `int prim_myprim_exec(...)` in `prim_impls.c`
2. Implement it — real work, real syscalls, real output
3. Add entry to `PRIM_TABLE[]` in `prim_registry.c`
4. Recompile — it's immediately available for composition

### Add a new gene

1. Define `G15_MYFEATURE 15` in `seed.h`, bump `GENE_COUNT` to 16
2. Add influence term to `genome_score_cand()` in `genome.c`
3. Add to `genome_update_from_outcome()` for learning
4. Add name to `gene_names[]` array

---

## License

This project is released for research, education, and personal experimentation. Do not deploy on systems you do not own or have explicit permission to run autonomous agents on.
