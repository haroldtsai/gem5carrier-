## Why

gem5 內建的 TAGE predictor 使用標準 Seznec 參數與 hashing 策略，無法模擬自訂硬體設計的預測行為。本次實作一個完全自訂的 TAGE variant，使用自訂 GHR 結構、index/tag hashing、prediction/training/allocation policy，以忠實反映目標硬體行為。

實作以繼承 gem5 現有 BranchPredictor / TAGE 相關 class 為優先，避免直接修改原始碼，降低未來 gem5 升版的維護成本。若繼承不可行，由實作者說明原因。

## 演算法概述

本 predictor 為 TAGE 架構：1 張 bimodal base table + 8 張 tagged TAGE table（T1–T8，以 history 深度區分）。使用 GHR 長度 456 bits（152 entries × 3 bits/entry，只記錄 taken branch）。Index hashing 為 PC[15:7] XOR 各 table 自訂的 GHR bit 組合（9 bits）。Tag 為 13 bits。Prediction counter 3 bits，useful counter 2 bits。Provider 選最長 history match；alternator 選次長；都 miss 則用 bimodal。

## What Changes

- 新增 `MyTAGE` C++ class，繼承並覆寫 gem5 TAGE base class 的 hashing 與 policy 函數
- 新增 `src/cpu/pred/my_tage.hh` / `my_tage.cc`（覆寫既有檔案）
- 更新 `src/cpu/pred/BranchPredictor.py`、`SConscript` 以註冊新 predictor

## Capabilities

### New Capabilities
- `ghr-structure`: 自訂 GHR — 456 bits，only-taken，每筆 3 bits = PC/target XOR
- `index-tag-hashing`: 各 table 自訂的 GHR bit 選取 + XOR hashing（T1–T8 分別定義）
- `prediction-policy`: Provider/Alternator 選擇 + use_alternate 5-bit 全域計數
- `training-policy`: pred counter / use_alternate / alternator counter 更新規則
- `allocation-policy`: Misprediction 時 victim 選擇（useful==0）+ useful decrement fallback

### Modified Capabilities

## Hardware Parameters

| 參數 | 值 |
|---|---|
| TAGE tables | 8（T1–T8，加上 bimodal base，共 9） |
| Tag bits | 13 |
| Useful counter | 2 bits（新 allocate 時為 1） |
| Pred counter | 3 bits（MSB = direction）；新 allocate taken→100, NT→011 |
| use_alternate | 5 bits，全域共享，threshold = 15 |
| GHR depth | 152 entries × 3 bits = 456 bits |
| GHR entry rule | 只記錄 taken branch |
| GHR entry value | branch_pc[7:5] ^ branch_pc[4:2] ^ target_pc[7:5] ^ target_pc[4:2] |
| GHR update | Speculative（prediction 時寫入），misprediction 時 checkpoint restore |
| Index bits | 9 bits = PC[15:7] XOR GHR_hash[9 bits] |
| Tag bits | 13 bits；hashing base = PC[15:7]（與 index 相同） |

## Index GHR_hash Definition (per table)

| Table | GHR bits（9 bits，MSB first） |
|---|---|
| T1 | [10],[2],[3],[4],[5],[6],[7],[8],[9] |
| T2 | [8],[9]^[10],[1]^[11],[2],[3],[4],[5],[6],[7] |
| T3 | [12],[14],[16],[0],[2],[4],[6],[8],[10] |
| T4 | [11],[13],[15],[17]^[19],[1]^[21],[3]^[23],[5]^[25],[7]^[27],[9]^[29] |
| T5 | [10]^[30]^[50],[12]^[32]^[52],[14]^[34],[16]^[36]^[38],[18]^[20]^[40],[2]^[22]^[42],[4]^[24]^[44],[6]^[26]^[46],[8]^[28]^[48] |
| T6 | [6]^[28]^[48]^[68],[8]^[30]^[50]^[70],[12]^[32]^[52]^[72],[14]^[34]^[54]^[56],[16]^[36]^[38]^[58],[18]^[20]^[40]^[60],[0]^[22]^[42]^[62],[2]^[24]^[44]^[64],[4]^[26]^[46]^[66] |
| T7 | [11]^[61]^[111]^[161],[16]^[66]^[116]^[166],[21]^[71]^[121]^[171],[26]^[76]^[126],[31]^[81]^[131]^[136],[36]^[86]^[91]^[141],[41]^[46]^[96]^[146],[1]^[51]^[101]^[151],[6]^[56]^[106]^[156] |
| T8 | [15]^[145]^[275]^[405],[28]^[158]^[288]^[418],[41]^[171]^[301]^[431],[54]^[184]^[314]^[444],[67]^[197]^[327],[80]^[210]^[340]^[353],[93]^[223]^[236]^[366],[106]^[119]^[249]^[379],[2]^[132]^[262]^[392] |

## Tag Hashing
- tag = br_pc[13:1]（13 bits）XOR folded_GHR（13 bits）
- GHR bit 0 = 最新；每張 table 取不同長度 GHR[n:0]，每 13 bits 一組互相 XOR，不足 13 bits 補零延伸

Table 1: GHR[7:0]   → {00000, GHR[7:0]}
Table 2: GHR[11:0]  → {0, GHR[11:0]}
Table 3: GHR[17:0]  → GHR[12:0] ^ {00000000, GHR[17:13]}
Table 4: GHR[29:0]  → GHR[12:0] ^ GHR[25:13] ^ {000000000, GHR[29:26]}
Table 5: GHR[52:0]  → GHR[12:0] ^ GHR[25:13] ^ GHR[38:26] ^ GHR[51:39] ^ {000000000000, GHR[52]}
Table 6: GHR[82:0]  → GHR[12:0] ^ GHR[25:13] ^ GHR[38:26] ^ GHR[51:39] ^ GHR[64:52] ^ GHR[77:65] ^ {000000000000, GHR[82:78]}
Table 7: GHR[175:0] → 依 13 bits 一組折疊，共 14 組（最後一組補零）
Table 8: GHR[455:0] → 依 13 bits 一組折疊，共 36 組（最後一組補零）

## 不在此次範圍內

- 多執行緒支援（single-thread only）
- 效能評估與 benchmark 比較
- Loop predictor / Statistical Corrector
- BTB、RAS、indirect branch predictor（沿用 gem5 預設）
- 週期性 useful counter reset（無老化機制）

## Impact

- 新增/修改：`src/cpu/pred/my_tage.hh`、`my_tage.cc`
- 修改：`src/cpu/pred/BranchPredictor.py`、`SConscript`
- 無影響：gem5 核心、其他 predictor、memory system
