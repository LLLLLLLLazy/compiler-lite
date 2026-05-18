#!/usr/bin/env python3
"""Execute the RISC-V64 register-allocation evaluation matrix."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from datetime import datetime
from pathlib import Path
from typing import Any

from ra_eval_common import (
    CORRECTNESS_SUITES,
    LLVM_LANES,
    RA_CONFIG_BY_NAME,
    RA_CONFIGS,
    Case,
    benchmark_suites as default_benchmark_suites,
    build_result_blob,
    discover_cases,
    normalize_text_bytes,
    parse_objdump_metrics,
    parse_total_timer_us,
    summarize_ra_stats,
    timing_summary,
    validate_ra_stats_payload,
)

CLANG_BASELINE_PREAMBLE = """
int getint(void);
int getch(void);
int getarray(int a[]);
float getfloat(void);
int getfarray(float a[]);
void putint(int a);
void putch(int a);
void putarray(int n, int a[]);
void putfloat(float a);
void putfarray(int n, float a[]);
void putf(char a[], ...);
void _sysy_starttime(int lineno);
void _sysy_stoptime(int lineno);
"""


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("correctness", "benchmark", "all"), default="all")
    parser.add_argument("--output-dir", type=Path, help="Directory for raw artifacts and records")
    parser.add_argument("--suite", action="append", help="Override suites for the selected mode(s)")
    parser.add_argument("--case", action="append", default=[], help="Case stem or simple prefix glob, e.g. 2026_perf_fft*")
    parser.add_argument("--config", action="append", choices=tuple(RA_CONFIG_BY_NAME), help="Limit the RA configs to run")
    parser.add_argument("--repeat", type=int, default=7, help="Measured benchmark runs per case/config")
    parser.add_argument("--warmup", type=int, default=1, help="Warm-up runs per case/config")
    parser.add_argument("--timeout", type=int, default=int(os.environ.get("MINIC_RA_EVAL_TIMEOUT", "120")))
    parser.add_argument("--skip-microbench", action="store_true", help="Skip diagnostic tests/ra_microbench benchmarks")
    parser.add_argument("--skip-llvm-lanes", action="store_true", help="Skip LLVM comparison lanes")
    parser.add_argument(
        "--skip-baselines",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--minic-bin", type=Path, default=Path(os.environ.get("MINIC_BIN", "build/minic")))
    parser.add_argument("--runtime-lib", type=Path, default=Path(os.environ.get("MINIC_RUNTIME_LIB", "tests/libsysy_riscv.a")))
    parser.add_argument("--clang-bin", default=os.environ.get("CLANG_BIN", "clang"))
    parser.add_argument("--llc-bin", default=os.environ.get("LLC_BIN", "llc"))
    parser.add_argument("--riscv64-gcc", default=os.environ.get("RISCV64_GCC_BIN", "riscv64-linux-gnu-gcc"))
    parser.add_argument("--objdump-bin", default=os.environ.get("RISCV64_OBJDUMP_BIN", "riscv64-linux-gnu-objdump"))
    parser.add_argument("--qemu", default=os.environ.get("QEMU_RISCV64_BIN", ""))
    parser.add_argument("--jobs", "-j", type=int, default=int(os.environ.get("MINIC_RA_EVAL_JOBS", str(os.cpu_count() or 4))), help="Number of parallel worker processes")
    return parser.parse_args()


def resolve_tool(path_or_name: str | Path, repo_root: Path) -> str:
    candidate = Path(path_or_name)
    if not candidate.is_absolute() and (repo_root / candidate).exists():
        return str((repo_root / candidate).resolve())
    return str(path_or_name)


def run_command(cmd: list[str], timeout: int, *, input_bytes: bytes | None = None) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        cmd,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )


def append_record(records_path: Path, record: dict[str, Any]) -> None:
    with records_path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")


def ensure_success(result: subprocess.CompletedProcess[bytes], phase: str, cmd: list[str]) -> None:
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"{phase} failed ({result.returncode}): {' '.join(cmd)}\n{stderr}")


def compile_ra_case(
    *,
    tools: dict[str, str],
    case: Case,
    artifact_dir: Path,
    config: dict[str, Any],
    opt_level: int,
    timeout: int,
    emit_stats: bool,
) -> dict[str, Path]:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    asm_path = artifact_dir / f"{case.name}.rv64.s"
    stats_path = artifact_dir / f"{case.name}.ra.json"
    obj_path = artifact_dir / f"{case.name}.rv64.o"
    exe_path = artifact_dir / f"{case.name}.rv64"
    cmd = [
        tools["minic"],
        "-S",
        "-A",
        f"-O{opt_level}",
        "-t",
        "RISCV64",
        *config["flags"],
    ]
    if emit_stats:
        cmd.append(f"--ra-stats-json={stats_path}")
    cmd.extend(["-o", str(asm_path), str(case.source)])
    ensure_success(run_command(cmd, timeout), "minic compile", cmd)
    assemble_cmd = [tools["gcc"], "-c", "-o", str(obj_path), str(asm_path)]
    ensure_success(run_command(assemble_cmd, timeout), "assemble", assemble_cmd)
    link_cmd = [tools["gcc"], "-static", "-o", str(exe_path), str(asm_path), tools["runtime_lib"]]
    ensure_success(run_command(link_cmd, timeout), "link", link_cmd)
    return {"asm": asm_path, "stats": stats_path, "obj": obj_path, "exe": exe_path}


def expected_matches(case: Case, stdout: bytes, exit_code: int) -> bool:
    actual = normalize_text_bytes(build_result_blob(stdout, exit_code))
    expected = normalize_text_bytes(case.expected.read_bytes())
    return actual == expected


def run_executable(tools: dict[str, str], exe_path: Path, case: Case, timeout: int) -> subprocess.CompletedProcess[bytes]:
    input_bytes = case.input_path.read_bytes() if case.input_path is not None else None
    return run_command([tools["qemu"], str(exe_path)], timeout, input_bytes=input_bytes)


def measure_text_size(obj_path: Path) -> int | None:
    candidates = [os.environ.get("RISCV64_SIZE_BIN", ""), shutil.which("riscv64-linux-gnu-size") or "", shutil.which("size") or ""]
    for tool in (candidate for candidate in candidates if candidate):
        try:
            result = subprocess.run([tool, "-A", str(obj_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        except OSError:
            continue
        if result.returncode != 0:
            continue
        for line in result.stdout.decode("utf-8", errors="replace").splitlines():
            fields = line.split()
            if len(fields) >= 2 and fields[0] == ".text":
                try:
                    return int(fields[1])
                except ValueError:
                    return None
    return None


def executable_available(path_or_name: str) -> bool:
    candidate = Path(path_or_name)
    if candidate.is_absolute() or "/" in path_or_name:
        return candidate.exists()
    return shutil.which(path_or_name) is not None


def measure_objdump_metrics(obj_path: Path, objdump_tool: str) -> dict[str, int | None]:
    if not executable_available(objdump_tool):
        return {
            "objdump_move_like_count": None,
            "objdump_stack_load_count": None,
            "objdump_stack_store_count": None,
        }
    try:
        result = subprocess.run(
            [objdump_tool, "-d", "-M", "no-aliases", str(obj_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError:
        result = None
    if result is None or result.returncode != 0:
        return {
            "objdump_move_like_count": None,
            "objdump_stack_load_count": None,
            "objdump_stack_store_count": None,
        }
    return parse_objdump_metrics(result.stdout.decode("utf-8", errors="replace"))


def measure_static_metrics(obj_path: Path, objdump_tool: str) -> dict[str, int | None]:
    return {
        "text_size_bytes": measure_text_size(obj_path),
        **measure_objdump_metrics(obj_path, objdump_tool),
    }


def correctness_suites(args: argparse.Namespace) -> tuple[str, ...]:
    return tuple(args.suite) if args.suite else CORRECTNESS_SUITES


def benchmark_suites(args: argparse.Namespace) -> tuple[str, ...]:
    if args.suite:
        return tuple(args.suite)
    return default_benchmark_suites(include_microbench=not args.skip_microbench)


def selected_configs(args: argparse.Namespace) -> list[dict[str, Any]]:
    if args.config:
        return [RA_CONFIG_BY_NAME[name] for name in args.config]
    return list(RA_CONFIGS)


def record_failure(base_record: dict[str, Any], error: Exception) -> dict[str, Any]:
    failed = dict(base_record)
    failed.update({"ok": False, "error": str(error)})
    return failed


def record_skip(base_record: dict[str, Any], reason: str) -> dict[str, Any]:
    skipped = dict(base_record)
    skipped.update({"ok": False, "skipped": True, "skip_reason": reason})
    return skipped


def _run_correctness_one(
    *,
    tools: dict[str, str],
    case: Case,
    config: dict[str, Any],
    opt_level: int,
    output_dir: Path,
    timeout: int,
) -> tuple[dict[str, Any], str]:
    """Run a single correctness test; return (record, print_line)."""
    base = {
        "record_type": "correctness",
        "suite": case.suite,
        "case": case.name,
        "config": config["name"],
        "features": list(config["features"]),
        "opt_level": opt_level,
    }
    artifact_dir = output_dir / "artifacts" / "correctness" / f"O{opt_level}" / config["name"] / case.suite / case.name
    try:
        artifacts = compile_ra_case(
            tools=tools,
            case=case,
            artifact_dir=artifact_dir,
            config=config,
            opt_level=opt_level,
            timeout=timeout,
            emit_stats=False,
        )
        run = run_executable(tools, artifacts["exe"], case, timeout)
        ok = run.returncode >= 0 and expected_matches(case, run.stdout, run.returncode)
        record = dict(base)
        record.update({"ok": ok, "exit_code": run.returncode})
        if not ok:
            record["stdout"] = run.stdout.decode("utf-8", errors="replace")
            record["stderr"] = run.stderr.decode("utf-8", errors="replace")
        line = f"[correctness] O{opt_level:<1} {config['name']:<38} {case.suite}/{case.name}: {'OK' if ok else 'NG'}"
        return (record, line)
    except Exception as error:  # noqa: BLE001
        record = record_failure(base, error)
        line = f"[correctness] O{opt_level:<1} {config['name']:<38} {case.suite}/{case.name}: NG ({error})"
        return (record, line)


def run_correctness_matrix(
    *,
    repo_root: Path,
    tools: dict[str, str],
    output_dir: Path,
    records_path: Path,
    args: argparse.Namespace,
    configs: list[dict[str, Any]],
) -> None:
    cases = discover_cases(repo_root, correctness_suites(args), args.case)
    print(f"[correctness] {len(cases)} cases x {len(configs)} configs x 2 opt levels")
    tasks = []
    for case in cases:
        for opt_level in (0, 1):
            for config in configs:
                tasks.append((case, opt_level, config))

    if args.jobs <= 1 or len(tasks) <= 1:
        for case, opt_level, config in tasks:
            record, line = _run_correctness_one(
                tools=tools, case=case, config=config, opt_level=opt_level,
                output_dir=output_dir, timeout=args.timeout,
            )
            append_record(records_path, record)
            print(line, flush=True)
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as executor:
            futures = {
                executor.submit(
                    _run_correctness_one,
                    tools=tools, case=case, config=config, opt_level=opt_level,
                    output_dir=output_dir, timeout=args.timeout,
                ): (case, opt_level, config)
                for case, opt_level, config in tasks
            }
            done = 0
            total = len(futures)
            for future in as_completed(futures):
                record, line = future.result()
                append_record(records_path, record)
                done += 1
                print(f"[{done}/{total}] {line}", flush=True)


def _benchmark_ra_config_one(
    *,
    tools: dict[str, str],
    case: Case,
    output_dir: Path,
    args: argparse.Namespace,
    config: dict[str, Any],
) -> tuple[dict[str, Any], str]:
    """Run a single RA config benchmark; return (record, print_line)."""
    base = {
        "record_type": "benchmark",
        "kind": "ra_config",
        "suite": case.suite,
        "case": case.name,
        "config": config["name"],
        "features": list(config["features"]),
        "opt_level": 1,
    }
    artifact_dir = output_dir / "artifacts" / "benchmark" / config["name"] / case.suite / case.name
    try:
        artifacts = compile_ra_case(
            tools=tools,
            case=case,
            artifact_dir=artifact_dir,
            config=config,
            opt_level=1,
            timeout=args.timeout,
            emit_stats=True,
        )
        payload = json.loads(artifacts["stats"].read_text(encoding="utf-8"))
        validate_ra_stats_payload(payload, str(artifacts["stats"]))
        warmups: list[int] = []
        timings: list[int] = []
        for index in range(args.warmup + args.repeat):
            run = run_executable(tools, artifacts["exe"], case, args.timeout)
            if run.returncode < 0 or not expected_matches(case, run.stdout, run.returncode):
                raise RuntimeError(f"benchmark output mismatch on run {index}")
            total_us = parse_total_timer_us(run.stderr.decode("utf-8", errors="replace"))
            if total_us is None:
                raise RuntimeError(f"missing TOTAL timer on run {index}")
            if index < args.warmup:
                warmups.append(total_us)
            else:
                timings.append(total_us)
        record = dict(base)
        record.update(
            {
                "ok": True,
                "warmup_timings_us": warmups,
                "timings_us": timings,
                **timing_summary(timings),
                **measure_static_metrics(artifacts["obj"], tools["objdump"]),
                "ra_stats": payload,
                "ra_summary": summarize_ra_stats(payload),
            }
        )
        line = f"[benchmark] {config['name']:<38} {case.suite}/{case.name}: median={record['median_us']:.0f}us"
        return (record, line)
    except Exception as error:  # noqa: BLE001
        record = record_failure(base, error)
        line = f"[benchmark] {config['name']:<38} {case.suite}/{case.name}: NG ({error})"
        return (record, line)


def emit_minic_ir(tools: dict[str, str], case: Case, artifact_dir: Path, timeout: int) -> Path:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    ll_path = artifact_dir / f"{case.name}.ll"
    minic_cmd = [tools["minic"], "-S", "-A", "-O1", "-L", "-o", str(ll_path), str(case.source)]
    ensure_success(run_command(minic_cmd, timeout), "minic llvm emit", minic_cmd)
    return ll_path


def compile_llvm_regalloc_lane(
    tools: dict[str, str],
    case: Case,
    artifact_dir: Path,
    timeout: int,
    regalloc: str,
) -> dict[str, Path]:
    ll_path = emit_minic_ir(tools, case, artifact_dir, timeout)
    obj_path = artifact_dir / f"{case.name}.llvm_ra_{regalloc}.o"
    exe_path = artifact_dir / f"{case.name}.llvm_ra_{regalloc}"
    llc_cmd = [
        tools["llc"],
        "-mtriple=riscv64-linux-gnu",
        "-mattr=+m,+a,+f,+d,+c",
        "-target-abi=lp64d",
        "-O2",
        f"-regalloc={regalloc}",
        "-filetype=obj",
        "-o",
        str(obj_path),
        str(ll_path),
    ]
    ensure_success(run_command(llc_cmd, timeout), f"llc {regalloc}", llc_cmd)
    link_cmd = [tools["gcc"], "-static", "-o", str(exe_path), str(obj_path), tools["runtime_lib"]]
    ensure_success(run_command(link_cmd, timeout), f"link llc {regalloc}", link_cmd)
    return {"obj": obj_path, "exe": exe_path}


def compile_same_ir_clang_lane(tools: dict[str, str], case: Case, artifact_dir: Path, timeout: int) -> dict[str, Path]:
    ll_path = emit_minic_ir(tools, case, artifact_dir, timeout)
    obj_path = artifact_dir / f"{case.name}.same_ir_clang_o2.o"
    exe_path = artifact_dir / f"{case.name}.same_ir_clang_o2"
    clang_cmd = [tools["clang"], "--target=riscv64-linux-gnu", "-O2", "-Wno-override-module", "-c", "-o", str(obj_path), str(ll_path)]
    ensure_success(run_command(clang_cmd, timeout), "clang same-ir lane", clang_cmd)
    link_cmd = [tools["gcc"], "-static", "-o", str(exe_path), str(obj_path), tools["runtime_lib"]]
    ensure_success(run_command(link_cmd, timeout), "link same-ir clang lane", link_cmd)
    return {"obj": obj_path, "exe": exe_path}


def compile_direct_clang_lane(tools: dict[str, str], case: Case, artifact_dir: Path, timeout: int) -> dict[str, Path]:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    c_path = artifact_dir / f"{case.name}.direct_clang_o2.c"
    obj_path = artifact_dir / f"{case.name}.direct_clang_o2.o"
    exe_path = artifact_dir / f"{case.name}.direct_clang_o2"
    source = case.source.read_text(encoding="utf-8", errors="replace")
    source = re.sub(r"\bstarttime\s*\(\s*\)", "_sysy_starttime(__LINE__)", source)
    source = re.sub(r"\bstoptime\s*\(\s*\)", "_sysy_stoptime(__LINE__)", source)
    c_path.write_text(CLANG_BASELINE_PREAMBLE + "\n" + source, encoding="utf-8")
    clang_cmd = [tools["clang"], "--target=riscv64-linux-gnu", "-O2", "-Wno-override-module", "-c", "-o", str(obj_path), str(c_path)]
    ensure_success(run_command(clang_cmd, timeout), "clang direct lane", clang_cmd)
    link_cmd = [tools["gcc"], "-static", "-o", str(exe_path), str(obj_path), tools["runtime_lib"]]
    ensure_success(run_command(link_cmd, timeout), "link direct clang lane", link_cmd)
    return {"obj": obj_path, "exe": exe_path}


def compile_llvm_lane(
    *,
    lane: dict[str, Any],
    tools: dict[str, str],
    case: Case,
    artifact_dir: Path,
    timeout: int,
) -> dict[str, Path]:
    if lane["kind"] == "llvm_regalloc":
        return compile_llvm_regalloc_lane(tools, case, artifact_dir, timeout, lane["regalloc"])
    if lane["kind"] == "same_ir_clang_o2":
        return compile_same_ir_clang_lane(tools, case, artifact_dir, timeout)
    if lane["kind"] == "direct_clang_o2":
        return compile_direct_clang_lane(tools, case, artifact_dir, timeout)
    raise ValueError(f"unsupported LLVM lane kind: {lane['kind']}")


def missing_lane_tools(lane: dict[str, Any], tools: dict[str, str]) -> list[str]:
    return [tool_name for tool_name in lane["requires"] if not executable_available(tools[tool_name])]


def _benchmark_llvm_lane_one(
    *,
    lane: dict[str, Any],
    tools: dict[str, str],
    case: Case,
    output_dir: Path,
    args: argparse.Namespace,
) -> tuple[dict[str, Any], str]:
    """Run a single LLVM lane benchmark; return (record, print_line)."""
    base = {
        "record_type": "benchmark",
        "kind": lane["kind"],
        "suite": case.suite,
        "case": case.name,
        "config": lane["name"],
        "features": [],
        "opt_level": 2,
    }
    missing = missing_lane_tools(lane, tools)
    if missing:
        reason = f"missing tool(s): {', '.join(missing)}"
        record = record_skip(base, reason)
        line = f"[llvm-lane] {lane['name']:<38} {case.suite}/{case.name}: SKIP ({reason})"
        return (record, line)

    artifact_dir = output_dir / "artifacts" / "benchmark" / lane["name"] / case.suite / case.name
    try:
        artifacts = compile_llvm_lane(lane=lane, tools=tools, case=case, artifact_dir=artifact_dir, timeout=args.timeout)
        warmups: list[int] = []
        timings: list[int] = []
        for index in range(args.warmup + args.repeat):
            run = run_executable(tools, artifacts["exe"], case, args.timeout)
            if run.returncode < 0 or not expected_matches(case, run.stdout, run.returncode):
                raise RuntimeError(f"lane output mismatch on run {index}")
            total_us = parse_total_timer_us(run.stderr.decode("utf-8", errors="replace"))
            if total_us is None:
                raise RuntimeError(f"missing TOTAL timer on run {index}")
            if index < args.warmup:
                warmups.append(total_us)
            else:
                timings.append(total_us)
        record = dict(base)
        record.update(
            {
                "ok": True,
                "warmup_timings_us": warmups,
                "timings_us": timings,
                **timing_summary(timings),
                **measure_static_metrics(artifacts["obj"], tools["objdump"]),
            }
        )
        line = f"[llvm-lane] {lane['name']:<38} {case.suite}/{case.name}: median={record['median_us']:.0f}us"
        return (record, line)
    except Exception as error:  # noqa: BLE001
        record = record_failure(base, error)
        line = f"[llvm-lane] {lane['name']:<38} {case.suite}/{case.name}: NG ({error})"
        return (record, line)


def run_benchmark_matrix(
    *,
    repo_root: Path,
    tools: dict[str, str],
    output_dir: Path,
    records_path: Path,
    args: argparse.Namespace,
    configs: list[dict[str, Any]],
) -> None:
    cases = discover_cases(repo_root, benchmark_suites(args), args.case)
    run_llvm_lanes = not (args.skip_llvm_lanes or args.skip_baselines)
    print(f"[benchmark] {len(cases)} cases x {len(configs)} RA configs")

    # Build task list: each task is a callable that returns (record, line)
    tasks: list[tuple[str, dict[str, Any]]] = []  # (kind, kwargs)
    for case in cases:
        for config in configs:
            tasks.append(("ra_config", {"tools": tools, "case": case, "output_dir": output_dir, "args": args, "config": config}))
        if run_llvm_lanes:
            for lane in LLVM_LANES:
                tasks.append(("llvm_lane", {"lane": lane, "tools": tools, "case": case, "output_dir": output_dir, "args": args}))

    if args.jobs <= 1 or len(tasks) <= 1:
        for kind, kwargs in tasks:
            if kind == "ra_config":
                record, line = _benchmark_ra_config_one(**kwargs)
            else:
                record, line = _benchmark_llvm_lane_one(**kwargs)
            append_record(records_path, record)
            print(line, flush=True)
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as executor:
            futures = {}
            for kind, kwargs in tasks:
                if kind == "ra_config":
                    future = executor.submit(_benchmark_ra_config_one, **kwargs)
                else:
                    future = executor.submit(_benchmark_llvm_lane_one, **kwargs)
                futures[future] = (kind, kwargs)
            done = 0
            total = len(futures)
            for future in as_completed(futures):
                record, line = future.result()
                append_record(records_path, record)
                done += 1
                print(f"[{done}/{total}] {line}", flush=True)


def main() -> int:
    args = parse_args()
    repo_root = repo_root_from_script()
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    output_dir = args.output_dir or (repo_root / "build" / "ra-eval" / timestamp)
    output_dir.mkdir(parents=True, exist_ok=True)
    records_path = output_dir / "records.jsonl"
    configs = selected_configs(args)

    qemu = args.qemu or (shutil.which("qemu-riscv64-static") or shutil.which("qemu-riscv64") or "")
    tools = {
        "minic": resolve_tool(args.minic_bin, repo_root),
        "runtime_lib": resolve_tool(args.runtime_lib, repo_root),
        "clang": args.clang_bin,
        "llc": args.llc_bin,
        "gcc": args.riscv64_gcc,
        "objdump": args.objdump_bin,
        "qemu": qemu,
    }
    if not Path(tools["minic"]).exists():
        raise SystemExit(f"minic not found: {tools['minic']}")
    if not Path(tools["runtime_lib"]).exists():
        raise SystemExit(f"runtime library not found: {tools['runtime_lib']}")
    if not tools["qemu"]:
        raise SystemExit("qemu-riscv64-static/qemu-riscv64 not found")

    print(f"Parallel jobs: {args.jobs}")

    manifest = {
        "schema_version": 1,
        "created_at": timestamp,
        "mode": args.mode,
        "repeat": args.repeat,
        "warmup": args.warmup,
        "timeout": args.timeout,
        "configs": [config["name"] for config in configs],
        "llvm_lanes": [lane["name"] for lane in LLVM_LANES],
        "llvm_lanes_enabled": not (args.skip_llvm_lanes or args.skip_baselines),
        "tools": tools,
        "correctness_suites": list(correctness_suites(args)),
        "benchmark_suites": list(benchmark_suites(args)),
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if args.mode in ("correctness", "all"):
        run_correctness_matrix(repo_root=repo_root, tools=tools, output_dir=output_dir, records_path=records_path, args=args, configs=configs)
    if args.mode in ("benchmark", "all"):
        run_benchmark_matrix(repo_root=repo_root, tools=tools, output_dir=output_dir, records_path=records_path, args=args, configs=configs)

    print(f"records: {records_path}")
    print(f"next: python3 tools/analyze_ra_eval.py {records_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
