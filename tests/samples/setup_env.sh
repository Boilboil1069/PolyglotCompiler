#!/usr/bin/env bash
# ============================================================================
# PolyglotCompiler Sample Environment Setup Script (Linux / macOS)
#
# This script creates isolated environments for Python, Rust, Java, and .NET
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

        # Use the venv's own python for pip operations
        VENV_PYTHON="$PYTHON_ENV_DIR/bin/python"

        echo "[..] Installing sample dependencies..."
        "$VENV_PYTHON" -m pip install --upgrade pip >/dev/null 2>&1 || true
        "$VENV_PYTHON" -m pip install numpy typing-extensions 2>&1 || true

        echo "[OK] Python packages installed"
    fi
else
    echo "[OK] Python environment already exists at: $PYTHON_ENV_DIR"
fi

# ---------------------------------------------------------------------------
# 3. Rust toolchain (auto-install rustup if not found)
# ---------------------------------------------------------------------------
RUST_ENV_DIR="$ENV_DIR/rust"

echo ""
echo "--- Setting up Rust environment ---"

if [ ! -d "$RUST_ENV_DIR" ]; then
    mkdir -p "$RUST_ENV_DIR"

    RUSTUP_AVAILABLE=false
    if command -v rustup &>/dev/null; then
        rustup_ver=$(rustup --version 2>&1)
        echo "[OK] Found rustup: $rustup_ver"
        RUSTUP_AVAILABLE=true
    fi

    if [ "$RUSTUP_AVAILABLE" = false ]; then
        echo "[..] rustup not found. Downloading and installing..."
        export RUSTUP_HOME="$RUST_ENV_DIR/rustup"
        export CARGO_HOME="$RUST_ENV_DIR/cargo"
        if curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --default-toolchain stable 2>&1; then
            RUSTUP_AVAILABLE=true
            echo "[OK] rustup installed successfully"
            export PATH="$CARGO_HOME/bin:$PATH"
        else
            echo "[ERR] rustup installation failed"
        fi
        unset RUSTUP_HOME
        unset CARGO_HOME
    fi

    if [ "$RUSTUP_AVAILABLE" = true ]; then
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
    fi
else
    echo "[OK] Rust environment already exists at: $RUST_ENV_DIR"
fi

# ---------------------------------------------------------------------------
# 4. Java environment (auto-detect or download Adoptium JDK)
# ---------------------------------------------------------------------------
JAVA_ENV_DIR="$ENV_DIR/java"

echo ""
echo "--- Setting up Java environment ---"

if [ ! -d "$JAVA_ENV_DIR" ]; then
    mkdir -p "$JAVA_ENV_DIR"

    JAVA_AVAILABLE=false
    JAVA_HOME_PATH=""

    if command -v java &>/dev/null; then
        java_ver=$(java -version 2>&1 | head -1)
        echo "[OK] Found system Java: $java_ver"
        JAVA_AVAILABLE=true
        # Try to find JAVA_HOME
        if [ -n "$JAVA_HOME" ] && [ -d "$JAVA_HOME" ]; then
            JAVA_HOME_PATH="$JAVA_HOME"
        else
            JAVA_HOME_PATH=$(dirname $(dirname $(readlink -f $(which java) 2>/dev/null || which java))) 2>/dev/null || true
        fi
    fi

    if [ "$JAVA_AVAILABLE" = false ]; then
        echo "[..] Java not found. Downloading Adoptium JDK 21 (LTS)..."
        # Detect OS and architecture
        OS_NAME="linux"
        ARCH="x64"
        EXT="tar.gz"
        if [ "$(uname)" = "Darwin" ]; then
            OS_NAME="mac"
            if [ "$(uname -m)" = "arm64" ]; then
                ARCH="aarch64"
            fi
        fi

        JDK_URL="https://api.adoptium.net/v3/binary/latest/21/ga/${OS_NAME}/${ARCH}/jdk/hotspot/normal/eclipse?project=jdk"
        JDK_ARCHIVE="$ENV_DIR/jdk.$EXT"

        if curl -L -o "$JDK_ARCHIVE" "$JDK_URL" 2>/dev/null; then
            echo "[OK] Downloaded Adoptium JDK 21"
            echo "[..] Extracting JDK..."
            tar xzf "$JDK_ARCHIVE" -C "$JAVA_ENV_DIR" 2>/dev/null || true
            rm -f "$JDK_ARCHIVE"

            JDK_SUBDIR=$(ls -d "$JAVA_ENV_DIR"/jdk-* 2>/dev/null | head -1)
            if [ -n "$JDK_SUBDIR" ] && [ -d "$JDK_SUBDIR" ]; then
                JAVA_HOME_PATH="$JDK_SUBDIR"
                JAVA_AVAILABLE=true
                echo "[OK] JDK extracted to: $JAVA_HOME_PATH"
            else
                echo "[ERR] Could not find extracted JDK directory"
            fi
        else
            echo "[ERR] Failed to download JDK"
            echo "      Manual install: https://adoptium.net/"
        fi
    fi

    if [ "$JAVA_AVAILABLE" = true ]; then
        # Create activation helper
        cat > "$JAVA_ENV_DIR/activate.sh" << ACTIVATE_EOF
#!/usr/bin/env bash
# Activate isolated Java environment
JDK_DIR=\$(ls -d "$JAVA_ENV_DIR"/jdk-* 2>/dev/null | head -1)
if [ -n "\$JDK_DIR" ] && [ -d "\$JDK_DIR" ]; then
    export JAVA_HOME="\$JDK_DIR"
    export PATH="\$JDK_DIR/bin:\$PATH"
    echo "[Java] Environment activated: \$JDK_DIR"
elif [ -n "$JAVA_HOME_PATH" ] && [ -d "$JAVA_HOME_PATH" ]; then
    export JAVA_HOME="$JAVA_HOME_PATH"
    export PATH="$JAVA_HOME_PATH/bin:\$PATH"
    echo "[Java] Environment activated (system): $JAVA_HOME_PATH"
else
    echo "[Java] Warning: No JDK directory found"
fi
ACTIVATE_EOF
        chmod +x "$JAVA_ENV_DIR/activate.sh"
        echo "[OK] Java activation script: $JAVA_ENV_DIR/activate.sh"
    fi
else
    echo "[OK] Java environment already exists at: $JAVA_ENV_DIR"
fi

# ---------------------------------------------------------------------------
# 5. .NET SDK environment (auto-detect or install via dotnet-install.sh)
# ---------------------------------------------------------------------------
DOTNET_ENV_DIR="$ENV_DIR/dotnet"

echo ""
echo "--- Setting up .NET environment ---"

if [ ! -d "$DOTNET_ENV_DIR" ]; then
    mkdir -p "$DOTNET_ENV_DIR"

    DOTNET_AVAILABLE=false
    if command -v dotnet &>/dev/null; then
        dotnet_ver=$(dotnet --version 2>&1)
        echo "[OK] Found system .NET SDK: $dotnet_ver"
        DOTNET_AVAILABLE=true
    fi

    if [ "$DOTNET_AVAILABLE" = false ]; then
        echo "[..] .NET SDK not found. Downloading .NET 9 SDK..."
        DOTNET_INSTALL_SCRIPT="$ENV_DIR/dotnet-install.sh"
        if curl -sSL https://dot.net/v1/dotnet-install.sh -o "$DOTNET_INSTALL_SCRIPT" 2>/dev/null; then
            chmod +x "$DOTNET_INSTALL_SCRIPT"
            echo "[OK] Downloaded dotnet-install.sh"

            echo "[..] Installing .NET 9 SDK to isolated directory..."
            "$DOTNET_INSTALL_SCRIPT" --channel 9.0 --install-dir "$DOTNET_ENV_DIR" --no-path 2>&1 || true

            if [ -f "$DOTNET_ENV_DIR/dotnet" ]; then
                DOTNET_AVAILABLE=true
                echo "[OK] .NET SDK installed at: $DOTNET_ENV_DIR"
            else
                echo "[ERR] .NET SDK installation failed"
            fi

            rm -f "$DOTNET_INSTALL_SCRIPT"
        else
            echo "[ERR] Failed to download dotnet-install.sh"
            echo "      Manual install: https://dotnet.microsoft.com/download"
        fi
    else
        # Store system dotnet path reference
        DOTNET_EXE=$(which dotnet 2>/dev/null || true)
        if [ -n "$DOTNET_EXE" ]; then
            echo "$DOTNET_EXE" > "$DOTNET_ENV_DIR/system_dotnet_path.txt"
        fi
    fi

    if [ "$DOTNET_AVAILABLE" = true ]; then
        # Create activation helper
        cat > "$DOTNET_ENV_DIR/activate.sh" << ACTIVATE_EOF
#!/usr/bin/env bash
# Activate isolated .NET environment
if [ -f "$DOTNET_ENV_DIR/dotnet" ]; then
    export DOTNET_ROOT="$DOTNET_ENV_DIR"
    export PATH="$DOTNET_ENV_DIR:\$PATH"
    export DOTNET_CLI_TELEMETRY_OPTOUT=1
    echo "[.NET] Environment activated (isolated): $DOTNET_ENV_DIR"
else
    echo "[.NET] Using system .NET SDK"
fi
ACTIVATE_EOF
        chmod +x "$DOTNET_ENV_DIR/activate.sh"
        echo "[OK] .NET activation script: $DOTNET_ENV_DIR/activate.sh"
    fi
else
    echo "[OK] .NET environment already exists at: $DOTNET_ENV_DIR"
fi

# ---------------------------------------------------------------------------
# 6. Link environments to sample directories
# ---------------------------------------------------------------------------
echo ""
echo "--- Linking environments to sample directories ---"

for dir in "$SCRIPT_DIR"/[0-9]*_*/; do
    if [ -d "$dir" ]; then
        dirname=$(basename "$dir")
        link_target="$dir/env"
        if [ ! -e "$link_target" ]; then
            ln -s "$ENV_DIR" "$link_target" 2>/dev/null || true
            if [ -L "$link_target" ]; then
                echo "[OK] Linked env/ -> $dirname/env"
            else
                echo "$ENV_DIR" > "$dir/env_path.txt"
                echo "[OK] Created env_path.txt in $dirname/"
            fi
        else
            echo "[OK] $dirname/env already linked"
        fi
    fi
done

# ---------------------------------------------------------------------------
# 7. Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo " Setup Complete!"
echo "============================================"
echo ""
echo "Python venv:      $PYTHON_ENV_DIR"
echo "Rust toolchain:   $RUST_ENV_DIR"
echo "Java JDK:         $JAVA_ENV_DIR"
echo ".NET SDK:         $DOTNET_ENV_DIR"
echo ""
echo "To activate Python: source $PYTHON_ENV_DIR/bin/activate"
echo "To activate Rust:   source $RUST_ENV_DIR/activate.sh"
echo "To activate Java:   source $JAVA_ENV_DIR/activate.sh"
echo "To activate .NET:   source $DOTNET_ENV_DIR/activate.sh"
echo ""
echo "Note: The env/ folder is git-ignored."
