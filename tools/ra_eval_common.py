#!/usr/bin/env python3
"""Shared helpers for the RISC-V64 register-allocation evaluation tools."""

from __future__ import annotations

import math
import re
import statistics
from fnmatch import fnmatch
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

SCHEMA_VERSION = 1

RA_CONFIGS = (
    {
        "name": "none",
        "features": (),
        "flags": (
            "--ra-no-callee-saved-fpr",
            "--ra-no-coalesce",
            "--ra-no-split",
        ),
    },
    {
        "name": "callee_saved_fpr",
        "features": ("callee_saved_fpr",),
        "flags": (
            "--ra-callee-saved-fpr",
            "--ra-no-coalesce",
            "--ra-no-split",
        ),
    },
    {
        "name": "coalesce",
        "features": ("coalesce",),
        "flags": (
            "--ra-no-callee-saved-fpr",
            "--ra-coalesce",
            "--ra-no-split",
        ),
    },
    {
        "name": "split",
        "features": ("split",),
        "flags": (
            "--ra-no-callee-saved-fpr",
            "--ra-no-coalesce",
            "--ra-split",
        ),
    },
    {
        "name": "callee_saved_fpr+coalesce",
        "features": ("callee_saved_fpr", "coalesce"),
        "flags": (
            "--ra-callee-saved-fpr",
            "--ra-coalesce",
            "--ra-no-split",
        ),
    },
    {
        "name": "callee_saved_fpr+split",
        "features": ("callee_saved_fpr", "split"),
        "flags": (
            "--ra-callee-saved-fpr",
            "--ra-no-coalesce",
            "--ra-split",
        ),
    },
    {
        "name": "coalesce+split",
        "features": ("coalesce", "split"),
        "flags": (
            "--ra-no-callee-saved-fpr",
            "--ra-coalesce",
            "--ra-split",
        ),
    },
    {
        "name": "callee_saved_fpr+coalesce+split",
        "features": ("callee_saved_fpr", "coalesce", "split"),
        "flags": (
            "--ra-callee-saved-fpr",
            "--ra-coalesce",
            "--ra-split",
        ),
    },
)
RA_CONFIG_BY_NAME = {config["name"]: config for config in RA_CONFIGS}

CORRECTNESS_SUITES = (
    "2023_function",
    "2025_function",
    "2026_function",
    "phi_regression",
    "float_regression",
    "riscv64_regression",
    "ra_microbench",
)
PERFORMANCE_SUITES = ("2025_performance", "2026_performance")
MICROBENCH_SUITE = "ra_microbench"
DIAGNOSTIC_SUITES = (MICROBENCH_SUITE,)

LLVM_LANES = (
    {"name": "llvm_ra_fast", "kind": "llvm_regalloc", "regalloc": "fast", "requires": ("llc",)},
    {"name": "llvm_ra_basic", "kind": "llvm_regalloc", "regalloc": "basic", "requires": ("llc",)},
    {"name": "llvm_ra_greedy", "kind": "llvm_regalloc", "regalloc": "greedy", "requires": ("llc",)},
    {"name": "same_ir_clang_o2", "kind": "same_ir_clang_o2", "requires": ("clang",)},
    {"name": "direct_clang_o2", "kind": "direct_clang_o2", "requires": ("clang",)},
)
LLVM_LANE_BY_NAME = {lane["name"]: lane for lane in LLVM_LANES}

MICROBENCH_HYPOTHESES = {
    "control_low_pressure": "低压力控制组应基本不 spill",
    "gpr_pressure_hot": "热循环 GPR 压力应触发整数寄存器竞争",
    "fpr_pressure_hot": "热循环 FPR 压力应触发浮点寄存器竞争",
    "cross_call_float": "跨调用 float 值应暴露 callee-saved FPR 价值",
    "long_live_across_call": "跨调用长活跃值应让 split lane 真正触发",
    "phi_copy_dense": "phi/copy 密集路径应暴露 copy 合并效果",
    "copy_chain_no_pressure": "低压力 copy 链应保留必要置换且不引入 spill",
    "loop_carried_copy_no_pressure": "循环携带更新用于观察低压力主路径",
    "branch_phi_copy_no_pressure": "热循环中的无环分支 phi copy 应被 coalesce 消除",
    "mixed_pressure_hot": "混合整数/浮点热循环应同时覆盖两类寄存器压力",
}

RA_FUNCTION_METRICS = (
    "assigned_reg_intervals",
    "assigned_gpr_intervals",
    "assigned_fpr_intervals",
    "spilled_intervals",
    "spilled_values",
    "estimated_reloads",
    "estimated_spill_stores",
    "eliminated_copies",
    "split_count",
    "frame_size",
    "machine_instruction_count",
    "stack_load_count",
    "stack_store_count",
    "move_instruction_count",
)
RA_LIST_METRICS = ("used_callee_saved_gpr", "used_callee_saved_fpr")

TOTAL_TIMER_RE = re.compile(r"TOTAL:\s*(\d+)H-(\d+)M-(\d+)S-(\d+)us")
OBJDUMP_INST_RE = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{2,8}\s+)+([.A-Za-z0-9_]+)\s*(.*?)\s*$"
)

MOVE_LIKE_OPCODES = {"c.mv", "mv", "fmv.s"}
STACK_LOAD_OPCODES = {
    "lb",
    "lbu",
    "lh",
    "lhu",
    "lw",
    "lwu",
    "ld",
    "flw",
    "fld",
    "c.lw",
    "c.ld",
    "c.flw",
    "c.fld",
    "c.lwsp",
    "c.ldsp",
    "c.flwsp",
    "c.fldsp",
}
STACK_STORE_OPCODES = {
    "sb",
    "sh",
    "sw",
    "sd",
    "fsw",
    "fsd",
    "c.sw",
    "c.sd",
    "c.fsw",
    "c.fsd",
    "c.swsp",
    "c.sdsp",
    "c.fswsp",
    "c.fsdsp",
}


class RAStatsValidationError(ValueError):
    """Raised when a backend RA stats payload does not match the expected schema."""


@dataclass(frozen=True)
class Case:
    suite: str
    name: str
    source: Path
    expected: Path
    input_path: Path | None


def discover_cases(repo_root: Path, suite_names: Iterable[str], case_patterns: Iterable[str] = ()) -> list[Case]:
    patterns = tuple(case_patterns)
    cases: list[Case] = []
    for suite in suite_names:
        suite_dir = repo_root / "tests" / suite
        if not suite_dir.is_dir():
            continue
        for source in sorted((*suite_dir.glob("*.c"), *suite_dir.glob("*.sy"))):
            if patterns and not any(fnmatch(source.stem, pattern) for pattern in patterns):
                continue
            expected = source.with_suffix(".out")
            if not expected.exists():
                continue
            input_path = source.with_suffix(".in")
            cases.append(Case(suite, source.stem, source, expected, input_path if input_path.exists() else None))
    return cases


def normalize_text_bytes(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def build_result_blob(stdout: bytes, exit_code: int) -> bytes:
    payload = stdout
    if payload and not payload.endswith(b"\n"):
        payload += b"\n"
    payload += f"{exit_code}\n".encode()
    return payload


def parse_total_timer_us(stderr: str) -> int | None:
    match = TOTAL_TIMER_RE.search(stderr)
    if match is None:
        return None
    hours, minutes, seconds, micros = (int(part) for part in match.groups())
    return (((hours * 60) + minutes) * 60 + seconds) * 1_000_000 + micros


def timing_summary(samples: list[int]) -> dict[str, float]:
    if not samples:
        raise ValueError("timing samples cannot be empty")
    ordered = sorted(samples)
    median = statistics.median(ordered)
    deviations = [abs(value - median) for value in ordered]
    mad = statistics.median(deviations)
    quartiles = statistics.quantiles(ordered, n=4, method="inclusive") if len(ordered) > 1 else [ordered[0]] * 3
    return {
        "median_us": float(median),
        "mad_us": float(mad),
        "iqr_us": float(quartiles[2] - quartiles[0]),
    }


def geometric_mean(values: Iterable[float]) -> float:
    usable = [value for value in values if value > 0]
    if not usable:
        raise ValueError("geometric mean requires at least one positive value")
    return math.exp(sum(math.log(value) for value in usable) / len(usable))


def benchmark_suites(include_microbench: bool = True) -> tuple[str, ...]:
    suites = list(PERFORMANCE_SUITES)
    if include_microbench:
        suites.extend(DIAGNOSTIC_SUITES)
    return tuple(suites)


def is_microbench_suite(suite: str) -> bool:
    return suite == MICROBENCH_SUITE


def parse_objdump_metrics(disassembly: str) -> dict[str, int]:
    """Parse cross-backend proxy metrics from `objdump -d -M no-aliases` text."""

    move_like = 0
    stack_loads = 0
    stack_stores = 0

    for line in disassembly.splitlines():
        match = OBJDUMP_INST_RE.match(line)
        if match is None:
            continue
        opcode = match.group(1).lower()
        operands = match.group(2).strip().lower()

        if opcode in MOVE_LIKE_OPCODES:
            move_like += 1
        elif opcode in {"addi", "addiw"} and operands.endswith(",0"):
            move_like += 1
        elif opcode == "fsgnj.s":
            parts = [part.strip() for part in operands.split(",")]
            if len(parts) == 3 and parts[1] == parts[2]:
                move_like += 1

        mentions_stack_base = "(sp)" in operands or "(s0)" in operands
        if opcode in STACK_LOAD_OPCODES and (mentions_stack_base or opcode.endswith("sp")):
            stack_loads += 1
        elif opcode in STACK_STORE_OPCODES and (mentions_stack_base or opcode.endswith("sp")):
            stack_stores += 1

    return {
        "objdump_move_like_count": move_like,
        "objdump_stack_load_count": stack_loads,
        "objdump_stack_store_count": stack_stores,
    }


def validate_ra_stats_payload(payload: dict[str, Any], context: str = "ra_stats") -> None:
    if payload.get("schema_version") != SCHEMA_VERSION:
        raise RAStatsValidationError(f"{context}: unsupported schema_version={payload.get('schema_version')!r}")
    if payload.get("target") != "RISCV64":
        raise RAStatsValidationError(f"{context}: expected target='RISCV64'")
    config = payload.get("config")
    if not isinstance(config, dict):
        raise RAStatsValidationError(f"{context}: missing config object")
    for key in ("callee_saved_fpr", "coalesce", "split"):
        if not isinstance(config.get(key), bool):
            raise RAStatsValidationError(f"{context}: config.{key} must be boolean")
    functions = payload.get("functions")
    if not isinstance(functions, list):
        raise RAStatsValidationError(f"{context}: functions must be an array")
    for index, function in enumerate(functions):
        prefix = f"{context}.functions[{index}]"
        if not isinstance(function, dict):
            raise RAStatsValidationError(f"{prefix}: expected object")
        if not isinstance(function.get("name"), str):
            raise RAStatsValidationError(f"{prefix}.name: expected string")
        for key in RA_FUNCTION_METRICS:
            if not isinstance(function.get(key), int):
                raise RAStatsValidationError(f"{prefix}.{key}: expected integer")
        for key in RA_LIST_METRICS:
            value = function.get(key)
            if not isinstance(value, list) or not all(isinstance(item, int) for item in value):
                raise RAStatsValidationError(f"{prefix}.{key}: expected integer array")


def summarize_ra_stats(payload: dict[str, Any]) -> dict[str, int]:
    validate_ra_stats_payload(payload)
    summary = {key: 0 for key in RA_FUNCTION_METRICS}
    summary["used_callee_saved_gpr_count"] = 0
    summary["used_callee_saved_fpr_count"] = 0
    for function in payload["functions"]:
        for key in RA_FUNCTION_METRICS:
            summary[key] += function[key]
        summary["used_callee_saved_gpr_count"] += len(function["used_callee_saved_gpr"])
        summary["used_callee_saved_fpr_count"] += len(function["used_callee_saved_fpr"])
    return summary
