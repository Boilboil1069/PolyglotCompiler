#!/usr/bin/env bash
# ============================================================================
# PolyglotCompiler Sample Environment Setup Script (Linux / macOS)
#
# This script creates a Python virtual environment and a Rust toolchain
# inside tests/samples/env/ for running sample programs.
#
# Usage:
#   cd tests/samples
#   chmod +x setup_env.sh
#   ./setup_env.sh
#
# The env/ folder is git-ignored and not committed to the repository.
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_DIR="$SCRIPT_DIR/env"

echo "============================================"
echo " PolyglotCompiler Sample Environment Setup"
echo "============================================"
echo ""

# ---------------------------------------------------------------------------
# 1. Create env/ directory
# ---------------------------------------------------------------------------
if [ ! -d "$ENV_DIR" ]; then
    mkdir -p "$ENV_DIR"
    echo "[OK] Created env/ directory"
else
    echo "[OK] env/ directory already exists"
fi

# ---------------------------------------------------------------------------
# 2. Python virtual environment
# ---------------------------------------------------------------------------
PYTHON_ENV_DIR="$ENV_DIR/python"

echo ""
echo "--- Setting up Python environment ---"

if [ ! -d "$PYTHON_ENV_DIR" ]; then
    # Find Python executable
    PYTHON_CMD=""
    for cmd in python3 python; do
        if command -v "$cmd" &>/dev/null; then
            ver=$($cmd --version 2>&1)
            echo "[OK] Found Python: $ver"
            PYTHON_CMD="$cmd"
            break
        fi
    done

    if [ -z "$PYTHON_CMD" ]; then
        echo "[ERR] Python not found. Please install Python 3.8+."
        echo "      Ubuntu/Debian: sudo apt install python3 python3-venv"
        echo "      macOS: brew install python3"
    else
        echo "[..] Creating Python virtual environment..."
        $PYTHON_CMD -m venv "$PYTHON_ENV_DIR"
        echo "[OK] Python venv created at: $PYTHON_ENV_DIR"

        # Activate and install packages
        source "$PYTHON_ENV_DIR/bin/activate"

        echo "[..] Installing sample dependencies..."
        pip install --upgrade pip >/dev/null 2>&1
        pip install numpy torch typing-extensions 2>&1 || true

        echo "[OK] Python packages installed"
        deactivate
    fi
else
    echo "[OK] Python environment already exists at: $PYTHON_ENV_DIR"
fi

# ---------------------------------------------------------------------------
# 3. Rust toolchain
# ---------------------------------------------------------------------------
RUST_ENV_DIR="$ENV_DIR/rust"

echo ""
echo "--- Setting up Rust environment ---"

if [ ! -d "$RUST_ENV_DIR" ]; then
    mkdir -p "$RUST_ENV_DIR"

    if command -v rustup &>/dev/null; then
        rustup_ver=$(rustup --version 2>&1)
        echo "[OK] Found rustup: $rustup_ver"

        # Set isolated env vars
        export RUSTUP_HOME="$RUST_ENV_DIR/rustup"
        export CARGO_HOME="$RUST_ENV_DIR/cargo"

        echo "[..] Installing Rust stable toolchain (isolated)..."
        rustup install stable 2>&1
        rustup default stable 2>&1

        echo "[OK] Rust toolchain installed at: $RUST_ENV_DIR"

        # Create activation helper
        cat > "$RUST_ENV_DIR/activate.sh" << 'ACTIVATE_EOF'
#!/usr/bin/env bash
# Activate isolated Rust environment
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RUSTUP_HOME="$SCRIPT_DIR/rustup"
export CARGO_HOME="$SCRIPT_DIR/cargo"
export PATH="$SCRIPT_DIR/cargo/bin:$PATH"
echo "[Rust] Environment activated"
ACTIVATE_EOF
        chmod +x "$RUST_ENV_DIR/activate.sh"
        echo "[OK] Rust activation script: $RUST_ENV_DIR/activate.sh"

        # Reset env vars
        unset RUSTUP_HOME
        unset CARGO_HOME
    else
        echo "[WARN] rustup not found. Please install Rust:"
        echo "       curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
        echo "       After installing, re-run this script."

        echo "Rust environment not set up." > "$RUST_ENV_DIR/README.txt"
        echo "Install rustup and re-run setup_env.sh" >> "$RUST_ENV_DIR/README.txt"
    fi
else
    echo "[OK] Rust environment already exists at: $RUST_ENV_DIR"
fi

# ---------------------------------------------------------------------------
# 4. Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo " Setup Complete!"
echo "============================================"
echo ""
echo "Python venv:    $PYTHON_ENV_DIR"
echo "Rust toolchain: $RUST_ENV_DIR"
echo ""
echo "To activate Python: source $PYTHON_ENV_DIR/bin/activate"
echo "To activate Rust:   source $RUST_ENV_DIR/activate.sh"
echo ""
echo "Note: The env/ folder is git-ignored."
