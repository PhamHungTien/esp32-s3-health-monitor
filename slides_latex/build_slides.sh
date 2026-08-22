#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/tmp/slides"
OUTPUT_DIR="$PROJECT_DIR/output/pdf"

mkdir -p "$BUILD_DIR" "$OUTPUT_DIR"
cd "$SCRIPT_DIR"
latexmk -xelatex -interaction=nonstopmode -halt-on-error \
  -outdir="$BUILD_DIR" bao_cao_do_an_esp32s3.tex
cp "$BUILD_DIR/bao_cao_do_an_esp32s3.pdf" \
  "$OUTPUT_DIR/05_slide_thuyet_trinh_do_an_esp32s3.pdf"
echo "Đã tạo: $OUTPUT_DIR/05_slide_thuyet_trinh_do_an_esp32s3.pdf"
