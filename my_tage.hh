/*
 * MyTAGE: Custom TAGE branch predictor with modified index/tag hashing.
 *
 * Inherits from TAGEBase and overrides:
 *   - gindex() : tagged table index computation
 *   - gtag()   : tag computation
 *   - F()      : path history folding function
 *
 * The outer ConditionalPredictor wrapper (MyTAGE) is identical to TAGE
 * but plugs in MyTAGEBase instead of TAGEBase.
 */

#ifndef __CPU_PRED_MY_TAGE_HH__
#define __CPU_PRED_MY_TAGE_HH__

#include "cpu/pred/tage.hh"
#include "cpu/pred/tage_base.hh"
#include "params/MyTAGEBase.hh"
#include "params/MyTAGE.hh"

namespace gem5
{
namespace branch_prediction
{

// ── MyTAGEBase ────────────────────────────────────────────────────────────────
//
// Drop-in replacement for TAGEBase with customisable index/tag hashing.
// All other TAGE internals (allocation, update, useful counters, …) are
// inherited unchanged from TAGEBase.
//
class MyTAGEBase : public TAGEBase
{
  public:
    MyTAGEBase(const MyTAGEBaseParams &p);

    // ── Override these three functions to change hashing strategy ──────────

    /**
     * gindex – index into tagged table `bank`.
     *
     * Inputs available:
     *   pc                                        branch PC
     *   threadHistory[tid].computeIndices[bank]   folded GHR for index
     *   threadHistory[tid].pathHist               path history register
     *   histLengths[bank]                         GHR length for this bank
     *   logTagTableSizes[bank]                    log2(table size)
     *   instShiftAmt                              PC alignment shift
     *
     * Must return a value in [0, 2^logTagTableSizes[bank]).
     */
    int gindex(ThreadID tid, Addr pc, int bank) const override;

    /**
     * gtag – tag for tagged table `bank`.
     *
     * Inputs available:
     *   pc                                        branch PC
     *   threadHistory[tid].computeTags[0][bank]   folded GHR tag slice 0
     *   threadHistory[tid].computeTags[1][bank]   folded GHR tag slice 1
     *   tagTableTagWidths[bank]                   tag width in bits
     *
     * Must return a value in [0, 2^tagTableTagWidths[bank]).
     */
    uint16_t gtag(ThreadID tid, Addr pc, int bank) const override;

    /**
     * F – fold path history `A` (length `size`) for table `bank`.
     *
     * Called inside gindex() to compress pathHist into table-index width.
     * Must return a value in [0, 2^logTagTableSizes[bank]).
     */
    int F(int A, int size, int bank) const override;
};


// ── MyTAGE ────────────────────────────────────────────────────────────────────
//
// Thin ConditionalPredictor wrapper – identical to TAGE but wired to
// MyTAGEBase.  You do NOT need to modify this class to experiment with
// index/tag hashing; change MyTAGEBase above instead.
//
class MyTAGE : public TAGE
{
  public:
    MyTAGE(const MyTAGEParams &p);
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_MY_TAGE_HH__
