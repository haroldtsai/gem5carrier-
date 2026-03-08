"""
RISC-V O3 CPU SE-mode runner with configurable branch predictor.

Usage:
    ./build/RISCV/gem5.opt configs/riscv_o3_run.py \
        --cmd <binary> \
        --bp-type <predictor>

Available --bp-type values:
    TournamentBP (default), LocalBP, BiModeBP, GshareBP,
    TAGE, LTAGE,
    TAGE_SC_L_8KB, TAGE_SC_L_64KB,
    MultiperspectivePerceptron8KB, MultiperspectivePerceptron64KB,
    MultiperspectivePerceptronTage8KB, MultiperspectivePerceptronTage64KB
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
    BranchPredictor,
    GshareBP,
    TournamentBP,
    LocalBP,
    BiModeBP,
    TAGE,
    LTAGE,
    TAGE_SC_L_8KB,
    TAGE_SC_L_64KB,
    MultiperspectivePerceptron8KB,
    MultiperspectivePerceptron64KB,
    MultiperspectivePerceptronTAGE8KB,
    MultiperspectivePerceptronTAGE64KB,
    RiscvO3CPU,
    Cache,
)
from m5.util import addToPath, fatal

addToPath(os.path.join(os.path.dirname(__file__), "../"))

# ── Argument Parsing ──────────────────────────────────────────────────────────

COND_BP_MAP = {
    "TournamentBP":                      TournamentBP,
    "LocalBP":                           LocalBP,
    "BiModeBP":                          BiModeBP,
    "TAGE":                              TAGE,
    "LTAGE":                             LTAGE,
    "TAGE_SC_L_8KB":                     TAGE_SC_L_8KB,
    "TAGE_SC_L_64KB":                    TAGE_SC_L_64KB,
    "MultiperspectivePerceptron8KB":     MultiperspectivePerceptron8KB,
    "MultiperspectivePerceptron64KB":    MultiperspectivePerceptron64KB,
    "MultiperspectivePerceptronTAGE8KB": MultiperspectivePerceptronTAGE8KB,
    "MultiperspectivePerceptronTAGE64KB":MultiperspectivePerceptronTAGE64KB,
    # These have their own BranchPredictor subclass
    "GshareBP":                          None,
}

parser = argparse.ArgumentParser(description="RISC-V O3 SE-mode runner")
parser.add_argument("--cmd",      required=True, help="Binary to run")
parser.add_argument("--options",  default="",    help="Arguments for the binary")
parser.add_argument("--mem-size", default="512MB")
parser.add_argument("--bp-type",  default="TournamentBP",
                    choices=list(COND_BP_MAP.keys()),
                    help="Branch predictor type")
parser.add_argument("--num-robs", type=int, default=192)
parser.add_argument("--num-phys-int-regs", type=int, default=256)
parser.add_argument("--num-phys-fp-regs",  type=int, default=256)

args = parser.parse_args()

# ── System Setup ──────────────────────────────────────────────────────────────

system = System()
system.clk_domain     = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange(args.mem_size)]

# ── CPU ───────────────────────────────────────────────────────────────────────

system.cpu = RiscvO3CPU()
system.cpu.numROBEntries       = args.num_robs
system.cpu.numPhysIntRegs      = args.num_phys_int_regs
system.cpu.numPhysFloatRegs    = args.num_phys_fp_regs

# Branch predictor
if args.bp_type == "GshareBP":
    system.cpu.branchPred = GshareBP()
else:
    bp = BranchPredictor()
    bp.conditionalBranchPred = COND_BP_MAP[args.bp_type]()
    system.cpu.branchPred = bp

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

system.cpu.icache_port = system.cpu.icache.cpu_side
system.cpu.dcache_port = system.cpu.dcache.cpu_side

system.membus = SystemXBar()

system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.dcache.mem_side = system.membus.cpu_side_ports

system.cpu.createInterruptController()

# ── Memory ────────────────────────────────────────────────────────────────────

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8(range=system.mem_ranges[0])
system.mem_ctrl.port = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

# ── Workload ──────────────────────────────────────────────────────────────────

process = Process()
process.cmd = [args.cmd] + (args.options.split() if args.options else [])

system.workload = SEWorkload.init_compatible(args.cmd)
system.cpu.workload = process
system.cpu.createThreads()

# ── Run ───────────────────────────────────────────────────────────────────────

root = Root(full_system=False, system=system)
m5.instantiate()

print(f"\n=== gem5 RISC-V O3 | BP: {args.bp_type} | ROB: {args.num_robs} ===\n")

exit_event = m5.simulate()
print(f"\nExited: {exit_event.getCause()} @ tick {m5.curTick()}\n")
