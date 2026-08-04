#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/tmp/pdfs"
OUTPUT_DIR="$PROJECT_DIR/output/pdf"

mkdir -p "$BUILD_DIR" "$OUTPUT_DIR"
cd "$SCRIPT_DIR"

for source in [0-9][0-9]_*.tex; do
  base="$(basename "$source" .tex)"
  latexmk -xelatex -interaction=nonstopmode -halt-on-error \
    -outdir="$BUILD_DIR" "$source"
  cp "$BUILD_DIR/$base.pdf" "$OUTPUT_DIR/$base.pdf"
done

echo "Đã tạo PDF trong: $OUTPUT_DIR"
