/*
 * MyTAGE implementation — custom TAGE branch predictor.
 *
 * Hardware spec summary:
 *   GHR  : 152 entries x 3 bits, only-taken, speculative
 *   Index: PC[15:7] XOR per-table GHR hash (9 bits)
 *   Tag  : br_pc[13:1] XOR folded GHR per table (13 bits)
 *   Pred : 3-bit unsigned (0–7), direction = (ctr >= 4)
 *   Alloc: misprediction only, victim useful==0, fallback useful--
 *   u-bit: 2-bit, set=1 on alloc, no periodic reset
 *   UseAlt: 5-bit global (0–31), threshold=15
 */

#include "cpu/pred/my_tage.hh"

#include <algorithm>
#include <cassert>

#include "base/intmath.hh"
#include "debug/Tage.hh"
#include "params/MyTAGEBase.hh"
#include "params/MyTAGE.hh"

namespace gem5
{
namespace branch_prediction
{

// ============================================================================
// MyTAGEBase
// ============================================================================

MyTAGEBase::MyTAGEBase(const MyTAGEBaseParams &p)
    : TAGEBase(p),
      useAltOnNa(16)   // midpoint of 0–31
{
}

// ── makeBranchInfo ────────────────────────────────────────────────────────────

TAGEBase::BranchInfo *
MyTAGEBase::makeBranchInfo(Addr pc, bool conditional)
{
    return new MyBranchInfo(*this, pc, conditional);
}

// ── GHR helpers ───────────────────────────────────────────────────────────────

uint8_t
MyTAGEBase::getGHRBit(int bitIdx) const
{
    // Flat 456-bit view: entry k occupies bits [3k, 3k+2], MSB first.
    // bit 0 = MSB of entry 0 (newest).
    int entryIdx = bitIdx / 3;
    int bitPos   = 2 - (bitIdx % 3);   // 2=MSB, 0=LSB within entry
    if (entryIdx >= (int)customGHR.size()) return 0;
    return (customGHR[entryIdx] >> bitPos) & 1;
}

void
MyTAGEBase::pushGHREntry(Addr branch_pc, Addr target)
{
    uint8_t entry =
        ((branch_pc >> 5) & 0x7) ^
        ((branch_pc >> 2) & 0x7) ^
        ((target    >> 5) & 0x7) ^
        ((target    >> 2) & 0x7);
    entry &= 0x7;   // 3 bits
    customGHR.push_front(entry);
    if (customGHR.size() > 152)
        customGHR.pop_back();
}

// ── Index hashing ─────────────────────────────────────────────────────────────

int
MyTAGEBase::computeIndexGHRHash(int bank) const
{
    // Each table defines a 9-bit GHR_hash: 9 output bits, MSB first.
    // Bit ordering: element[0] → output bit 8 (MSB), element[8] → bit 0 (LSB).
    // Some elements are XOR of multiple GHR bit positions.
    auto G = [this](int i) -> uint8_t { return getGHRBit(i); };
    int h = 0;
    switch (bank) {
      case 1:
        h = (G(10)            << 8) | (G(2) << 7)  | (G(3) << 6)  |
            (G(4)             << 5) | (G(5) << 4)  | (G(6) << 3)  |
            (G(7)             << 2) | (G(8) << 1)  |  G(9);
        break;
      case 2:
        h = (G(8)             << 8) |
            ((G(9)^G(10))     << 7) |
            ((G(1)^G(11))     << 6) |
            (G(2)             << 5) | (G(3) << 4) | (G(4) << 3) |
            (G(5)             << 2) | (G(6) << 1) |  G(7);
        break;
      case 3:
        h = (G(12) << 8) | (G(14) << 7) | (G(16) << 6) |
            (G(0)  << 5) | (G(2)  << 4) | (G(4)  << 3) |
            (G(6)  << 2) | (G(8)  << 1) |  G(10);
        break;
      case 4:
        h = (G(11)            << 8) |
            (G(13)            << 7) |
            (G(15)            << 6) |
            ((G(17)^G(19))    << 5) |
            ((G(1) ^G(21))    << 4) |
            ((G(3) ^G(23))    << 3) |
            ((G(5) ^G(25))    << 2) |
            ((G(7) ^G(27))    << 1) |
            (G(9) ^G(29));
        break;
      case 5:
        h = ((G(10)^G(30)^G(50))      << 8) |
            ((G(12)^G(32)^G(52))      << 7) |
            ((G(14)^G(34))            << 6) |
            ((G(16)^G(36)^G(38))      << 5) |
            ((G(18)^G(20)^G(40))      << 4) |
            ((G(2) ^G(22)^G(42))      << 3) |
            ((G(4) ^G(24)^G(44))      << 2) |
            ((G(6) ^G(26)^G(46))      << 1) |
            (G(8) ^G(28)^G(48));
        break;
      case 6:
        h = ((G(6) ^G(28)^G(48)^G(68))      << 8) |
            ((G(8) ^G(30)^G(50)^G(70))      << 7) |
            ((G(12)^G(32)^G(52)^G(72))      << 6) |
            ((G(14)^G(34)^G(54)^G(56))      << 5) |
            ((G(16)^G(36)^G(38)^G(58))      << 4) |
            ((G(18)^G(20)^G(40)^G(60))      << 3) |
            ((G(0) ^G(22)^G(42)^G(62))      << 2) |
            ((G(2) ^G(24)^G(44)^G(64))      << 1) |
            (G(4) ^G(26)^G(46)^G(66));
        break;
      case 7:
        h = ((G(11)^G(61) ^G(111)^G(161))   << 8) |
            ((G(16)^G(66) ^G(116)^G(166))   << 7) |
            ((G(21)^G(71) ^G(121)^G(171))   << 6) |
            ((G(26)^G(76) ^G(126))           << 5) |
            ((G(31)^G(81) ^G(131)^G(136))   << 4) |
            ((G(36)^G(86) ^G(91) ^G(141))   << 3) |
            ((G(41)^G(46) ^G(96) ^G(146))   << 2) |
            ((G(1) ^G(51) ^G(101)^G(151))   << 1) |
            (G(6) ^G(56) ^G(106)^G(156));
        break;
      case 8:
        h = ((G(15)^G(145)^G(275)^G(405))   << 8) |
            ((G(28)^G(158)^G(288)^G(418))   << 7) |
            ((G(41)^G(171)^G(301)^G(431))   << 6) |
            ((G(54)^G(184)^G(314)^G(444))   << 5) |
            ((G(67)^G(197)^G(327))           << 4) |
            ((G(80)^G(210)^G(340)^G(353))   << 3) |
            ((G(93)^G(223)^G(236)^G(366))   << 2) |
            ((G(106)^G(119)^G(249)^G(379))  << 1) |
            (G(2) ^G(132)^G(262)^G(392));
        break;
      default:
        h = 0;
        break;
    }
    return h & 0x1FF;   // 9 bits
}

int
MyTAGEBase::gindex(ThreadID /*tid*/, Addr pc, int bank) const
{
    int hash = ((pc >> 7) & 0x1FF) ^ computeIndexGHRHash(bank);
    return hash & 0x1FF;    // 9 bits; table must have >= 512 entries
}

// ── Tag hashing ───────────────────────────────────────────────────────────────

uint16_t
MyTAGEBase::foldGHR(int maxBit) const
{
    // Fold GHR bits [0..maxBit] into a 13-bit result.
    // Group bits into 13-bit chunks; XOR all chunks together.
    // Last (possibly partial) chunk is zero-padded.
    uint16_t result = 0;
    for (int startBit = 0; startBit <= maxBit; startBit += 13) {
        int endBit = std::min(startBit + 12, maxBit);
        uint16_t chunk = 0;
        for (int b = startBit; b <= endBit; b++) {
            chunk |= (uint16_t)(getGHRBit(b)) << (b - startBit);
        }
        result ^= chunk;
    }
    return result & 0x1FFF;   // 13 bits
}

uint16_t
MyTAGEBase::computeTagFoldedGHR(int bank) const
{
    // Fold depth per table (highest bit index, inclusive).
    static const int maxBits[9] = {
        0,    // unused (bank 0 = bimodal)
        7,    // T1: GHR[7:0]
        11,   // T2: GHR[11:0]
        17,   // T3: GHR[17:0]
        29,   // T4: GHR[29:0]
        52,   // T5: GHR[52:0]
        82,   // T6: GHR[82:0]
        175,  // T7: GHR[175:0]
        455,  // T8: GHR[455:0]
    };
    if (bank < 1 || bank > 8) return 0;
    return foldGHR(maxBits[bank]);
}

uint16_t
MyTAGEBase::gtag(ThreadID /*tid*/, Addr pc, int bank) const
{
    uint16_t tag = (uint16_t)((pc >> 1) & 0x1FFF) ^ computeTagFoldedGHR(bank);
    return tag & 0x1FFF;   // 13 bits
}

// ── Counter helpers ───────────────────────────────────────────────────────────

void
MyTAGEBase::satIncUnsigned(int8_t &ctr, int8_t maxVal)
{
    if (ctr < maxVal) ctr++;
}

void
MyTAGEBase::satDecUnsigned(int8_t &ctr, int8_t minVal)
{
    if (ctr > minVal) ctr--;
}

// ── myTagePredict ─────────────────────────────────────────────────────────────

bool
MyTAGEBase::myTagePredict(ThreadID tid, Addr pc,
                           TAGEBase::BranchInfo *bi_base)
{
    MyBranchInfo *bi = static_cast<MyBranchInfo *>(bi_base);

    bi->hitBank      = 0;
    bi->hitBankIndex = 0;
    bi->altBank      = 0;
    bi->altBankIndex = 0;
    bi->bimodalIndex = bindex(pc);

    if (!bi->condBranch) {
        bi->tagePred = true;
        bi->provider = BIMODAL_ONLY;
        return true;
    }

    // Scan T8..T1 for longest and second-longest tag match.
    for (int bank = nHistoryTables; bank >= 1; bank--) {
        int  idx = gindex(tid, pc, bank);
        auto tag = gtag(tid, pc, bank);

        if (gtable[bank][idx].tag == tag) {
            if (bi->hitBank == 0) {
                bi->hitBank      = bank;
                bi->hitBankIndex = idx;
            } else if (bi->altBank == 0) {
                bi->altBank      = bank;
                bi->altBankIndex = idx;
                break;   // found both; stop
            }
        }
    }

    // Provider / alternator predictions.
    if (bi->hitBank > 0) {
        bi->longestMatchPred =
            (gtable[bi->hitBank][bi->hitBankIndex].ctr >= 4);

        if (bi->altBank > 0) {
            bi->altTaken =
                (gtable[bi->altBank][bi->altBankIndex].ctr >= 4);
        } else {
            bi->altTaken = getBimodePred(pc, bi);
        }

        // Newly allocated: useful==1 AND ctr is in weak zone {3,4}.
        int8_t c = gtable[bi->hitBank][bi->hitBankIndex].ctr;
        uint8_t u = gtable[bi->hitBank][bi->hitBankIndex].u;
        bi->newlyAllocated = (u == 1) && (c == 3 || c == 4);

        // use_alternate threshold.
        if (bi->newlyAllocated && useAltOnNa >= 15) {
            bi->tagePred = bi->altTaken;
            bi->provider = (bi->altBank > 0) ?
                           TAGE_ALT_MATCH : BIMODAL_ALT_MATCH;
        } else {
            bi->tagePred = bi->longestMatchPred;
            bi->provider = TAGE_LONGEST_MATCH;
        }
    } else {
        bi->longestMatchPred = false;
        bi->altTaken         = false;
        bi->tagePred         = getBimodePred(pc, bi);
        bi->provider         = BIMODAL_ONLY;
    }

    return bi->tagePred;
}

// ── updateHistories (speculative GHR push) ────────────────────────────────────

void
MyTAGEBase::updateHistories(ThreadID tid, Addr branch_pc, bool speculative,
                             bool taken, Addr target,
                             const StaticInstPtr & /*inst*/,
                             BranchInfo *bi_base)
{
    if (!speculative) return;   // GHR is already updated speculatively.

    MyBranchInfo *bi = static_cast<MyBranchInfo *>(bi_base);

    if (bi->modified) return;   // Already recorded for this branch.

    // Save GHR snapshot before modifying.
    bi->ghrSnapshot = customGHR;
    bi->modified    = true;

    // Push 3-bit entry only for taken branches.
    if (taken) {
        pushGHREntry(branch_pc, target);
    }
}

// ── squash (misprediction recovery) ───────────────────────────────────────────

void
MyTAGEBase::squash(ThreadID /*tid*/, bool taken, Addr target,
                   const StaticInstPtr & /*inst*/, BranchInfo *bi_base)
{
    MyBranchInfo *bi = static_cast<MyBranchInfo *>(bi_base);

    // Restore GHR to the state before the mispredicted prediction.
    customGHR = bi->ghrSnapshot;

    // Push correct outcome into customGHR.
    if (taken) {
        pushGHREntry(bi->branchPC, target);
    }
}

// ── condBranchUpdate (training + allocation) ──────────────────────────────────

void
MyTAGEBase::condBranchUpdate(ThreadID tid, Addr branch_pc, bool taken,
                              BranchInfo *bi_base, int /*nrand*/,
                              Addr /*corrTarget*/, bool /*pred*/,
                              bool /*preAdjustAlloc*/)
{
    MyBranchInfo *bi = static_cast<MyBranchInfo *>(bi_base);

    // Recover alternator prediction.
    bool altPred;
    if (bi->altBank > 0) {
        altPred = (gtable[bi->altBank][bi->altBankIndex].ctr >= 4);
    } else {
        altPred = getBimodePred(branch_pc, bi);
    }

    // ── 1. use_alternate update ─────────────────────────────────────────────
    // Increment if alternator correct, decrement if alternator wrong.
    if (altPred == taken) {
        if (useAltOnNa < 31) useAltOnNa++;
    } else {
        if (useAltOnNa > 0)  useAltOnNa--;
    }

    // ── 2. Provider pred counter update ────────────────────────────────────
    if (bi->hitBank > 0) {
        int8_t &ctr = gtable[bi->hitBank][bi->hitBankIndex].ctr;
        if (taken) satIncUnsigned(ctr, 7);
        else       satDecUnsigned(ctr, 0);
    }

    // ── 3. Alternator counter adjustment ───────────────────────────────────
    if (bi->hitBank > 0 && bi->altBank > 0 &&
        bi->longestMatchPred != altPred)
    {
        int8_t &altCtr = gtable[bi->altBank][bi->altBankIndex].ctr;
        bool providerWrong = (bi->tagePred != taken);

        if (providerWrong && bi->newlyAllocated) {
            // Provider wrong + newly allocated → credit alternator.
            satIncUnsigned(altCtr, 7);
        } else if (!providerWrong) {
            // Provider right → penalise alternator.
            satDecUnsigned(altCtr, 0);
        }
    }

    // ── 4. Bimodal update when no TAGE hit ─────────────────────────────────
    if (bi->hitBank == 0) {
        baseUpdate(branch_pc, taken, bi);
        return;
    }

    // ── 5. Allocation on misprediction ─────────────────────────────────────
    if (bi->tagePred == taken) return;   // Correct prediction; no allocation.

    // Search deeper tables for a useful==0 entry.
    bool allocated = false;
    for (int bank = bi->hitBank + 1; bank <= nHistoryTables; bank++) {
        int idx = gindex(tid, branch_pc, bank);
        if (gtable[bank][idx].u == 0) {
            // Allocate here.
            gtable[bank][idx].tag = gtag(tid, branch_pc, bank);
            gtable[bank][idx].u   = 1;
            gtable[bank][idx].ctr = taken ? 4 : 3;
            allocated = true;
            break;
        }
    }

    if (!allocated) {
        // Fallback: decrement useful of all corresponding entries.
        for (int bank = bi->hitBank + 1; bank <= nHistoryTables; bank++) {
            int idx = gindex(tid, branch_pc, bank);
            if (gtable[bank][idx].u > 0)
                gtable[bank][idx].u--;
        }
    }
}

// ============================================================================
// MyTAGE
// ============================================================================

MyTAGE::MyTAGE(const MyTAGEParams &p)
    : TAGE(p)
{
    // Shadow-field fix: MyTAGEParams::tage is a new field that shadows
    // TAGEParams::tage; the TAGE(p) ctor reads the parent's (null) pointer.
    tage = p.tage;
}

bool
MyTAGE::predict(ThreadID tid, Addr pc, bool cond_branch, void* &b)
{
    TageBranchInfo *bi = new TageBranchInfo(*tage, pc, cond_branch);
    b = static_cast<void *>(bi);

    MyTAGEBase *myTage = static_cast<MyTAGEBase *>(tage);
    return myTage->myTagePredict(tid, pc, bi->tageBranchInfo);
}

void
MyTAGE::squash(ThreadID /*tid*/, void* &bp_history)
{
    assert(bp_history);
    TageBranchInfo *bi  = static_cast<TageBranchInfo *>(bp_history);
    MyTAGEBase::MyBranchInfo *mbi =
        static_cast<MyTAGEBase::MyBranchInfo *>(bi->tageBranchInfo);
    MyTAGEBase *myTage = static_cast<MyTAGEBase *>(tage);

    // Restore customGHR if this branch was speculatively recorded.
    if (mbi->modified) {
        myTage->customGHR = mbi->ghrSnapshot;
    }

    delete bi;
    bp_history = nullptr;
}

} // namespace branch_prediction
} // namespace gem5
