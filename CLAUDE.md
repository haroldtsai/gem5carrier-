# CLAUDE.md — Custom TAGE Branch Predictor (Hardware Simulation)

## Project Goal
Simulate a real hardware TAGE-based branch predictor as accurately as possible.
The implementation mirrors actual hardware behavior; area/timing constraints are not a concern.
All algorithms are provided by the user and must be implemented faithfully.

---

## Predictor Overview

| Component | Status | Notes |
|---|---|---|
| GHR | [ ] TBD | |
| Tag & Index Hashing | [ ] TBD | |
| Prediction Algorithm | [ ] TBD | |
| Training / Recovery | [ ] TBD | |
| Flush Policy | [ ] TBD | |

---

## 1. GHR (Global History Register)

<!-- To be filled: length, update timing (speculative/committed), structure (shift reg / circular), per-thread or global -->

**Length**: TBD
**Update timing**: TBD (speculative / at commit)
**Structure**: TBD
**Scope**: TBD (global / per-thread)
**Special behavior**: TBD

---

## 2. Tag & Index Hashing

<!-- To be filled: PC bits used, history folding method, geometric lengths, path history -->

**History lengths (geometric series)**: TBD
**Index hash function**: TBD
**Tag hash function**: TBD
**Folding method**: TBD (CSR / XOR / other)
**PC bits used**: TBD

---

## 3. Prediction Algorithm

<!-- To be filled: provider selection, alt prediction, confidence, fallback to base predictor -->

**Provider selection**: TBD (longest matching tag)
**Alt prediction**: TBD
**Base predictor**: TBD (bimodal / other)
**Confidence / threshold**: TBD
**Special cases**: TBD

---

## 4. Training & Recovery Algorithm

<!-- To be filled: when to update counters, useful bit policy, allocation policy, recovery mechanism -->

**Update trigger**: TBD (speculative / at commit / on mispredict)
**Counter update policy**: TBD
**Useful bit (u-bit) policy**: TBD
**Entry allocation policy**: TBD (on mispredict)
**Recovery mechanism**: TBD (checkpoint / re-execute / GHR restore)

---

## 5. Flush Policy

<!-- To be filled: when to flush, what to flush, partial vs full flush -->

**Flush trigger**: TBD (mispredict / context switch / other)
**Flush scope**: TBD (full / partial / tagged entries only)
**GHR restore method**: TBD
**BTB / RAS behavior on flush**: TBD

---

## gem5 Implementation Plan

### Files to Create / Modify
| File | Action | Purpose |
|---|---|---|
| `src/cpu/pred/my_tage.hh` | Modify | Class declaration |
| `src/cpu/pred/my_tage.cc` | Modify | Implementation |
| `src/cpu/pred/BranchPredictor.py` | Modify if needed | Python params |
| `src/cpu/pred/SConscript` | Modify if needed | Build registration |

### Key Base Classes
- `TAGEBase` — core TAGE logic (`src/cpu/pred/tage_base.hh/cc`)
- `TAGE` — wrapper (`src/cpu/pred/tage.hh/cc`)
- Override targets: `gindex()`, `gtag()`, `bindex()`, `predict()`, `update()`

### Run Command
```bash
./build/RISCV/gem5.opt configs/riscv_o3_run.py \
    --cmd <benchmark> \
    --bp-type=MyTAGE
```

---

## Implementation Rules

1. **Faithfulness first**: implement the algorithm exactly as specified, no simplifications.
2. **No area optimization**: do not reduce table sizes or history lengths to save memory.
3. **Speculative vs committed**: always match the hardware's update timing exactly.
4. **One component at a time**: implement and verify each of the 5 components before moving to next.
5. **Regression check**: after each change, run hello benchmark to confirm no segfault/crash.

---

## Testing & Validation

### Benchmarks
- [ ] Smoke test: `tests/test-progs/hello/bin/riscv/linux/hello`
- [ ] TBD: actual target workload

### Metrics to Track
- Branch MPKI (mispredictions per kilo-instruction)
- Overall IPC
- Per-component accuracy (if instrumentable)

### Baseline Comparison
- gem5 stock TAGE (`m5out_tage/`)
- MyTAGE current (`m5out_mytage/`)

---

## Known Issues & Gotchas

- `MyTAGEParams` shadow field bug: always add `tage = p.tage;` in constructor (already fixed).
- `--bp-type` in `se.py` does not support ConditionalPredictor subclasses; use `configs/riscv_o3_run.py`.
- Build after any `.hh/.cc` change: `scons build/RISCV/gem5.opt -j$(sysctl -n hw.ncpu)`.
