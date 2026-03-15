/*
 * MyTAGE: Custom TAGE branch predictor.
 *
 * Hardware spec:
 *   - 1 bimodal base + 8 tagged TAGE tables (T1–T8)
 *   - GHR: 152 entries x 3 bits = 456 bits, only-taken, speculative
 *   - Index: PC[15:7] XOR per-table GHR hash (9 bits)
 *   - Tag:   br_pc[13:1] XOR folded GHR (13 bits, per-table depth)
 *   - Pred counter: 3 bits unsigned (0–7), MSB = direction
 *   - Useful counter: 2 bits, set to 1 on alloc, no periodic reset
 *   - use_alternate: 5-bit global counter (0–31), threshold = 15
 */

#ifndef __CPU_PRED_MY_TAGE_HH__
#define __CPU_PRED_MY_TAGE_HH__

#include <deque>
#include <cstdint>

#include "cpu/pred/tage.hh"
#include "cpu/pred/tage_base.hh"
#include "params/MyTAGEBase.hh"
#include "params/MyTAGE.hh"

namespace gem5
{
namespace branch_prediction
{

// ── MyTAGEBase ────────────────────────────────────────────────────────────────

class MyTAGEBase : public TAGEBase
{
  public:

    // Extended BranchInfo with GHR checkpoint and custom fields.
    struct MyBranchInfo : public TAGEBase::BranchInfo
    {
        std::deque<uint8_t> ghrSnapshot; // GHR state saved at predict time
        bool newlyAllocated;             // useful==1 && ctr∈{3,4}

        MyBranchInfo(const TAGEBase &tage, Addr pc, bool cond)
            : TAGEBase::BranchInfo(tage, pc, cond),
              newlyAllocated(false)
        {}
    };

    MyTAGEBase(const MyTAGEBaseParams &p);

    // ── Custom prediction (called from MyTAGE::predict) ────────────────────
    bool myTagePredict(ThreadID tid, Addr pc, TAGEBase::BranchInfo *bi);

    // ── Overrides ──────────────────────────────────────────────────────────

    /** Override to return MyBranchInfo. */
    BranchInfo *makeBranchInfo(Addr pc, bool conditional) override;

    /**
     * Index hashing: ((pc >> 7) & 0x1FF) XOR computeIndexGHRHash(bank).
     * 9-bit result.
     */
    int gindex(ThreadID tid, Addr pc, int bank) const override;

    /**
     * Tag hashing: ((pc >> 1) & 0x1FFF) XOR computeTagFoldedGHR(bank).
     * 13-bit result.
     */
    uint16_t gtag(ThreadID tid, Addr pc, int bank) const override;

    /**
     * Speculative GHR push (only when speculative && taken, first call
     * per branch). Saves GHR snapshot into bi before pushing.
     */
    void updateHistories(ThreadID tid, Addr branch_pc, bool speculative,
                         bool taken, Addr target,
                         const StaticInstPtr &inst,
                         BranchInfo *bi) override;

    /**
     * Misprediction recovery: restore GHR from snapshot, then push
     * correct outcome if taken.
     */
    void squash(ThreadID tid, bool taken, Addr target,
                const StaticInstPtr &inst, BranchInfo *bi) override;

    /**
     * Training + allocation per hardware spec:
     *   - use_alternate update
     *   - pred counter update
     *   - alternator counter update
     *   - allocation on misprediction (useful==0 victim or fallback)
     */
    void condBranchUpdate(ThreadID tid, Addr branch_pc, bool taken,
                          BranchInfo *bi, int nrand, Addr corrTarget,
                          bool pred, bool preAdjustAlloc = false) override;

    // ── Custom GHR ─────────────────────────────────────────────────────────
    // Public so MyTAGE::squash can access for branch-cancellation restore.
    std::deque<uint8_t> customGHR; // index 0 = newest taken branch

  private:

    int useAltOnNa; // 5-bit global counter, range 0–31

    // ── GHR helpers ────────────────────────────────────────────────────────

    /** Read bit bitIdx from flattened 456-bit GHR (bit 0 = newest). */
    uint8_t getGHRBit(int bitIdx) const;

    /** Compute 3-bit entry and push to front of customGHR. */
    void pushGHREntry(Addr branch_pc, Addr target);

    // ── Hashing helpers ────────────────────────────────────────────────────

    /** Per-table 9-bit index GHR hash (T1–T8 via switch). */
    int computeIndexGHRHash(int bank) const;

    /**
     * Per-table 13-bit folded tag GHR hash.
     * Folds GHR[0..maxBit] in 13-bit chunks via XOR; last chunk zero-padded.
     */
    uint16_t computeTagFoldedGHR(int bank) const;

    /** Generic fold: XOR bits [0..maxBit] in 13-bit groups. */
    uint16_t foldGHR(int maxBit) const;

    // ── Counter helpers ────────────────────────────────────────────────────

    static void satIncUnsigned(int8_t &ctr, int8_t maxVal);
    static void satDecUnsigned(int8_t &ctr, int8_t minVal);
};


// ── MyTAGE ────────────────────────────────────────────────────────────────────

class MyTAGE : public TAGE
{
  public:
    MyTAGE(const MyTAGEParams &p);

    /**
     * Override predict to call MyTAGEBase::myTagePredict instead of the
     * non-virtual TAGEBase::tagePredict.
     */
    bool predict(ThreadID tid, Addr pc, bool cond_branch,
                 void* &b) override;

    /**
     * Override squash (branch-cancellation path) to restore customGHR.
     */
    void squash(ThreadID tid, void* &bp_history) override;
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_MY_TAGE_HH__
