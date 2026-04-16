# Benchmark Inventory

All files follow the schema defined in `schema.md`.
All components are unit-size (1×1) unless noted. All nodes are movable unless marked fixed.

---

## Manifest

| File | Grid | Nodes | Nets | Avg Fanout | Type | Seed | Regenerate |
|------|------|-------|------|-----------|------|------|-----------|
| `tiny_10.txt` | 20×20 | 10 | 15 | 3.00 | random | 1001 | see below |
| `tiny_30.txt` | 40×40 | 30 | 45 | 3.00 | random | 1002 | see below |
| `tiny_100.txt` | 80×80 | 100 | 150 | 3.01 | random | 1003 | see below |
| `clustered_small.txt` | 15×15 | 20 | 30 | 2.53 | clustered (4 clusters, 80% intra) | 2001 | see below |
| `clustered_medium.txt` | 25×25 | 50 | 80 | 2.77 | clustered (6 clusters, 80% intra) | 2002 | see below |
| `hub_small.txt` | 15×15 | 20 | 30 | 3.70 | hub-heavy (3 hubs, 50% hub nets) | 3001 | see below |
| `hub_medium.txt` | 25×25 | 50 | 80 | 4.46 | hub-heavy (5 hubs, 50% hub nets) | 3002 | see below |

---

## Benchmark Descriptions

### Random (`tiny_*`)
Nodes and nets are fully random. Sizes vary (1×1 to 2×2). ~20% of nodes are fixed.
Good for basic correctness tests; SA improvement signal is weak because there is no structure to exploit.

### Clustered (`clustered_*`)
Nodes are divided into equally-sized clusters. 80% of nets connect nodes within the same cluster; 20% connect nodes from two different clusters. A good optimizer should pack each cluster together, which yields a large HPWL reduction versus random placement.

### Hub-heavy (`hub_*`)
A small number of high-fanout "hub" nodes (first N nodes in the file) participate in ~50% of all nets, each hub net having degree 4–8. The remaining nets are low-degree (2–3) among non-hub nodes. A good optimizer places hubs near the center of the grid.

---

## How to Regenerate

```bash
# Random benchmarks (via C++ generator through CLI)
./main   # then: generate bench/tiny_10.txt 20 20 10 15 1001
         #       generate bench/tiny_30.txt 40 40 30 45 1002
         #       generate bench/tiny_100.txt 80 80 100 150 1003

# Or use the shell script:
bash scripts/gen_bench.sh

# Structured benchmarks (Python)
python3 scripts/gen_structured_bench.py          # writes to bench/ by default
python3 scripts/gen_structured_bench.py --outdir bench/
```

## How to Validate

```bash
# Check all files in bench/
python3 scripts/check_dataset.py bench/

# Check a single file
python3 scripts/check_dataset.py bench/clustered_small.txt
```
