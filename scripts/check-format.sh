#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/check-format.sh [--fix]

Check the formatting of the project's C, header, and Python files.
Use --fix to format files in place.
EOF
}

fix=false
case "${1:-}" in
    "") ;;
    --fix|-f) fix=true ;;
    --check) ;;
    --help|-h)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

if (( $# > 1 )); then
    usage >&2
    exit 2
fi

repo_root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$repo_root"

if command -v clang-format-18 >/dev/null 2>&1; then
    clang_format=clang-format-18
elif command -v clang-format >/dev/null 2>&1; then
    clang_format=clang-format
elif command -v xcrun >/dev/null 2>&1 && xcrun --find clang-format >/dev/null 2>&1; then
    clang_format=$(xcrun --find clang-format)
else
    echo "error: clang-format-18 or clang-format is required" >&2
    exit 1
fi

if command -v black >/dev/null 2>&1; then
    black_command=(black)
elif python3 -m black --version >/dev/null 2>&1; then
    black_command=(python3 -m black)
else
    echo "error: Black is required (install with: python3 -m pip install black)" >&2
    exit 1
fi

c_files=()
while IFS= read -r -d '' file; do
    case "$file" in
        tests/expected/*.mxsd.c|tests/expected/*.mxsd.h) continue ;;
    esac
    c_files+=("$file")
done < <(git ls-files -z -- '*.c' '*.h' 'tests/w3c/runner.c.in')

python_files=(mxsdc)
while IFS= read -r -d '' file; do
    python_files+=("$file")
done < <(git ls-files -z -- '*.py')

if $fix; then
    "$clang_format" -i "${c_files[@]}"
    "${black_command[@]}" --line-length 100 "${python_files[@]}"
    echo "Formatting complete."
else
    "$clang_format" --dry-run --Werror "${c_files[@]}"
    "${black_command[@]}" --check --line-length 100 "${python_files[@]}"
    echo "Formatting checks passed."
fi
