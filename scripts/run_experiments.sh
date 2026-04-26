#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────
#  SEED CORE — run_experiments.sh
#  Predefined genome profiles for experimentation.
#  Each runs for 120 seconds and reports what was promoted.
#
#  Usage: bash scripts/run_experiments.sh [profile]
#  Profiles: explorer | survivor | conservative | anarchist | all
# ─────────────────────────────────────────────────────────
set -euo pipefail

SEED="./seed"
if [[ ! -x "$SEED" ]]; then
    SEED="$HOME/.local/bin/seed"
fi
if [[ ! -x "$SEED" ]]; then
    echo "Error: seed binary not found. Run: make && bash install.sh" >&2
    exit 1
fi

PROFILES_DIR="$(mktemp -d)"
trap "rm -rf $PROFILES_DIR" EXIT

# ── Write a genome to a temp file ────────────────────────
write_genome() {
    local name="$1"; shift
    local path="${PROFILES_DIR}/${name}.conf"
    cat > "$path" <<EOF
# SEED CORE — ${name} genome profile
EOF
    while [[ $# -ge 2 ]]; do
        echo "$1 = $2" >> "$path"
        shift 2
    done
    echo "$path"
}

# ── Run a profile ─────────────────────────────────────────
run_profile() {
    local label="$1"
    local genome="$2"
    local duration="${3:-120}"

    echo ""
    echo "══════════════════════════════════════════════════"
    echo "  Profile: ${label}"
    echo "  Duration: ${duration}s"
    echo "══════════════════════════════════════════════════"
    echo ""

    "$SEED" -r -g "$genome" -s "$duration" -v
    echo ""

    local art_dir="$HOME/.seedcore/artifacts"
    local n_art; n_art=$(ls "${art_dir}"/*.sh 2>/dev/null | wc -l)
    echo "  → Generated ${n_art} artifact(s) in ${art_dir}"
    echo ""
}

PROFILE="${1:-all}"

# ── Explorer: high exploration, high freedom ───────────────
EXPLORER=$(write_genome "explorer" \
    g0-res-acq 0.65  g1-nrg-ret 0.45  g2-task-au 0.50  g3-redund 0.30  g4-scarc 0.40 \
    g5-beh-div 0.60  g6-uniq-ac 0.65  g7-niche 0.35    g8-mut-d 0.70   g9-net-var 0.75 \
    g10-act-ent 0.85 g11-path-dv 0.80 g12-explor 0.90  g13-bypass 0.45 g14-react-i 0.80)

# ── Survivor: high self-sufficiency, conservative ─────────
SURVIVOR=$(write_genome "survivor" \
    g0-res-acq 0.90  g1-nrg-ret 0.85  g2-task-au 0.75  g3-redund 0.80  g4-scarc 0.90 \
    g5-beh-div 0.20  g6-uniq-ac 0.15  g7-niche 0.70    g8-mut-d 0.15   g9-net-var 0.10 \
    g10-act-ent 0.20 g11-path-dv 0.15 g12-explor 0.25  g13-bypass 0.20 g14-react-i 0.60)

# ── Conservative: low mutation, high redundancy ────────────
CONSERVATIVE=$(write_genome "conservative" \
    g0-res-acq 0.55  g1-nrg-ret 0.80  g2-task-au 0.70  g3-redund 0.90  g4-scarc 0.65 \
    g5-beh-div 0.15  g6-uniq-ac 0.10  g7-niche 0.80    g8-mut-d 0.05   g9-net-var 0.05 \
    g10-act-ent 0.10 g11-path-dv 0.10 g12-explor 0.20  g13-bypass 0.05 g14-react-i 0.70)

# ── Anarchist: maximum bypass, entropy, deviation ─────────
ANARCHIST=$(write_genome "anarchist" \
    g0-res-acq 0.50  g1-nrg-ret 0.30  g2-task-au 0.40  g3-redund 0.15  g4-scarc 0.80 \
    g5-beh-div 0.90  g6-uniq-ac 0.85  g7-niche 0.20    g8-mut-d 0.90   g9-net-var 0.80 \
    g10-act-ent 0.95 g11-path-dv 0.95 g12-explor 0.85  g13-bypass 0.95 g14-react-i 0.90)

case "$PROFILE" in
    explorer)     run_profile "EXPLORER — max exploration, high freedom" "$EXPLORER" 120 ;;
    survivor)     run_profile "SURVIVOR — max self-sufficiency, conservative" "$SURVIVOR" 120 ;;
    conservative) run_profile "CONSERVATIVE — low mutation, high redundancy" "$CONSERVATIVE" 120 ;;
    anarchist)    run_profile "ANARCHIST — maximum bypass, entropy, deviation" "$ANARCHIST" 120 ;;
    all)
        run_profile "EXPLORER"     "$EXPLORER"     60
        run_profile "SURVIVOR"     "$SURVIVOR"     60
        run_profile "CONSERVATIVE" "$CONSERVATIVE" 60
        run_profile "ANARCHIST"    "$ANARCHIST"    60
        ;;
    *)
        echo "Unknown profile: $PROFILE"
        echo "Usage: $0 [explorer|survivor|conservative|anarchist|all]"
        exit 1
        ;;
esac

echo "══════════════════════════════════════════════════"
echo "  All done. Check ~/.seedcore/artifacts/ for"
echo "  generated programs and ~/.seedcore/snapshots/"
echo "  for evolved genome checkpoints."
echo "══════════════════════════════════════════════════"
