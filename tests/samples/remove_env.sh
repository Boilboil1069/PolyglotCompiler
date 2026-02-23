#!/usr/bin/env bash
# ============================================================================
# PolyglotCompiler Sample Environment Cleanup Script (Linux / macOS)
#
# This script removes isolated environments created by setup_env.sh:
# - tests/samples/env/
# - per-sample env links or env_path.txt files
#
# Usage:
#   cd tests/samples
#   chmod +x remove_env.sh
#   ./remove_env.sh
#   ./remove_env.sh --force
#
# The script only operates inside tests/samples to avoid accidental deletion.
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_DIR="$SCRIPT_DIR/env"

FORCE=false
if [ "${1:-}" = "--force" ] || [ "${1:-}" = "-f" ]; then
	FORCE=true
fi

echo "============================================"
echo " PolyglotCompiler Sample Environment Cleanup"
echo "============================================"
echo ""

# ---------------------------------------------------------------------------
# 1. Safety guard
# ---------------------------------------------------------------------------
case "$ENV_DIR" in
	*/tests/samples/env) ;;
	*)
		echo "[ERR] Safety check failed. Refusing to operate on: $ENV_DIR"
		exit 1
		;;
esac

# ---------------------------------------------------------------------------
# 2. Confirmation
# ---------------------------------------------------------------------------
if [ "$FORCE" = false ]; then
	echo "This will remove:"
	echo "  - $ENV_DIR"
	echo "  - env symlink/directory (or env_path.txt) under each sample folder"
	echo ""
	read -r -p "Continue? [y/N] " confirm
	case "$confirm" in
		y|Y|yes|YES) ;;
		*)
			echo "[OK] Cancelled"
			exit 0
			;;
	esac
fi

# ---------------------------------------------------------------------------
# 3. Remove per-sample links/files
# ---------------------------------------------------------------------------
echo ""
echo "--- Cleaning sample directory links ---"

for dir in "$SCRIPT_DIR"/[0-9]*_*/; do
	if [ -d "$dir" ]; then
		sample_name=$(basename "$dir")
		link_target="$dir/env"
		env_path_file="$dir/env_path.txt"

		if [ -L "$link_target" ]; then
			rm -f "$link_target"
			echo "[OK] Removed symlink: $sample_name/env"
		elif [ -d "$link_target" ]; then
			rm -rf "$link_target"
			echo "[OK] Removed directory/junction: $sample_name/env"
		else
			echo "[OK] No env link in: $sample_name"
		fi

		if [ -f "$env_path_file" ]; then
			rm -f "$env_path_file"
			echo "[OK] Removed file: $sample_name/env_path.txt"
		fi
	fi
done

# ---------------------------------------------------------------------------
# 4. Remove env directory
# ---------------------------------------------------------------------------
echo ""
echo "--- Removing env directory ---"

if [ -d "$ENV_DIR" ]; then
	rm -rf "$ENV_DIR"
	echo "[OK] Removed: $ENV_DIR"
else
	echo "[OK] env directory does not exist: $ENV_DIR"
fi

# ---------------------------------------------------------------------------
# 5. Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo " Cleanup Complete!"
echo "============================================"
echo ""
echo "Removed environment root: $ENV_DIR"
echo "Removed sample links/files under: $SCRIPT_DIR/[0-9]*_*/"
