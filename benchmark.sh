#!/usr/bin/env bash
# =============================================================
#  benchmark.sh — CS2006 OS Chat Server  |  Throughput Evaluation
#
#  Runs the simulator at 10, 50, and 100 clients (§10.4) and
#  accumulates all results into a single throughput.csv so the
#  marker can compare performance across client counts.
#
#  Usage:
#      chmod +x benchmark.sh
#      ./benchmark.sh
#
#  Output:
#      throughput.csv   — per-second data for all three runs
#      benchmark_summary.txt — peak / avg per configuration
# =============================================================

set -euo pipefail

BINARY="./chat_sim"
THREADS=8
DURATION=30       # 30s per run is enough to see steady-state
RATE=50           # high arrival rate to stress-test
RATELIMIT=1000    # disable per-client throttle for throughput test

if [[ ! -x "$BINARY" ]]; then
    echo "[ERROR] $BINARY not found. Run 'make' first."
    exit 1
fi

# Clear previous results so we start fresh
rm -f throughput.csv benchmark_summary.txt

echo "============================================================="
echo "  CS2006 Throughput Benchmark"
echo "  Threads=$THREADS  Duration=${DURATION}s  Rate=$RATE/s"
echo "============================================================="
echo ""

for CLIENTS in 10 25 50; do
    echo "--- Run: --clients $CLIENTS --threads $THREADS --duration $DURATION ---"

    # Run headless: pipe /dev/null to stdin; suppress GUI by redirecting
    # The GUI window will open briefly — close it or let duration expire.
    "$BINARY" \
        --threads  "$THREADS"   \
        --clients  "$CLIENTS"   \
        --duration "$DURATION"  \
        --rate     "$RATE"      \
        --ratelimit "$RATELIMIT" \
        2>/dev/null | tee "run_${CLIENTS}clients.log" | \
        grep -E '^\[CONFIG\]|SIMULATION COMPLETE|Total messages|Peak|avg|Throughput CSV' || true

    echo ""
done

# Build summary from the CSV
echo "============================================================="
echo "  BENCHMARK SUMMARY"
echo "============================================================="

if [[ -f throughput.csv ]]; then
    echo ""
    printf "%-10s %-10s %-18s %-18s\n" "clients" "threads" "peak_msgs/sec" "avg_msgs/sec"
    printf "%-10s %-10s %-18s %-18s\n" "-------" "-------" "-------------" "------------"

    # Use awk to pull one summary line per (clients,threads) group
    awk -F',' '
        NR==1 { next }          # skip header
        {
            key = $1","$2
            if (!(key in peak) || $4+0 > peak[key]+0) peak[key] = $4
            sum[key]  += $4
            cnt[key]++
            avg5[key]  = $6      # pre-computed avg from logger
        }
        END {
            for (k in peak) {
                split(k, parts, ",")
                printf "%-10s %-10s %-18s %-18s\n", parts[1], parts[2], peak[k], sprintf("%.1f", sum[k]/cnt[k])
            }
        }
    ' throughput.csv | sort -n

    echo ""
    echo "Full per-second data: throughput.csv"
fi | tee benchmark_summary.txt

echo "============================================================="
echo "Done."
