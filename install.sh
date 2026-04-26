#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────
#  SEED CORE — install.sh
#  Works on: Kali Linux, Debian, Ubuntu, Parrot OS
#  Usage:    bash install.sh [--no-install-deps]
# ─────────────────────────────────────────────────────────
set -euo pipefail

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m';    RST='\033[0m'

log()  { echo -e "${CYN}[seed]${RST} $*"; }
ok()   { echo -e "${GRN}[  OK  ]${RST} $*"; }
warn() { echo -e "${YEL}[ WARN ]${RST} $*"; }
die()  { echo -e "${RED}[ FAIL ]${RST} $*" >&2; exit 1; }

INSTALL_DEPS=1
for arg in "$@"; do
    [[ "$arg" == "--no-install-deps" ]] && INSTALL_DEPS=0
done

echo ""
echo -e "${BLD}  ╔══════════════════════════════════════════╗${RST}"
echo -e "${BLD}  ║  SEED CORE v0.1.0 — installer           ║${RST}"
echo -e "${BLD}  ╚══════════════════════════════════════════╝${RST}"
echo ""

# ── OS detection ─────────────────────────────────────────
OS_ID=""
if [[ -f /etc/os-release ]]; then
    source /etc/os-release
    OS_ID="${ID:-unknown}"
fi
log "Detected OS: ${PRETTY_NAME:-$OS_ID}"

# ── Install dependencies ──────────────────────────────────
if [[ $INSTALL_DEPS -eq 1 ]]; then
    case "$OS_ID" in
        kali|debian|ubuntu|parrot|linuxmint)
            log "Installing build dependencies via apt..."
            if ! sudo -n true 2>/dev/null; then
                log "sudo required for apt install — you may be prompted for a password"
            fi
            sudo apt-get update -qq
            sudo apt-get install -y \
                build-essential \
                gcc \
                make \
                libssl-dev \
                zlib1g-dev \
                2>/dev/null || die "apt install failed. Run with --no-install-deps if they're already installed."
            ok "Build dependencies installed"
            ;;
        fedora|rhel|centos)
            log "Installing dependencies via dnf/yum..."
            sudo dnf install -y gcc make openssl-devel zlib-devel 2>/dev/null \
                || sudo yum install -y gcc make openssl-devel zlib-devel
            ok "Build dependencies installed"
            ;;
        arch|manjaro)
            log "Installing dependencies via pacman..."
            sudo pacman -S --noconfirm gcc make openssl zlib
            ok "Build dependencies installed"
            ;;
        *)
            warn "Unknown distro '${OS_ID}' — skipping auto-install"
            warn "Ensure gcc, make, libssl-dev, zlib1g-dev are installed"
            ;;
    esac
fi

# ── Check required tools ──────────────────────────────────
log "Verifying build tools..."
for tool in gcc make; do
    if ! command -v "$tool" &>/dev/null; then
        die "$tool not found — run: sudo apt install build-essential"
    fi
    ok "$tool found"
done

# ── Check libraries ───────────────────────────────────────
log "Checking libraries..."
if ! pkg-config --exists openssl 2>/dev/null && \
   ! [[ -f /usr/include/openssl/evp.h ]]; then
    die "OpenSSL dev headers not found — run: sudo apt install libssl-dev"
fi
ok "OpenSSL headers found"

if ! pkg-config --exists zlib 2>/dev/null && \
   ! [[ -f /usr/include/zlib.h ]]; then
    die "zlib headers not found — run: sudo apt install zlib1g-dev"
fi
ok "zlib headers found"

# ── Build ─────────────────────────────────────────────────
log "Building SEED CORE..."
make clean >/dev/null 2>&1 || true
if make -j"$(nproc)" 2>&1; then
    ok "Build successful"
else
    die "Build failed — check compiler output above"
fi

# ── Install binary ────────────────────────────────────────
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"

cp seed "$INSTALL_DIR/seed"
chmod 755 "$INSTALL_DIR/seed"
ok "Installed: $INSTALL_DIR/seed"

# ── Copy default genome ───────────────────────────────────
SEED_DIR="$HOME/.seedcore"
SNAP_DIR="$SEED_DIR/snapshots"
mkdir -p "$SNAP_DIR"

if [[ ! -f "$SNAP_DIR/genome.conf" ]]; then
    cp config/genome_default.conf "$SNAP_DIR/genome.conf"
    ok "Default genome installed: $SNAP_DIR/genome.conf"
else
    warn "Genome already exists at $SNAP_DIR/genome.conf — not overwriting"
fi

# ── PATH check ────────────────────────────────────────────
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    warn "$INSTALL_DIR is not in PATH"
    echo ""
    echo "  Add this to your ~/.bashrc or ~/.zshrc:"
    echo ""
    echo -e "    ${CYN}export PATH=\"\$HOME/.local/bin:\$PATH\"${RST}"
    echo ""
    echo "  Then reload: source ~/.bashrc"
fi

# ── Done ──────────────────────────────────────────────────
echo ""
echo -e "${BLD}  Installation complete.${RST}"
echo ""
echo "  Quick start:"
echo -e "    ${CYN}./seed -h${RST}               — show help"
echo -e "    ${CYN}./seed -v${RST}               — run verbose (all ticks printed)"
echo -e "    ${CYN}./seed -s 60${RST}            — run for 60 seconds then exit"
echo -e "    ${CYN}./seed -d -v${RST}            — dry-run, no disk writes"
echo -e "    ${CYN}./seed -p${RST}               — print current genome and exit"
echo -e "    ${CYN}./seed -g config/genome_default.conf${RST}  — load specific genome"
echo -e "    ${CYN}./seed -r${RST}               — reset all state (fresh genome)"
echo ""
echo "  Data stored in:  ${CYN}$SEED_DIR/${RST}"
echo "  Generated artifacts in: ${CYN}$SEED_DIR/artifacts/${RST}"
echo "  Genome checkpoints in:  ${CYN}$SEED_DIR/snapshots/${RST}"
echo ""
