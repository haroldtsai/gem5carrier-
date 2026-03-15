## 1. 繼承架構確認（my_tage.hh）

- [x] 1.1 確認 `MyTAGEBase : TAGEBase` 與 `MyTAGE : TAGE` 繼承鏈可覆寫所有必要函數，不需修改 gem5 核心檔案（tage_base.hh/cc, tage.hh/cc）
- [x] 1.2 確認 `BranchPredictor.py` 已正確宣告 `MyTAGEBase(TAGEBase)` 與 `MyTAGE(TAGE)` class（對應 CLAUDE.md 繼承優先原則）
- [x] 1.3 確認 `SConscript` 已正確 register `Source('my_tage.cc')` 與 sim_objects（對應 CLAUDE.md 繼承優先原則）

## 2. Header 宣告（my_tage.hh）

- [x] 2.1 宣告自訂 GHR 儲存結構：`customGHR`（`std::deque<uint8_t>`，最多 152 entries，每 entry 3 bits）與 per-branch checkpoint（`seqNum → GHR snapshot`）（對應 ghr-structure spec）
- [x] 2.2 宣告全域 `use_alternate` 5-bit counter（`int useAltOnNa`，range 0–31）（對應 prediction-policy spec）
- [x] 2.3 宣告 `computeIndexGHRHash(int bank) const` 回傳 9-bit index GHR hash（對應 index-tag-hashing spec）
- [x] 2.4 宣告 `computeTagFoldedGHR(int bank) const` 回傳 13-bit folded GHR for tag（對應 index-tag-hashing spec）
- [x] 2.5 覆寫宣告：`gindex(ThreadID, Addr, int)` 與 `gtag(ThreadID, Addr, int)`（對應 index-tag-hashing spec）
- [x] 2.6 覆寫宣告：`tagePredict(ThreadID, Addr, bool, BranchInfo*)` 與 `condBranchUpdate(ThreadID, Addr, bool, BranchInfo*, unsigned)`（對應 prediction-policy / training-policy / allocation-policy spec）

## 3. GHR 實作（my_tage.cc）

- [x] 3.1 實作 GHR 初始化（建構子）：`customGHR` 清空，checkpoint map 清空（對應 ghr-structure spec）
- [x] 3.2 實作 GHR speculative push：**只在 taken 時**寫入 `branch_pc[7:5] ^ branch_pc[4:2] ^ target_pc[7:5] ^ target_pc[4:2]`（3 bits）至 `customGHR` 前端；not-taken 不寫入（對應 ghr-structure spec）
- [x] 3.3 實作 GHR checkpoint：predict 時將當前 `customGHR` snapshot（或 head state）存入以 `seqNum` 為 key 的 map（對應 ghr-structure spec）
- [x] 3.4 實作 GHR restore：squash 時從 checkpoint map 取出對應 snapshot 還原 `customGHR`，並清除該 seqNum 之後的所有 checkpoint（對應 ghr-structure spec）

## 4. Index Hashing 實作（my_tage.cc）

- [x] 4.1 實作 `computeIndexGHRHash(int bank)`：以 switch(bank) 分支，依 proposal T1–T8 定義 XOR 指定 GHR flattened bit positions，回傳 9-bit hash（對應 index-tag-hashing spec）
- [x] 4.2 實作 `gindex()`：回傳 `((pc >> 7) & 0x1FF) ^ computeIndexGHRHash(bank)`（9 bits）（對應 index-tag-hashing spec）

## 5. Tag Hashing 實作（my_tage.cc）

- [x] 5.1 實作 `computeTagFoldedGHR(int bank)`：依 proposal T1–T8 定義取 GHR[n:0]（bit 0 = 最新），每 13 bits 一組互相 XOR，末組補零延伸至 13 bits（T1: GHR[7:0], T2: GHR[11:0], T3: GHR[17:0], T4: GHR[29:0], T5: GHR[52:0], T6: GHR[82:0], T7: GHR[175:0], T8: GHR[455:0]）（對應 index-tag-hashing spec）
- [x] 5.2 實作 `gtag()`：回傳 `((pc >> 1) & 0x1FFF) ^ computeTagFoldedGHR(bank)`（13 bits = `br_pc[13:1] XOR folded_GHR`）（對應 index-tag-hashing spec）

## 6. Prediction Policy 實作（my_tage.cc）

- [x] 6.1 實作 `tagePredict()`：掃描 T1..T8，以 `gindex()` / `gtag()` 找 longest match（provider）與 second-longest（alternator）（對應 prediction-policy spec）
- [x] 6.2 實作 newly-allocated 判斷：`entry.useful == 1 && entry.ctr ∈ {3, 4}`（對應 prediction-policy spec）
- [x] 6.3 實作 use_alternate threshold 邏輯：provider newly-allocated 且 `useAltOnNa >= 15` → 使用 alternator 預測（對應 prediction-policy spec）

## 7. Training Policy 實作（my_tage.cc）

- [x] 7.1 實作 `condBranchUpdate()` 中 `useAltOnNa` 更新：alternator 猜對 → ++，猜錯 → --（saturate 0–31）（對應 training-policy spec）
- [x] 7.2 實作 provider pred counter 更新：TAGE table prediction，taken → ctr++，not-taken → ctr--（saturate 0–7）（對應 training-policy spec）
- [x] 7.3 實作 alternator counter 增加：`provider ≠ alternator 方向 && provider 猜錯 && provider newly-allocated` → alternator ctr++（對應 training-policy spec）
- [x] 7.4 實作 alternator counter 減少：`provider ≠ alternator 方向 && provider 猜對` → alternator ctr--（對應 training-policy spec）

## 8. Allocation Policy 實作（my_tage.cc）

- [x] 8.1 實作 misprediction 觸發 allocation 路徑（只在 misprediction 進入）（對應 allocation-policy spec）
- [x] 8.2 實作 victim 選擇：搜尋 T(provider+1)..T8，找第一個 `useful == 0` 的 entry（對應 allocation-policy spec）
- [x] 8.3 實作 allocation 初始值：`useful = 1`；taken → `ctr = 4`（100b），not-taken → `ctr = 3`（011b）（對應 allocation-policy spec）
- [x] 8.4 實作 fallback useful decrement：所有更深 table 皆無 `useful == 0` → 各自對應 index entry 的 `useful--`（saturate at 0）（對應 allocation-policy spec）
- [x] 8.5 確認無週期性 useful counter reset 程式碼存在（對應 allocation-policy spec）

## 9. Build 驗證

- [x] 9.1 執行 `scons build/RISCV/gem5.opt -j$(sysctl -n hw.ncpu)` 確認編譯成功，無 warning/error（my_tage.cc）

## 10. Smoke Test

- [x] 10.1 執行 hello benchmark 確認無 segfault：`./build/RISCV/gem5.opt configs/riscv_o3_run.py --cmd tests/test-progs/hello/bin/riscv/linux/hello --bp-type=MyTAGE`
- [x] 10.2 確認 `m5out/stats.txt` 中 branch pred count > 0、MPKI 有值，predictor 正常運作
