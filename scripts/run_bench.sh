#!/usr/bin/env bash
# Build in Release and run the calm and busy batches. Week 1: single thread only.
# Later weeks add loops over --threads 1 2 4 8 and --schedule static|dynamic|priority.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build -j

if [ ! -f data/synth/papers.tsv ]; then
  python3 scripts/gen_synthetic.py --papers 20000 --refs 2000 --out data/synth
fi

mkdir -p results
for batch in calm busy; do
  echo "================ $batch batch ================"
  ./build/citeverify --db data/synth/papers.tsv \
                     --refs "data/synth/refs_${batch}.txt" \
                     --out "results/report_${batch}.tsv" 2>&1 | tee "results/log_${batch}.txt"
done
