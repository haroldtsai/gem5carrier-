/*
 * MyTAGE implementation.
 *
 * Demo modification:
 *
 *  Original TAGE hashing (tage_base.cc):
 *    gindex = shiftedPc
 *           ^ (shiftedPc >> |tableSize - bank| + 1)    // second PC slice
 *           ^ computeIndices[bank].comp                 // folded GHR
 *           ^ F(pathHist, hlen, bank)                   // folded path hist
 *
 *    gtag   = shiftedPc
 *           ^ computeTags[0][bank].comp
 *           ^ (computeTags[1][bank].comp << 1)
 *
 *    F(A)   = rotate-and-XOR fold of A into table-width bits
 *
 *  Demo changes in MyTAGEBase:
 *    gindex : adds a Fibonacci-hash term on the PC for better dispersion,
 *             and mixes in an extra rotation of the folded GHR.
 *
 *    gtag   : adds XOR with the upper half of the PC to reduce aliasing
 *             across branches that are close in the address space.
 *
 *    F      : replaces the rotation fold with a polynomial (multiply-and-XOR)
 *             fold – empirically gives lower inter-bank collisions.
 *
 *  These are intentionally simple so that the structure is easy to read.
 *  Replace the bodies with your own ideas.
 */

#include "cpu/pred/my_tage.hh"

#include "base/intmath.hh"
#include "debug/Tage.hh"
#include "params/MyTAGEBase.hh"
#include "params/MyTAGE.hh"

namespace gem5
{
namespace branch_prediction
{

// ── MyTAGEBase ────────────────────────────────────────────────────────────────

MyTAGEBase::MyTAGEBase(const MyTAGEBaseParams &p)
    : TAGEBase(p)
{
}

// ── F(): path history folding ─────────────────────────────────────────────────
//
// Demo: polynomial fold using a Knuth multiplicative constant instead of
// the original bank-dependent rotation.
//
// Original:
//   split A into two halves, rotate each by `bank` positions, XOR together.
//
// This version:
//   multiply A by a constant to spread bits, then fold by XOR-ing the upper
//   half down onto the lower half.
//
int
MyTAGEBase::F(int A, int size, int bank) const
{
    const unsigned tableWidth = logTagTableSizes[bank];
    const unsigned mask = (1ULL << tableWidth) - 1;

    // Mask to `size` bits first (same as original)
    A = A & ((1ULL << size) - 1);

    // === YOUR MODIFICATION HERE ===
    //
    // Demo: multiply-and-fold (polynomial hash).
    // The constant 0x9e3779b9 is derived from the golden ratio;
    // it distributes the input bits evenly across the output width.
    //
    uint32_t h = (uint32_t)A * 0x9e3779b9u;
    h ^= (h >> tableWidth);          // fold upper half down
    h ^= (uint32_t)(bank * 0x5a17);  // bank-specific perturbation

    return (int)(h & mask);
}

// ── gindex(): tagged-table index ─────────────────────────────────────────────
//
// Demo modification: adds a Fibonacci-hash term on the PC.
//
// Original:
//   shiftedPc ^ (shiftedPc >> shift) ^ GHR_folded ^ F(pathHist)
//
// This version:
//   adds  (shiftedPc * fibConst) >> 16
//   and   rotates the folded GHR by one extra position before XOR.
//
int
MyTAGEBase::gindex(ThreadID tid, Addr pc, int bank) const
{
    const auto &tHist  = threadHistory[tid];
    const unsigned tableWidth = logTagTableSizes[bank];
    const unsigned mask = (1ULL << tableWidth) - 1;

    int hlen = (histLengths[bank] > pathHistBits) ? pathHistBits
                                                  : histLengths[bank];
    const unsigned shiftedPc = (unsigned)(pc >> instShiftAmt);

    // === YOUR MODIFICATION HERE ===
    //
    // Demo: Fibonacci hash of PC gives better dispersion than a plain shift,
    // especially for benchmarks with many branches in a tight address range.
    //
    const uint32_t fibHash = ((uint32_t)shiftedPc * 0x9e3779b9u) >> 16;

    // Extra rotation of the folded GHR (one bit left, wrap-around)
    const unsigned ghrComp  = tHist.computeIndices[bank].comp;
    const unsigned ghrExtra = ((ghrComp << 1) | (ghrComp >> (tableWidth - 1)))
                               & mask;

    int index = shiftedPc
              ^ (int)fibHash
              ^ ghrExtra
              ^ F(tHist.pathHist, hlen, bank);

    return (int)(index & mask);
}

// ── gtag(): tag computation ───────────────────────────────────────────────────
//
// Demo modification: XOR with the upper half of the PC to reduce aliasing
// between branches that are close in the lower address bits but differ in
// upper bits (e.g. identical loop bodies in different functions).
//
// Original:
//   shiftedPc ^ ct0 ^ (ct1 << 1)
//
uint16_t
MyTAGEBase::gtag(ThreadID tid, Addr pc, int bank) const
{
    const auto &tHist = threadHistory[tid];
    const unsigned tagWidth = tagTableTagWidths[bank];
    const unsigned mask = (1ULL << tagWidth) - 1;

    const unsigned shiftedPc = (unsigned)(pc >> instShiftAmt);

    // === YOUR MODIFICATION HERE ===
    //
    // Demo: additionally XOR in the upper half of the PC (bits above the
    // index field) so that two branches that alias in the index but differ
    // in high PC bits get different tags → fewer false hits.
    //
    const unsigned upperPc = shiftedPc >> (logTagTableSizes[bank]);

    int tag = (int)shiftedPc
            ^ (int)upperPc
            ^ tHist.computeTags[0][bank].comp
            ^ (tHist.computeTags[1][bank].comp << 1);

    return (uint16_t)(tag & mask);
}


// ── MyTAGE ────────────────────────────────────────────────────────────────────

MyTAGE::MyTAGE(const MyTAGEParams &p)
    : TAGE(p)
{
    // tage pointer is set by TAGE(p) via params.tage which points to
    // the MyTAGEBase instance declared in BranchPredictor.py
}

} // namespace branch_prediction
} // namespace gem5
