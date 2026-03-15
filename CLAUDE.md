# gem5 Branch Predictor 研究專案

## 專案目標
在 gem5 中實作並研究各種 branch predictor 演算法。
每次研究的具體演算法由使用者指定，實作以忠實反映硬體行為為優先，不考慮面積與時序限制。
所有演算法細節由使用者提供，必須忠實實作，不可自行假設或補充。

## 技術環境
- 模擬器：gem5，語言：C++17，遵循 gem5 coding style
- CPU model：RISCVO3CPU（如有不同請修改）
- Benchmark：待定

## 目錄慣例
- Branch predictor 原始碼位於：`src/cpu/pred/`
- 新 predictor 必須繼承自 `BranchPredictor` base class
- 每個新 predictor 需要同步新增或修改：`.cc`、`.hh`、`SConscript`、`BranchPredictor.py`

## 重要限制
優先以繼承方式擴充現有 gem5 class，避免直接修改 gem5 原始碼，以利未來升版
- 若繼承技術上不可行，由 Claude Code 判斷並說明原因後再直接修改
- 遇到規格不明確時，必須停下來詢問使用者，不可自行推斷
- 演算法細節未經使用者確認前不可實作

## 開發流程
- 每個研究題目對應一個獨立的 OpenSpec change
- 所有程式碼變更必須先有 OpenSpec 規格才能動程式碼
- 流程參考 `openspec/specs/config.yaml`