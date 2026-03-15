## Context

gem5 內建 TAGEBase/TAGE 提供可覆寫的 `gindex()`、`gtag()`、`F()` 函數，以及繼承鏈 `MyTAGE → TAGE → ConditionalPredictor`。現有的 `my_tage.hh/cc` 是一個 Demo 版本，使用 Fibonacci hash。本次要完整替換為使用者指定的硬體規格。

主要挑戰：
1. GHR 結構與 gem5 標準不同（only-taken，3 bits/entry）
2. Index hashing 不使用 folded history，而是直接 XOR 特定 GHR bits
3. Prediction/Training/Allocation policy 與 gem5 TAGE 原始邏輯有差異，需要完全覆寫

## Goals / Non-Goals

**Goals:**
- 完整覆寫 GHR 結構（only-taken，3 bits/entry，speculative update + checkpoint restore）
- 實作各 table 自訂的 9-bit GHR_hash XOR index hashing
- 實作 13-bit tag
- 覆寫 prediction policy（provider/alternator + use_alternate 5-bit）
- 覆寫 training policy（pred counter、use_alternate、alternator counter 更新）
- 覆寫 allocation policy（useful==0 victim，fallback useful--，無老化）
- 通過 hello smoke test（無 segfault）

**Non-Goals:**
- 多執行緒正確性
- 效能 benchmark
- Loop/SC 組件

## Decisions

### D1: 繼承策略 — 部分覆寫 vs 完全重寫

**決定**: 繼承 `TAGEBase` 並覆寫關鍵函數，不繼承 `TAGE` wrapper 的 predict/update 邏輯。

**理由**: `TAGE::predict()` 與 `TAGE::update()` 對應的 `tagePredict()` / `condBranchUpdate()` 在 `TAGEBase` 中，policy 差異大到需要完全覆寫這兩個函數。但 table 資料結構（`TageEntry`）、`BranchInfo`、`ThreadHistory` 等仍可重用。

**Alternative 考慮**: 完全獨立實作（不繼承）— 工作量太大，且 gem5 的 SimObject 註冊機制仍需繼承鏈。

### D2: GHR 儲存方式

**決定**: 在 `MyTAGEBase` 中新增獨立的 `customGHR`（`std::vector<uint8_t>`，每個 entry 存 3 bits）和 checkpoint buffer，完全不使用 `ThreadHistory::globalHist`。

**理由**: gem5 的 `globalHist` 是 bit array（每個 branch 存 1 bit taken/not-taken），結構與本次 GHR（only-taken，3 bits/entry）不相容，無法改造複用。

### D3: Index Hashing 實作

**決定**: 在 `gindex()` 中直接用查表方式讀取指定 GHR bit，按 table 編號 switch，計算 9-bit GHR_hash，XOR PC[15:7]。

**理由**: 各 table 的 GHR bit 組合是固定常數，直接查表最清晰、最不容易出錯，也最容易對照規格驗證。

### D4: Tag Hashing

**決定**: Tag hashing 的 PC 貢獻與 index 相同，使用 PC[15:7]（9 bits）。Tag 為 13 bits，故取 PC[15:3]（13 bits）作為 tag 計算基礎，不混入 GHR。

**理由**: 使用者確認 tag 與 index 都以 PC[15:7] 為 hashing base。Tag 需要 13 bits，以 PC[15:3] 取得 13 bit PC contribution，維持 tag 與 index 的一致性。

### D5: Prediction / Training / Allocation — 覆寫深度

**決定**: 完全覆寫 `TAGEBase::tagePredict()` 和 `TAGEBase::condBranchUpdate()`，重新實作符合規格的 policy。

**理由**: gem5 原始 `tagePredict()` 使用 `USE_ALT_ON_NA` 機制（初始值 8，threshold 8），與規格的 5-bit use_alternate（threshold 15）邏輯相似但不同；原始 allocation 有 u-bit 週期性 reset，規格明確禁止。完全覆寫確保不殘留舊行為。

## Risks / Trade-offs

- **GHR 推測性更新 + restore 的正確性** → 需要仔細管理 checkpoint（每個 in-flight branch 存一份 GHR snapshot），misprediction squash 時正確還原。風險：checkpoint 記憶體用量。緩解：只存 head pointer + 修改的 3-bit entry。
- **Tag 13-bit 構成** → 目前採用 PC[15:3] 取 13 bits。若硬體實際使用不同位元範圍，需修改 `gtag()`。緩解：函數獨立，容易替換。
- **Allocation 無老化機制** → useful counter 永遠不會被週期性清零，長時間執行可能導致 useful counter 鎖死（所有 entry useful>0，無法 allocate）。緩解：規格已明確要求如此，fallback useful-- 機制可部分緩解。

## Migration Plan

1. 覆寫 `src/cpu/pred/my_tage.hh` — 加入新成員與函數宣告
2. 覆寫 `src/cpu/pred/my_tage.cc` — 實作新 GHR、hashing、policy
3. 確認 `BranchPredictor.py` 與 `SConscript` 已正確註冊（已有，確認即可）
4. Build：`scons build/RISCV/gem5.opt -j$(sysctl -n hw.ncpu)`
5. Smoke test：`./build/RISCV/gem5.opt configs/riscv_o3_run.py --cmd tests/test-progs/hello/bin/riscv/linux/hello --bp-type=MyTAGE`
6. Rollback：git checkout my_tage.hh my_tage.cc

## Open Questions

- Table size（每張 TAGE table 有多少 entry）？規格未提供，目前沿用 gem5 TAGE 預設。
- Bimodal table size？規格未提供，目前沿用 gem5 TAGE 預設。
- Tag 精確計算：目前採 PC[15:3]（13 bits）。若有不同規格請告知。
