"""
RISC-V O3 CPU SE-mode runner with detailed branch predictor configuration.

Usage:
    ./build/RISCV/gem5.opt configs/riscv_o3_run.py --cmd <binary> [options]

Branch predictor options:
    --bp-type       : TournamentBP(default), LocalBP, BiModeBP, GshareBP,
                      TAGE, LTAGE, TAGE_SC_L_8KB, TAGE_SC_L_64KB,
                      MultiperspectivePerceptron8KB/64KB,
                      MultiperspectivePerceptronTAGE8KB/64KB

BTB options:
    --btb-entries   : number of BTB entries (default: 4096)
    --btb-tag-bits  : BTB tag bits (default: 16)
    --btb-assoc     : BTB associativity (default: 1)

RAS options:
    --ras-entries   : Return Address Stack depth (default: 16)

Indirect predictor options (SimpleIndirectPredictor):
    --indirect-sets         : cache sets (default: 256)
    --indirect-ways         : ways (default: 2)
    --indirect-path-length  : path history length (default: 3)
    --indirect-ghr-bits     : GHR bits (default: 13)

TAGE/LTAGE/TAGE_SC_L options:
    --tage-history-tables   : number of history tables (default: 7)
    --tage-min-hist         : minimum history length (default: 5)
    --tage-max-hist         : maximum history length (default: 130)
    --tage-counter-bits     : tag table counter bits (default: 3)
    --tage-path-hist-bits   : path history bits (default: 16)
    --tage-u-reset-period   : log2 of useful counter reset period (default: 18)
    --tage-taken-only-hist  : use taken-only history (modern server style)

BranchPredictor wrapper options:
    --inst-shift-amt        : PC shift amount (default: 1 for RISC-V with RVC)
    --speculative-hist      : use speculative history update (default: True)
    --taken-only-history    : taken-only global history (default: False)
"""

import argparse
import sys
import os

import m5
from m5.objects import (
    System,
    SrcClockDomain,
    VoltageDomain,
    AddrRange,
    MemCtrl,
    DDR3_1600_8x8,
    SystemXBar,
    Process,
    SEWorkload,
    Root,
    # BranchPredictor wrapper
    BranchPredictor,
    SimpleBTB,
    ReturnAddrStack,
    # Conditional predictors
    GshareBP,
    TournamentBP,
    LocalBP,
    BiModeBP,
    TAGE,
    TAGEBase,
    LTAGE,
    LTAGE_TAGE,
    TAGE_SC_L_8KB,
    TAGE_SC_L_64KB,
    MultiperspectivePerceptron8KB,
    MultiperspectivePerceptron64KB,
    MultiperspectivePerceptronTAGE8KB,
    MultiperspectivePerceptronTAGE64KB,
    # Custom
    MyTAGE,
    # Indirect predictor
    SimpleIndirectPredictor,
    # Loop predictor (for LTAGE)
    LoopPredictor,
    # CPU & Cache
    RiscvO3CPU,
    Cache,
    # Replacement policy
    LRURP,
)
from m5.util import addToPath, fatal

addToPath(os.path.join(os.path.dirname(__file__), "../"))

# ── Argument Parsing ──────────────────────────────────────────────────────────

COND_BP_MAP = {
    "TournamentBP":                       TournamentBP,
    "LocalBP":                            LocalBP,
    "BiModeBP":                           BiModeBP,
    "TAGE":                               TAGE,
    "LTAGE":                              LTAGE,
    "TAGE_SC_L_8KB":                      TAGE_SC_L_8KB,
    "TAGE_SC_L_64KB":                     TAGE_SC_L_64KB,
    "MultiperspectivePerceptron8KB":      MultiperspectivePerceptron8KB,
    "MultiperspectivePerceptron64KB":     MultiperspectivePerceptron64KB,
    "MultiperspectivePerceptronTAGE8KB":  MultiperspectivePerceptronTAGE8KB,
    "MultiperspectivePerceptronTAGE64KB": MultiperspectivePerceptronTAGE64KB,
    "MyTAGE":                             MyTAGE,
    "GshareBP":                           None,  # GshareBP is BranchPredictor subclass
}

parser = argparse.ArgumentParser(description="RISC-V O3 SE-mode runner")

# Binary
parser.add_argument("--cmd",      required=True)
parser.add_argument("--options",  default="", help="Arguments for the binary")
parser.add_argument("--mem-size", default="512MB")

# CPU
parser.add_argument("--num-robs",          type=int, default=192)
parser.add_argument("--num-phys-int-regs", type=int, default=256)
parser.add_argument("--num-phys-fp-regs",  type=int, default=256)

# Branch predictor type
parser.add_argument("--bp-type", default="TournamentBP",
                    choices=list(COND_BP_MAP.keys()))

# BranchPredictor wrapper
parser.add_argument("--inst-shift-amt",     type=int,  default=1,
                    help="PC shift for RISC-V: 1=with RVC, 2=no RVC")
parser.add_argument("--speculative-hist",   type=bool, default=True)
parser.add_argument("--taken-only-history", action="store_true", default=False,
                    help="Use taken-only global history (modern server style)")

# BTB
parser.add_argument("--btb-entries",  type=int, default=4096)
parser.add_argument("--btb-tag-bits", type=int, default=16)
parser.add_argument("--btb-assoc",    type=int, default=1)

# RAS
parser.add_argument("--ras-entries",  type=int, default=16)

# Indirect predictor
parser.add_argument("--indirect-sets",        type=int, default=256)
parser.add_argument("--indirect-ways",        type=int, default=2)
parser.add_argument("--indirect-path-length", type=int, default=3)
parser.add_argument("--indirect-ghr-bits",    type=int, default=13)

# TAGE parameters (applies to TAGE / LTAGE / TAGE_SC_L)
parser.add_argument("--tage-history-tables",  type=int, default=7)
parser.add_argument("--tage-min-hist",        type=int, default=5)
parser.add_argument("--tage-max-hist",        type=int, default=130)
parser.add_argument("--tage-counter-bits",    type=int, default=3)
parser.add_argument("--tage-path-hist-bits",  type=int, default=16)
parser.add_argument("--tage-u-reset-period",  type=int, default=18)

args = parser.parse_args()

# ── Build Branch Predictor ────────────────────────────────────────────────────

def make_btb():
    return SimpleBTB(
        numEntries   = args.btb_entries,
        tagBits      = args.btb_tag_bits,
        associativity= args.btb_assoc,
        btbReplPolicy= LRURP(),
    )

def make_ras():
    return ReturnAddrStack(numEntries=args.ras_entries)

def make_indirect():
    return SimpleIndirectPredictor(
        indirectSets       = args.indirect_sets,
        indirectWays       = args.indirect_ways,
        indirectPathLength = args.indirect_path_length,
        indirectGHRBits    = args.indirect_ghr_bits,
    )

def make_tage_base():
    """Custom TAGEBase with user-specified parameters."""
    t = TAGEBase()
    t.nHistoryTables    = args.tage_history_tables
    t.minHist           = args.tage_min_hist
    t.maxHist           = args.tage_max_hist
    t.tagTableCounterBits = args.tage_counter_bits
    t.pathHistBits      = args.tage_path_hist_bits
    t.logUResetPeriod   = args.tage_u_reset_period
    return t

def make_conditional_bp():
    bp_type = args.bp_type

    if bp_type == "TAGE":
        cond = TAGE()
        cond.tage = make_tage_base()
        return cond

    elif bp_type == "LTAGE":
        cond = LTAGE()
        tage = LTAGE_TAGE()
        # Override only the params that make sense on LTAGE_TAGE
        tage.minHist          = args.tage_min_hist
        tage.maxHist          = args.tage_max_hist
        tage.pathHistBits     = args.tage_path_hist_bits
        tage.logUResetPeriod  = args.tage_u_reset_period
        cond.tage = tage
        cond.loop_predictor = LoopPredictor()
        return cond

    elif bp_type in ("TAGE_SC_L_8KB", "TAGE_SC_L_64KB"):
        # TAGE_SC_L has fixed internal structure; use defaults
        return COND_BP_MAP[bp_type]()

    else:
        return COND_BP_MAP[bp_type]()


if args.bp_type == "GshareBP":
    branch_pred = GshareBP(
        btb                = make_btb(),
        ras                = make_ras(),
        indirectBranchPred = make_indirect(),
        instShiftAmt       = args.inst_shift_amt,
        takenOnlyHistory   = args.taken_only_history,
    )
else:
    branch_pred = BranchPredictor(
        conditionalBranchPred = make_conditional_bp(),
        btb                   = make_btb(),
        ras                   = make_ras(),
        indirectBranchPred    = make_indirect(),
        instShiftAmt          = args.inst_shift_amt,
        takenOnlyHistory      = args.taken_only_history,
        speculativeHistUpdate = args.speculative_hist,
    )

# ── System Setup ──────────────────────────────────────────────────────────────

system = System()
system.clk_domain = SrcClockDomain(
    clock="1GHz",
    voltage_domain=VoltageDomain()
)
system.mem_mode   = "timing"
system.mem_ranges = [AddrRange(args.mem_size)]

# ── CPU ───────────────────────────────────────────────────────────────────────

system.cpu = RiscvO3CPU()
system.cpu.numROBEntries    = args.num_robs
system.cpu.numPhysIntRegs   = args.num_phys_int_regs
system.cpu.numPhysFloatRegs = args.num_phys_fp_regs
system.cpu.branchPred       = branch_pred

# ── Caches ────────────────────────────────────────────────────────────────────

system.cpu.icache = Cache(
    size="32kB", assoc=4,
    tag_latency=1, data_latency=1, response_latency=1,
    mshrs=4, tgts_per_mshr=8,
)
system.cpu.dcache = Cache(
    size="32kB", assoc=4,
    tag_latency=1, data_latency=1, response_latency=1,
    mshrs=4, tgts_per_mshr=8,
)

system.membus = SystemXBar()
system.cpu.icache_port = system.cpu.icache.cpu_side
system.cpu.dcache_port = system.cpu.dcache.cpu_side
system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.dcache.mem_side = system.membus.cpu_side_ports
system.cpu.createInterruptController()

# ── Memory ────────────────────────────────────────────────────────────────────

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8(range=system.mem_ranges[0])
system.mem_ctrl.port = system.membus.mem_side_ports
system.system_port   = system.membus.cpu_side_ports

# ── Workload ──────────────────────────────────────────────────────────────────

process = Process()
process.cmd = [args.cmd] + (args.options.split() if args.options else [])
system.workload      = SEWorkload.init_compatible(args.cmd)
system.cpu.workload  = process
system.cpu.createThreads()

# ── Run ───────────────────────────────────────────────────────────────────────

root = Root(full_system=False, system=system)
m5.instantiate()

print(f"\n=== gem5 RISC-V O3 | BP: {args.bp_type} | ROB: {args.num_robs} "
      f"| BTB: {args.btb_entries} entries | RAS: {args.ras_entries} ===\n")

exit_event = m5.simulate()
print(f"\nExited: {exit_event.getCause()} @ tick {m5.curTick()}\n")
