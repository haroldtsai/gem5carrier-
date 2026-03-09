# MyTAGE: 自訂 TAGE Index/Tag Hashing

## 檔案清單

| 檔案 | 說明 |
|---|---|
| `src/cpu/pred/my_tage.hh` | 類別宣告 (MyTAGEBase, MyTAGE) |
| `src/cpu/pred/my_tage.cc` | 實作（含 demo 修改） |
| `src/cpu/pred/BranchPredictor.py` | SimObject 註冊 |
| `src/cpu/pred/SConscript` | Build 系統註冊 |

---

## 架構

```
MyTAGE (ConditionalPredictor)
  └── tage: MyTAGEBase (TAGEBase)
        ├── gindex()   ← 修改 index hashing
        ├── gtag()     ← 修改 tag hashing
        └── F()        ← 修改 path history folding
```

`MyTAGE` 繼承 `TAGE`，只是把內部的 `TAGEBase` 換成 `MyTAGEBase`。
所有 prediction / training / allocation / useful counter 邏輯都繼承自 `TAGEBase`，
不需要修改。

---

## 三個關鍵函數

### 1. `F(int A, int size, int bank)` — Path History Folding

**位置**: `my_tage.cc:F()`

**用途**: 把 `pathHist`（記錄最近 branch target 低位）壓縮進 table index 寬度。

**原始做法**: bank-dependent rotation + XOR fold
```cpp
// 原始 tage_base.cc
A1 = lower half of A
A2 = upper half of A, rotated by `bank` bits
return A1 ^ A2, rotated by `bank`
```

**Demo 做法**: Multiply-and-fold（polynomial hash）
```cpp
uint32_t h = (uint32_t)A * 0x9e3779b9u;  // 黃金比例常數
h ^= (h >> tableWidth);                   // fold 高位下來
h ^= (uint32_t)(bank * 0x5a17);           // bank 擾動
```

**何時改這裡**: 想讓不同 bank 之間的 path history contribution 差異更大時。

---

### 2. `gindex(ThreadID tid, Addr pc, int bank)` — Tagged Table Index

**位置**: `my_tage.cc:gindex()`

**用途**: 計算對第 `bank` 張 tagged table 的存取 index。
Prediction 和 Training 都會呼叫這個函數（透過 `calculateIndicesAndTags()`）。

**輸入資料**:
```
pc                                 branch PC
threadHistory[tid].computeIndices[bank].comp   folded GHR (for index)
threadHistory[tid].pathHist                    path history register
histLengths[bank]                  此 bank 使用的 GHR 長度
logTagTableSizes[bank]             log2(table size)
instShiftAmt                       PC alignment shift (RISC-V: 1 或 2)
```

**原始做法**:
```
shiftedPc ^ (shiftedPc >> shift) ^ GHR_folded ^ F(pathHist)
```

**Demo 做法**: 加入 Fibonacci hash 項目 + GHR 額外旋轉
```
shiftedPc ^ fibHash(shiftedPc) ^ rotate(GHR_folded) ^ F(pathHist)
```
Fibonacci hash 把 PC bits 更均勻分散，減少相鄰 PC 的 index collision。

**何時改這裡**:
- 想換掉 PC 和 GHR 的混合方式
- 想加入新的 history 來源（例如 branch type、target address）
- 想用 hardware-friendly hash（XOR tree、galois LFSR）

---

### 3. `gtag(ThreadID tid, Addr pc, int bank)` — Tag Computation

**位置**: `my_tage.cc:gtag()`

**用途**: 計算 tagged table entry 的 tag，用於判斷是否命中 (hit)。

**輸入資料**:
```
pc                                           branch PC
threadHistory[tid].computeTags[0][bank].comp folded GHR (tag slice 0)
threadHistory[tid].computeTags[1][bank].comp folded GHR (tag slice 1)
tagTableTagWidths[bank]                      tag 寬度 (bits)
```

**原始做法**:
```
shiftedPc ^ ct0 ^ (ct1 << 1)
```

**Demo 做法**: 加入 upperPc（PC 高位）
```
shiftedPc ^ upperPc ^ ct0 ^ (ct1 << 1)
```
讓在 address space 中位置相近（低位相同）但函式不同（高位不同）的 branch
產生不同 tag，降低 false hit 率。

**何時改這裡**:
- 想讓 tag 包含更多 PC entropy（減少 aliasing）
- 想讓 tag 更依賴 GHR（減少 compulsory miss）

---

## FoldedHistory 結構說明

`computeIndices` 和 `computeTags` 都是 `FoldedHistory` 型態：

```cpp
struct FoldedHistory {
    unsigned comp;       // 壓縮後的值（直接用於 hash）
    int compLength;      // 壓縮目標長度 = logTagTableSizes[bank]
    int origLength;      // 原始 GHR 長度 = histLengths[bank]
};
```

每個 cycle 執行 `update()` 時自動滑動：
- 把最新 branch outcome bit 移入
- 把超出 origLength 的舊 bit 移出
- 維持 `comp` 為 GHR 的折疊版本

**不需要手動更新**，只需在 `gindex()`/`gtag()` 中讀取 `.comp`。

---

## Build & Run

### Rebuild
```bash
scons build/RISCV/gem5.opt -j$(sysctl -n hw.ncpu)
```

### 使用 MyTAGE
```bash
./build/RISCV/gem5.opt configs/riscv_o3_run.py \
    --cmd tests/test-progs/hello/bin/riscv/linux/hello \
    --bp-type=MyTAGE
```

### 與原始 TAGE 比較
```bash
# 原始 TAGE
./build/RISCV/gem5.opt --outdir=m5out_tage configs/riscv_o3_run.py \
    --cmd <your_benchmark> --bp-type=TAGE

# MyTAGE
./build/RISCV/gem5.opt --outdir=m5out_mytage configs/riscv_o3_run.py \
    --cmd <your_benchmark> --bp-type=MyTAGE

# 比較 misprediction rate
grep "branchPred.condIncorrect\|branchPred.condPredicted" \
    m5out_tage/stats.txt m5out_mytage/stats.txt
```

---

## 常見研究方向

| 目標 | 建議修改 |
|---|---|
| 減少 index aliasing | `gindex()`: 換更強的 hash（例如 CRC32、Galois LFSR） |
| 改善 path history mixing | `F()`: 換 polynomial hash 或 tabulated hash |
| 讓 tag 更有 entropy | `gtag()`: 加入 target address bits |
| 加入 branch type | `gindex()` / `gtag()`: XOR in instruction opcode bits |
| 模擬 TAGE-SC-L style hash | 參考 `tage_sc_l.cc` 的 `TAGE_SC_L_TAGE` |
| 更多 history tables | Python config: `--tage-history-tables=N` |

---

## 注意事項

1. **`gindex()` 和 `gtag()` 在 prediction 和 training 時都會被呼叫**，
   確保兩次呼叫的 GHR 狀態一致（speculative vs. committed）是 gem5 內部處理的。

2. **`logTagTableSizes[0]` 是 bimodal table**，index 計算在 `bindex()` 中，
   需要另外 override 才能改 bimodal table 的 index。

3. **Tag width 必須 ≤ 16 bits**（`uint16_t` 的限制）。

4. **Rebuild 必要**：修改 `.cc`/`.hh` 後必須重新 `scons build`。
