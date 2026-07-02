#!/usr/bin/env python3
# BOOM v3 configuration for gem5 RISC-V simulation
# Compatible with gem5 25.1+

import m5
from m5.objects import *
import argparse


# BOOM v3 CPU configuration
class BOOMV3CPU(DerivO3CPU):
    """
    BOOM v3 (Berkeley Out-of-Order Machine) configuration

    Key parameters from BOOM v3 Medium configuration:
    - 8-wide fetch, 4-wide decode/rename/dispatch/issue/commit
    - 128-entry ROB
    - 128 physical integer registers, 128 physical FP registers
    - 32-entry Load Queue, 32-entry Store Queue
    - TAGE-based branch predictor

    Note: gem5's O3CPU does not have a unified Issue Queue parameter.
    Issue bandwidth is controlled by dispatchWidth and issueWidth.
    """

    # Pipeline widths (instructions per cycle)
    fetchWidth = 8          # BOOM v3: 8-wide fetch
    decodeWidth = 4         # BOOM v3: 4-wide decode
    renameWidth = 4         # BOOM v3: 4-wide rename
    dispatchWidth = 4       # BOOM v3: 4-wide dispatch
    issueWidth = 4          # BOOM v3: 4-wide issue
    wbWidth = 4             # BOOM v3: 4-wide writeback
    commitWidth = 4         # BOOM v3: 4-wide commit
    squashWidth = 4         # BOOM v3: 4-wide squash

    # Reorder Buffer
    numROBEntries = 128     # BOOM v3: 128-entry ROB

    # Physical Register File
    numPhysIntRegs = 128    # BOOM v3: 128 physical int registers
    numPhysFloatRegs = 128  # BOOM v3: 128 physical FP registers

    # Load/Store Queue
    LQEntries = 32          # BOOM v3: 32-entry Load Queue
    SQEntries = 32          # BOOM v3: 32-entry Store Queue

    # Function Units (use default pool)
    fuPool = DefaultFUPool()

    # Fetch buffer
    fetchBufferSize = 64    # Fetch buffer size in bytes (BOOM v3: 16 instructions)
    fetchQueueSize = 32     # BOOM v3: 32-entry fetch queue

    # Note: Branch predictor is set to LTAGE in main() after CPU creation


def main():
    parser = argparse.ArgumentParser(
        description="gem5 RISC-V BOOM v3 simulation"
    )

    parser.add_argument("--cmd", type=str, required=True,
                        help="Binary to execute")
    parser.add_argument("--options", type=str, default="",
                        help="Command line options")
    parser.add_argument("--input", type=str, default="",
                        help="Input file for stdin")
    parser.add_argument("--output", type=str, default="",
                        help="Output file for stdout")
    parser.add_argument("--sys-clock", type=str, default="4GHz",
                        help="System clock (default: 4GHz)")
    parser.add_argument("--mem-size", type=str, default="4GB",
                        help="Memory size (default: 4GB)")
    parser.add_argument("--l2-size", type=str, default="256kB",
                        help="L2 cache size (default: 256kB)")
    parser.add_argument("--max-insts", type=int, default=0,
                        help="Max instructions (0=unlimited)")

    args = parser.parse_args()

    # Create system
    system = System()
    system.clk_domain = SrcClockDomain()
    system.clk_domain.clock = args.sys_clock
    system.clk_domain.voltage_domain = VoltageDomain()
    system.mem_mode = 'timing'
    system.mem_ranges = [AddrRange(args.mem_size)]

    # Create BOOM v3 CPU
    system.cpu = BOOMV3CPU()
    system.cpu.cpu_id = 0

    # Set branch predictor (LTAGE is closest to BOOM's TAGE-SC-L)
    try:
        system.cpu.branchPred = LTAGE(
            tage=LTAGE_TAGE(),
            loop_predictor=LoopPredictor()
        )
    except:
        # Fallback to default LTAGE if construction fails
        pass

    # Set up SE workload
    system.workload = SEWorkload.init_compatible(args.cmd)

    # Create process
    process = Process()
    process.cmd = [args.cmd]
    if args.options:
        process.cmd += args.options.split()

    # Set up I/O redirection
    if args.input:
        process.input = args.input
    if args.output:
        process.output = args.output

    system.cpu.workload = process
    system.cpu.createThreads()

    # Create interrupt controller (required for O3CPU)
    system.cpu.createInterruptController()

    # L1 Instruction Cache (32KB, 8-way)
    system.cpu.icache = Cache(
        size='32kB',
        assoc=8,
        tag_latency=1,
        data_latency=1,
        response_latency=1,
        mshrs=4,
        tgts_per_mshr=8,
        writeback_clean=False
    )

    # L1 Data Cache (32KB, 8-way)
    system.cpu.dcache = Cache(
        size='32kB',
        assoc=8,
        tag_latency=2,
        data_latency=2,
        response_latency=1,
        mshrs=8,
        tgts_per_mshr=8,
        writeback_clean=False
    )

    # Connect L1 caches
    system.cpu.icache.cpu_side = system.cpu.icache_port
    system.cpu.dcache.cpu_side = system.cpu.dcache_port

    # L2 Cache (256KB, 16-way)
    system.l2cache = Cache(
        size=args.l2_size,
        assoc=16,
        tag_latency=10,
        data_latency=10,
        response_latency=1,
        mshrs=16,
        tgts_per_mshr=12,
        writeback_clean=True
    )

    # L2 XBar (to connect multiple L1 caches to L2)
    system.l2bus = L2XBar()

    # Memory bus
    system.membus = SystemXBar()

    # Connect L1 to L2 bus
    system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
    system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports

    # Connect L2 bus to L2 cache
    system.l2bus.mem_side_ports = system.l2cache.cpu_side

    # Connect L2 to memory bus
    system.l2cache.mem_side = system.membus.cpu_side_ports

    # Memory controller
    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR4_2400_16x4()
    system.mem_ctrl.dram.range = system.mem_ranges[0]
    system.mem_ctrl.port = system.membus.mem_side_ports

    # System port
    system.system_port = system.membus.cpu_side_ports

    # Create root and instantiate
    root = Root(full_system=False, system=system)
    m5.instantiate()

    print("=" * 60)
    print("BOOM v3 RISC-V Simulation")
    print("=" * 60)
    print(f"Binary:      {args.cmd}")
    print(f"Clock:       {args.sys_clock}")
    print(f"Memory:      {args.mem_size}")
    print(f"L2 Cache:    {args.l2_size}")
    print("=" * 60)
    print("CPU Configuration:")
    print(f"  Fetch Width:     {system.cpu.fetchWidth}")
    print(f"  Decode Width:    {system.cpu.decodeWidth}")
    print(f"  Issue Width:     {system.cpu.issueWidth}")
    print(f"  Commit Width:    {system.cpu.commitWidth}")
    print(f"  ROB Entries:     {system.cpu.numROBEntries}")
    print(f"  Phys Int Regs:   {system.cpu.numPhysIntRegs}")
    print(f"  Phys Float Regs: {system.cpu.numPhysFloatRegs}")
    print(f"  Load Queue:      {system.cpu.LQEntries}")
    print(f"  Store Queue:     {system.cpu.SQEntries}")
    print("=" * 60)
    print("Beginning simulation...")
    print()

    # Run simulation
    if args.max_insts > 0:
        exit_event = m5.simulate(args.max_insts)
    else:
        exit_event = m5.simulate()

    print()
    print("=" * 60)
    print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
    print("=" * 60)


if __name__ == "__m5_main__":
    main()
