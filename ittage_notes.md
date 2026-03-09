# ITTAGE: Indirect Target TAGEd Predictor

> 原始論文: *"A New Case for the TAGE Predictor"*, André Seznec, MICRO 2011
> 前身: TAGE (Seznec & Michaud, 2006)

---

## 1. 背景：為什麼需要 ITTAGE

| 分支類型 | 範例 (RISC-V) | 預測目標 |
|---|---|---|
| 條件分支 | `beq`, `bne`, `blt` | 方向 (taken / not-taken) |
| 直接無條件跳躍 | `jal` | 靜態已知，不需預測 |
| **間接分支** | `jalr` | **目標位址** (動態變化) |

`jalr` 最常出現在：
- **虛擬函式呼叫** (C++ vtable dispatch)
- **函式指標呼叫**
- **switch-case** (jump table)
- **動態連結** (PLT)
- **longjmp / coroutine**

這些目標位址依賴 register 值，無法靜態決定，且同一 `jalr` 指令在不同 call context 下可能跳到不同目標，是現代高效能 CPU 的主要 misprediction 來源。

ITTAGE 把 TAGE 的多歷史長度理念套用到**目標位址預測**，是目前學術界最強的間接分支預測器之一。

---

## 2. 整體架構

```
                        PC
                         │
         ┌───────────────┼───────────────────────┐
         │               │                       │
         ▼               ▼                       ▼
   Base Table        Table T1                Table TN
  (indexed by      (history len H1)        (history len HN)
     PC only)
         │               │                       │
         │         tag match?              tag match?
         │               │                       │
         └───────────────┴───────────────────────┘
                         │
                  longest-match wins
                         │
                    predicted target
```

### 2.1 組成元件

#### Base Predictor（基底預測器）
- 以 **PC** 直接 index（無 GHR）
- 每個 entry 存一個 target address
- 負責沒有 tagged table 命中時的 fallback

#### Tagged Tables T_1 ~ T_N
- 共 N 張（典型值 12～15 張）
- 使用**幾何級數增長**的歷史長度：
  ```
  H_i = H_1 × α^(i-1)   (α ≈ 1.5 ~ 2.0)
  ```
  例如：4, 6, 10, 16, 26, 42, 68, 110, 180, 290, 470, 760
- 平行存取，最長命中者提供預測

---

## 3. Entry 欄位

### Tagged Table Entry

```
┌─────────┬──────────────────────┬─────┬─────┐
│   tag   │        target        │ ctr │  u  │
│(10~15b) │   (full address)     │(2b) │(1b) │
└─────────┴──────────────────────┴─────┴─────┘
```

| 欄位 | 寬度 | 說明 |
|---|---|---|
| `tag` | 10～15 bits | Partial tag，用於比對是否命中 |
| `target` | 全位址 or 壓縮 | 預測的目標 PC |
| `ctr` | 2 bits (飽和計數) | **Confidence counter**：高 = 有信心；低 = 不確定 |
| `u` | 1 bit | **Useful counter**：1 = 此 entry 有效，不應被取代 |

`ctr` 狀態機（2-bit 飽和）：
```
  00 ──taken──→ 01 ──taken──→ 10 ──taken──→ 11
  11 ──wrong──→ 10 ──wrong──→ 01 ──wrong──→ 00
```
`ctr >= 2`（bit[1]=1）代表 confident，`ctr < 2` 代表 weak。

### Base Table Entry

```
┌──────────────────────┐
│        target        │
│   (full address)     │
└──────────────────────┘
```
Base table 只存 target，無 tag、無 ctr、無 u。

---

## 4. History 管理

### Global History Register (GHR)
- 長度 H_max（最長歷史長度）
- 每個分支完成後 shift in 1 bit：`GHR = (GHR << 1) | taken`

### Path History Register
- 記錄最近 pathHistBits 個間接分支的 **target 低位**
- 用於提高不同 call context 的 index 區分度
- 更新：`pathHist = (pathHist << k) ^ (target & mask)`

### Folded History（Compressed History）
為每張 table 各維護兩套 folded GHR（用於 index 和 tag）：
```
computeIndices[i]   : 壓縮至 logTableSize[i] bits，用於 index
computeTags[0][i]   : 壓縮至 tagWidth[i] bits，用於 tag
computeTags[1][i]   : 壓縮至 tagWidth[i]-1 bits，用於 tag（第二段）
```
每個 cycle 執行滑動更新，O(N) 時間。

---

## 5. Index 與 Tag 計算

### Index（`gindex`）
```
index[i] = PC[shifted]
         ^ (PC >> |tableSize[i] - i| + 1)
         ^ computeIndices[i].comp          // folded GHR
         ^ F(pathHist, hlen[i], i)         // folded path hist
```
`F()` 把 pathHist 壓縮進 table size 寬度，加入 bank-dependent rotation。

### Tag（`gtag`）
```
tag[i] = PC[shifted]
       ^ computeTags[0][i].comp
       ^ (computeTags[1][i].comp << 1)
```
使用兩段不同長度的 folded GHR 做 XOR，增加 tag entropy。

---

## 6. Prediction Algorithm

```
Input:  branch PC, ThreadID
Output: predicted target address

1. 對所有 T_1 ~ T_N 同時計算 index[i] 和 tag[i]
   base_index = PC[shifted] & (baseTableSize - 1)

2. hitBank  = 0   // 最長命中的 table index
   altBank  = 0   // 第二長命中的 table index

3. for i = N downto 1:
       if noSkip[i] && tag[i] == table[i][index[i]].tag:
           if hitBank == 0:
               hitBank = i
           elif altBank == 0:
               altBank = i
               break

4. 選擇 provider:
   if hitBank > 0:
       if ctr[hitBank] is WEAK (ctr < 2) and altBank > 0:
           provider = altBank    // 信心不足時用 alt
       else:
           provider = hitBank
   else:
       provider = base table

5. return table[provider][index[provider]].target
```

**關鍵差異（vs TAGE 條件分支）**：
- TAGE 預測的是 taken/not-taken bit，ITTAGE 預測的是完整 target address
- ITTAGE 不需要 alternate prediction 的 "would have been correct" 機制

---

## 7. Training Algorithm

```
Input:  branch PC, actual_target, prediction_was_correct

1. Update provider entry:
   if correct:
       ctr = sat_inc(ctr)           // 信心增加
   else:
       ctr = sat_dec(ctr)           // 信心降低
       if ctr drops to 0:
           entry.target = actual_target  // 更新目標

2. Useful counter update:
   if hitBank != altBank:
       if hit provider was correct and alt was wrong:
           u[hitBank] = sat_inc(u)  // 有貢獻，標記 useful
       if hit provider was wrong and alt was correct:
           u[hitBank] = sat_dec(u)  // 沒用，降低 useful

3. Allocation on misprediction:
   if prediction was wrong:
       try to allocate in tables T_{hitBank+1} ~ T_N:
           find table j where u[j] == 0
           if found:
               install entry: tag = tag[j], target = actual_target
                              ctr = weak_taken (10)
                              u = 0
           else:
               decrement all u in candidate tables

4. Periodic useful reset:
   every 2^logUResetPeriod branches:
       for all entries in all tables:
           u >>= 1    // 讓舊的 useful 位元衰減
```

### 分配策略（Allocation Policy）
- `maxNumAlloc`：單次 misprediction 最多分配幾個 entry（通常 1～2）
- 優先分配在**較長歷史**的 table（提高未來命中率）
- 若候選 tables 都 `u=1`，全部 decrement `u`，等下次機會
- 可加入隨機性（`nrand`）打破平局

---

## 8. 關鍵參數

### Table 結構參數

| 參數 | 說明 | 典型值 |
|---|---|---|
| `nHistoryTables` (N) | Tagged table 數量 | 12 ～ 15 |
| `minHist` (H_1) | 最短歷史長度 | 4 ～ 6 |
| `maxHist` (H_N) | 最長歷史長度 | 640 ～ 3000 |
| `logTagTableSizes[]` | 每張 table 的 log2 大小 | [14, 10, 10, ...] |
| `tagTableTagWidths[]` | 每張 table 的 tag 寬度 (bits) | [0, 7, 7, 8, ...] |
| `tagTableCounterBits` | ctr 位元數 | 2 ～ 3 |
| `tagTableUBits` | useful counter 位元數 | 1 ～ 2 |

### History 參數

| 參數 | 說明 | 典型值 |
|---|---|---|
| `pathHistBits` | Path history 長度 (bits) | 16 ～ 27 |
| `histBufferSize` | GHR buffer 大小 | 2M entries |

### 更新策略參數

| 參數 | 說明 | 典型值 |
|---|---|---|
| `logUResetPeriod` | Useful counter reset 週期 (log2) | 10 ～ 19 |
| `maxNumAlloc` | 每次 mispred 最多分配 entry 數 | 1 ～ 2 |
| `numUseAltOnNa` | USE_ALT_ON_NA counter 數量 | 1 ～ 16 |
| `useAltOnNaBits` | USE_ALT_ON_NA counter 寬度 | 4 ～ 5 |
| `initialTCounterValue` | tCounter 初始值 | 2^17 ～ 2^9 |

### noSkip 向量
```python
noSkip = [0, 1, 1, 0, 1, ...]   # 長度 = nHistoryTables + 1
```
- `noSkip[i] = 0`：跳過 table i（不存取，省 area）
- `noSkip[i] = 1`：啟用 table i
- TAGE_SC_L_64KB 用此來實現 2-way associativity（相鄰一對 table 共用）

---

## 9. ITTAGE vs TAGE 差異對照

| 面向 | TAGE（條件分支） | ITTAGE（間接分支）|
|---|---|---|
| 預測輸出 | taken / not-taken (1 bit) | target address (64 bits) |
| Entry 主要欄位 | tag, ctr, u | tag, **target**, ctr, u |
| Base predictor | bimodal (2-bit counter) | target address array |
| Hit 判斷 | tag match | tag match |
| Provider 選擇 | longest match | longest match |
| Misprediction | 方向錯誤 | 目標位址錯誤 |
| 分配觸發 | 方向錯誤時 | 目標錯誤時 |
| Entry 大小 | ~5 bits | ~70 bits（含 64-bit target） |
| Storage overhead | 低 | 高（target 佔主要空間）|

---

## 10. gem5 現有實作：SimpleIndirectPredictor

gem5 v25 **沒有原生 ITTAGE 實作**，`SimpleIndirectPredictor` 是最接近的替代品。

### SimpleIndirectPredictor 架構

```
Set-associative cache (numSets × numWays)
每個 entry:  tag | target
```

### 與 ITTAGE 的差異

| 面向 | SimpleIndirectPredictor | ITTAGE |
|---|---|---|
| 結構 | 單一 set-associative cache | N 張 tagged tables + base |
| 歷史長度 | 單一（固定） | 幾何級數多長度 |
| ctr（信心） | **無** | 有（2-bit saturating） |
| u（useful） | **無** | 有（1-2 bit） |
| 替換策略 | Random | u-bit guided |
| Index hash | PC ^ GHR ^ pathTargets | PC ^ GHR ^ F(pathHist) per table |
| Tag | PC 低位 | PC ^ folded_GHR |

### SimpleIndirectPredictor 參數（gem5）

```python
SimpleIndirectPredictor(
    indirectSets        = 256,   # Cache sets (must be power of 2)
    indirectWays        = 2,     # Associativity
    indirectTagSize     = 16,    # Tag bits
    indirectPathLength  = 3,     # Path history length (# of past targets)
    indirectGHRBits     = 13,    # GHR bits used for index
    indirectHashGHR     = True,  # Hash GHR into index
    indirectHashTargets = True,  # Hash past targets into index
    speculativePathLength = 256, # Buffer for in-flight speculative branches
)
```

### Index 計算（SimpleIndirectPredictor）

```cpp
// getSetIndex()
hash = PC >> instShift
if hashGHR:    hash ^= ghr
if hashTargets:
    for last `pathLength` indirect branch targets:
        hash ^= (target >> (instShift + p * hash_shift))
return hash & (numSets - 1)
```

---

## 11. 在 gem5 中實作 ITTAGE

gem5 目前沒有 ITTAGE，若要研究 ITTAGE 需自行實作。

### 繼承路徑

```
IndirectPredictor (base class, indirect.hh)
└── SimpleIndirectPredictor    ← 可以參考這個改寫成 ITTAGE
└── YourITTAGE                 ← 繼承 IndirectPredictor 自行實作
```

### 需要實作的 interface

```cpp
class IndirectPredictor : public SimObject {
    // 查詢：給 PC，回傳預測的 target
    virtual const PCStateBase* lookup(ThreadID tid, InstSeqNum sn,
                                      Addr pc, void* &iHistory) = 0;
    // 更新：分支解決後更新 table 和 history
    virtual void update(ThreadID tid, InstSeqNum sn, Addr pc,
                        bool squash, bool taken,
                        const PCStateBase& target,
                        BranchType br_type, void* &iHistory) = 0;
    // Squash：流水線清洗時復原 history
    virtual void squash(ThreadID tid, InstSeqNum sn,
                        void* &iHistory) = 0;
    // Commit：確認提交，釋放 history
    virtual void commit(ThreadID tid, InstSeqNum sn,
                        void* &iHistory) = 0;
};
```

### 核心資料結構（ITTAGE 實作建議）

```cpp
struct ITTAGEEntry {
    uint16_t  tag    = 0;
    Addr      target = 0;     // 64-bit target
    int8_t    ctr    = 0;     // 2-bit saturating (-2 ~ +1, or 0~3)
    uint8_t   u      = 0;     // 1-bit useful
};

// N tagged tables
std::vector<std::vector<ITTAGEEntry>> taggedTables;  // [nTables][tableSize]

// Base table (no tag, only target)
std::vector<Addr> baseTable;  // [baseTableSize]

// Per-thread history
struct ThreadHistory {
    std::vector<uint8_t>  globalHist;    // circular buffer for GHR
    uint32_t              pathHist = 0;  // path history register
    FoldedHistory*        computeIndices;
    FoldedHistory*        computeTags[2];
};
```

---

## 12. Storage Budget 估算

以 ITTAGE 64KB 配置為例（學術標準）：

```
N = 12 tables
每個 entry = tag(12b) + target(64b) + ctr(2b) + u(1b) = 79 bits ≈ 10 bytes

Table 1:  1024 entries × 10B = 10 KB
Table 2:  512  entries × 10B =  5 KB
...
Total tagged: ~60 KB

Base table: 16K entries × 8B = 128 KB  (或壓縮方式儲存)
```

實際上 target 通常用 **partial address** 或 **offset** 來壓縮儲存，
搭配 BTB 的 full address 做組合，可大幅降低 storage。

---

## 13. 相關論文

| 年份 | 論文 | 貢獻 |
|---|---|---|
| 2006 | *A Case for (Partially) TAgged GEometric History Length Branch Prediction* (Seznec & Michaud) | TAGE 原始論文 |
| 2011 | *A New Case for the TAGE Predictor* (Seznec) | **ITTAGE 提出** |
| 2016 | *TAGE-SC-L Branch Predictors* (Seznec) | TAGE-SC-L，加入 Statistical Corrector 和 Loop Predictor |
| 2022 | *The ITTAGE Indirect Branch Predictor* (Vavro et al.) | ITTAGE 硬體實作分析 |

---

## 14. 快速對照表：gem5 Indirect Predictor 參數

```bash
# gem5 使用 SimpleIndirectPredictor（預設）
./build/RISCV/gem5.opt configs/riscv_o3_run.py \
    --cmd <binary> \
    --bp-type=TAGE \
    --indirect-sets=1024 \
    --indirect-ways=4 \
    --indirect-path-length=8 \
    --indirect-ghr-bits=16

# 對應關係
# --indirect-sets       ← numSets  (ITTAGE: nEntries per table)
# --indirect-ways       ← numWays  (ITTAGE: associativity)
# --indirect-path-length← pathLength (ITTAGE: 用幾個歷史 target 做 hash)
# --indirect-ghr-bits   ← ghrNumBits (ITTAGE: GHR 寬度)
```
