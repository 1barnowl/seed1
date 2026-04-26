#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────
#  SEED CORE — watch.sh
#  Live terminal dashboard. Run alongside ./seed -v
#  Usage: bash scripts/watch.sh
# ─────────────────────────────────────────────────────────

SEED_DIR="${HOME}/.seedcore"
LOG="${SEED_DIR}/seed.log"
SNAP="${SEED_DIR}/snapshots/genome.conf"
ART="${SEED_DIR}/artifacts"
IDX="${SEED_DIR}/snapshots/composites.idx"

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[1;33m'
CYN='\033[0;36m'; BLU='\033[0;34m'; MAG='\033[0;35m'
BLD='\033[1m'; DIM='\033[2m'; RST='\033[0m'

bar() {
    local val="$1" max="${2:-1}" width="${3:-20}"
    local filled=$(echo "$val $max $width" | awk '{printf "%d", ($1/$2)*$3}')
    local empty=$((width - filled))
    printf "${CYN}"
    for ((i=0;i<filled;i++)); do printf "█"; done
    printf "${DIM}"
    for ((i=0;i<empty;i++)); do printf "░"; done
    printf "${RST}"
}

clear
while true; do
    tput cup 0 0

    echo -e "${BLD}${CYN}  ⬡ SEED CORE — Live Monitor${RST}  $(date '+%H:%M:%S')"
    echo -e "  ${DIM}$(printf '─%.0s' {1..50})${RST}"

    # ── Seed process ──────────────────────────────────────
    SEED_PID=$(pgrep -x seed 2>/dev/null | head -1)
    if [[ -n "$SEED_PID" ]]; then
        echo -e "  ${GRN}● RUNNING${RST}  PID: ${BLD}${SEED_PID}${RST}"
        CPU=$(ps -p "$SEED_PID" -o %cpu= 2>/dev/null | tr -d ' ')
        MEM=$(ps -p "$SEED_PID" -o rss=  2>/dev/null | awk '{printf "%.0fKB", $1}')
        echo -e "  CPU: ${BLD}${CPU:-0}%${RST}   RSS: ${BLD}${MEM:-0KB}${RST}"
    else
        echo -e "  ${RED}● STOPPED${RST}  (start with: ./seed -v)"
    fi

    echo ""

    # ── Genome ────────────────────────────────────────────
    echo -e "  ${BLD}GENOME${RST}"
    if [[ -f "$SNAP" ]]; then
        declare -A GENES
        while IFS=' = ' read -r key val; do
            [[ "$key" =~ ^# ]] && continue
            [[ -z "$key" || -z "$val" ]] && continue
            GENES["$key"]="$val"
        done < "$SNAP"

        genes_suf=(g0-res-acq g1-nrg-ret g2-task-au g3-redund g4-scarc)
        genes_ind=(g5-beh-div g6-uniq-ac g7-niche g8-mut-d g9-net-var)
        genes_fre=(g10-act-ent g11-path-dv g12-explor g13-bypass g14-react-i)

        echo -e "  ${CYN}[SELF-SUFFICIENCY]${RST}"
        for g in "${genes_suf[@]}"; do
            v="${GENES[$g]:-0.5}"
            printf "    %-14s  " "$g"
            bar "$v" 1 16
            printf "  ${BLD}%s${RST}\n" "$v"
        done

        echo -e "  ${GRN}[INDIVIDUALITY]${RST}"
        for g in "${genes_ind[@]}"; do
            v="${GENES[$g]:-0.5}"
            printf "    %-14s  " "$g"
            bar "$v" 1 16
            printf "  ${BLD}%s${RST}\n" "$v"
        done

        echo -e "  ${MAG}[FREEDOM]${RST}"
        for g in "${genes_fre[@]}"; do
            v="${GENES[$g]:-0.5}"
            printf "    %-14s  " "$g"
            bar "$v" 1 16
            printf "  ${BLD}%s${RST}\n" "$v"
        done
    else
        echo -e "  ${DIM}  No genome checkpoint yet${RST}"
    fi

    echo ""

    # ── Composites ────────────────────────────────────────
    N_ART=$(ls "${ART}"/*.sh 2>/dev/null | wc -l)
    N_COMP=$(wc -l < "${IDX}" 2>/dev/null || echo 0)
    echo -e "  ${BLD}ARTIFACTS${RST}  ${GRN}${N_ART}${RST} scripts  |  ${BLD}COMPOSITES${RST}  ${GRN}${N_COMP}${RST} promoted"

    if [[ -f "$IDX" ]]; then
        echo -e "  ${DIM}Latest composites:${RST}"
        tail -5 "$IDX" 2>/dev/null | while read -r line; do
            id=$(echo "$line" | awk '{print $1}')
            sc=$(echo "$line" | awk '{print $2}')
            echo -e "    ${CYN}${id}${RST}  score=${YEL}${sc}${RST}"
        done
    fi

    echo ""

    # ── Log tail ──────────────────────────────────────────
    echo -e "  ${BLD}LOG${RST} (last 8 lines)"
    if [[ -f "$LOG" ]]; then
        tail -8 "$LOG" 2>/dev/null | while IFS= read -r line; do
            if echo "$line" | grep -q "PROMOTED"; then
                echo -e "  ${GRN}${line}${RST}"
            elif echo "$line" | grep -q "SCARCITY\|bypass\|FAIL"; then
                echo -e "  ${RED}${line}${RST}"
            elif echo "$line" | grep -q "checkpoint"; then
                echo -e "  ${YEL}${line}${RST}"
            else
                echo -e "  ${DIM}${line}${RST}"
            fi
        done
    else
        echo -e "  ${DIM}  (no log yet — run ./seed -v first)${RST}"
    fi

    echo ""
    echo -e "  ${DIM}Refreshing every 2s — Ctrl+C to exit${RST}"

    sleep 2
done
