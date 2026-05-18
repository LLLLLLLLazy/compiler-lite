#!/usr/bin/env python3
"""Analyze raw register-allocation evaluation records."""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Any

from ra_eval_common import (
    LLVM_LANES,
    MICROBENCH_HYPOTHESES,
    MICROBENCH_SUITE,
    RA_CONFIGS,
    geometric_mean,
    is_microbench_suite,
    summarize_ra_stats,
    validate_ra_stats_payload,
)

LLVM_REGALLOC_CONFIGS = ("llvm_ra_fast", "llvm_ra_basic", "llvm_ra_greedy")
BACKEND_GAP_CONFIGS = (*LLVM_REGALLOC_CONFIGS, "same_ir_clang_o2", "direct_clang_o2")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("records", type=Path, help="records.jsonl emitted by run_ra_eval.py")
    parser.add_argument("--output-dir", type=Path, help="Directory for CSV/Markdown reports (defaults beside records)")
    return parser.parse_args()


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{lineno}: invalid JSON: {error}") from error
    return records


def collect_benchmark_rows(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    latest: dict[tuple[str, str, str, str], dict[str, Any]] = {}
    for record in records:
        if record.get("record_type") != "benchmark" or not record.get("ok"):
            continue
        key = (record["kind"], record["suite"], record["case"], record["config"])
        latest[key] = record
    rows = list(latest.values())
    for row in rows:
        if row["kind"] == "ra_config":
            validate_ra_stats_payload(row["ra_stats"], f"{row['suite']}/{row['case']}/{row['config']}")
            row["ra_summary"] = summarize_ra_stats(row["ra_stats"])
    return rows


def collect_skipped_benchmark_rows(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    latest: dict[tuple[str, str, str, str], dict[str, Any]] = {}
    for record in records:
        if record.get("record_type") != "benchmark" or not record.get("skipped"):
            continue
        key = (record["kind"], record["suite"], record["case"], record["config"])
        latest[key] = record
    return sorted(latest.values(), key=lambda row: (row["config"], row["suite"], row["case"]))


def build_normalized_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_case: dict[tuple[str, str], dict[str, dict[str, Any]]] = defaultdict(dict)
    for row in rows:
        by_case[(row["suite"], row["case"])][row["config"]] = row

    normalized: list[dict[str, Any]] = []
    for (suite, case), case_rows in sorted(by_case.items()):
        baseline = case_rows.get("none")
        if baseline is None:
            continue
        baseline_median = baseline["median_us"]
        for config in RA_CONFIGS:
            row = case_rows.get(config["name"])
            if row is None:
                continue
            item = dict(row)
            item["normalized_runtime"] = row["median_us"] / baseline_median
            item["baseline_summary"] = baseline["ra_summary"]
            normalized.append(item)
    return normalized


def config_summary(normalized: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_config: dict[str, list[dict[str, Any]]] = defaultdict(list)
    suites = sorted({row["suite"] for row in normalized})
    for row in normalized:
        by_config[row["config"]].append(row)

    summaries: list[dict[str, Any]] = []
    for config in RA_CONFIGS:
        rows = by_config.get(config["name"], [])
        if not rows:
            continue
        ratios = [row["normalized_runtime"] for row in rows]
        summary: dict[str, Any] = {
            "config": config["name"],
            "features": "+".join(config["features"]) or "none",
            "overall_geomean": geometric_mean(ratios),
            "wins": sum(ratio < 1.0 for ratio in ratios),
            "ties": sum(math.isclose(ratio, 1.0, rel_tol=1e-9, abs_tol=1e-12) for ratio in ratios),
            "losses": sum(ratio > 1.0 for ratio in ratios),
        }
        for suite in suites:
            suite_ratios = [row["normalized_runtime"] for row in rows if row["suite"] == suite]
            summary[f"geomean_{suite}"] = geometric_mean(suite_ratios) if suite_ratios else None
        for metric in (
            "spilled_values",
            "estimated_reloads",
            "estimated_spill_stores",
            "eliminated_copies",
            "split_count",
            "machine_instruction_count",
            "move_instruction_count",
        ):
            deltas = [row["ra_summary"][metric] - row["baseline_summary"][metric] for row in rows]
            summary[f"mean_delta_{metric}"] = sum(deltas) / len(deltas)
        summaries.append(summary)
    return sorted(summaries, key=lambda item: item["overall_geomean"])


def runtime_index(rows: list[dict[str, Any]]) -> dict[tuple[str, str], dict[str, dict[str, Any]]]:
    by_case: dict[tuple[str, str], dict[str, dict[str, Any]]] = defaultdict(dict)
    for row in rows:
        by_case[(row["suite"], row["case"])][row["config"]] = row
    return by_case


def safe_ratio(numerator: dict[str, Any] | None, denominator: dict[str, Any] | None) -> float | None:
    if numerator is None or denominator is None:
        return None
    denominator_median = denominator.get("median_us")
    if not denominator_median:
        return None
    return numerator["median_us"] / denominator_median


def backend_gap_rows(rows: list[dict[str, Any]], *, microbench: bool | None = None) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for (suite, case), case_rows in sorted(runtime_index(rows).items()):
        if microbench is not None and is_microbench_suite(suite) != microbench:
            continue
        own_none = case_rows.get("none")
        if own_none is None:
            continue
        own_candidates = [case_rows[config["name"]] for config in RA_CONFIGS if config["name"] in case_rows]
        best_own = min(own_candidates, key=lambda row: row["median_us"]) if own_candidates else None
        llvm_fast = case_rows.get("llvm_ra_fast")
        llvm_basic = case_rows.get("llvm_ra_basic")
        llvm_greedy = case_rows.get("llvm_ra_greedy")
        same_ir = case_rows.get("same_ir_clang_o2")
        direct_clang = case_rows.get("direct_clang_o2")
        row = {
            "suite": suite,
            "case": case,
            "own_none_us": own_none["median_us"],
            "best_own_config": best_own["config"] if best_own is not None else None,
            "best_own_us": best_own["median_us"] if best_own is not None else None,
            "llvm_ra_fast_us": llvm_fast["median_us"] if llvm_fast is not None else None,
            "llvm_ra_basic_us": llvm_basic["median_us"] if llvm_basic is not None else None,
            "llvm_ra_greedy_us": llvm_greedy["median_us"] if llvm_greedy is not None else None,
            "same_ir_clang_o2_us": same_ir["median_us"] if same_ir is not None else None,
            "direct_clang_o2_us": direct_clang["median_us"] if direct_clang is not None else None,
            "own_ra_effect": safe_ratio(best_own, own_none),
            "llvm_ra_spread": safe_ratio(llvm_fast, llvm_greedy),
            "backend_gap": safe_ratio(llvm_greedy, own_none),
            "extra_llvm_opt_gap": safe_ratio(same_ir, llvm_greedy),
            "whole_compiler_gap": safe_ratio(direct_clang, own_none),
            "complete": all(case_rows.get(config) is not None for config in BACKEND_GAP_CONFIGS),
        }
        result.append(row)
    return result


def aggregate_ratio(rows: list[dict[str, Any]], key: str) -> float | None:
    values = [row[key] for row in rows if isinstance(row.get(key), (float, int)) and row[key] is not None]
    return geometric_mean(values) if values else None


def llvm_regalloc_summary(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_case = runtime_index([row for row in rows if not is_microbench_suite(row["suite"])])
    result: list[dict[str, Any]] = []
    for config in LLVM_REGALLOC_CONFIGS:
        ratios_vs_greedy: list[float] = []
        ratios_vs_own_none: list[float] = []
        cases = 0
        for case_rows in by_case.values():
            lane = case_rows.get(config)
            greedy = case_rows.get("llvm_ra_greedy")
            own_none = case_rows.get("none")
            if lane is None:
                continue
            cases += 1
            if greedy is not None:
                ratios_vs_greedy.append(lane["median_us"] / greedy["median_us"])
            if own_none is not None:
                ratios_vs_own_none.append(lane["median_us"] / own_none["median_us"])
        if cases == 0:
            continue
        result.append(
            {
                "config": config,
                "geomean_vs_llvm_ra_greedy": geometric_mean(ratios_vs_greedy) if ratios_vs_greedy else None,
                "geomean_vs_own_none": geometric_mean(ratios_vs_own_none) if ratios_vs_own_none else None,
                "cases": cases,
            }
        )
    return result


def case_summary_rows(normalized: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for row in sorted(normalized, key=lambda item: (item["suite"], item["case"], item["config"])):
        summary = row["ra_summary"]
        rows.append(
            {
                "suite": row["suite"],
                "case": row["case"],
                "config": row["config"],
                "median_us": row["median_us"],
                "mad_us": row["mad_us"],
                "iqr_us": row["iqr_us"],
                "normalized_runtime": row["normalized_runtime"],
                "text_size_bytes": row.get("text_size_bytes"),
                "objdump_move_like_count": row.get("objdump_move_like_count"),
                "objdump_stack_load_count": row.get("objdump_stack_load_count"),
                "objdump_stack_store_count": row.get("objdump_stack_store_count"),
                **summary,
            }
        )
    return rows


def external_baseline_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    ra_by_case = {(row["suite"], row["case"]): row for row in rows if row["config"] == "none"}
    result: list[dict[str, Any]] = []
    for row in rows:
        if row["kind"] == "ra_config":
            continue
        baseline = ra_by_case.get((row["suite"], row["case"]))
        if baseline is None:
            continue
        result.append(
            {
                "suite": row["suite"],
                "case": row["case"],
                "baseline": row["config"],
                "median_us": row["median_us"],
                "normalized_to_ra_none": row["median_us"] / baseline["median_us"],
                "text_size_bytes": row.get("text_size_bytes"),
                "objdump_move_like_count": row.get("objdump_move_like_count"),
                "objdump_stack_load_count": row.get("objdump_stack_load_count"),
                "objdump_stack_store_count": row.get("objdump_stack_store_count"),
            }
        )
    return sorted(result, key=lambda item: (item["baseline"], item["suite"], item["case"]))


def microbench_signal(case: str, case_rows: dict[str, dict[str, Any]]) -> tuple[bool | None, str]:
    own_none = case_rows.get("none")
    if own_none is None:
        return None, "missing own:none"
    own_summary = own_none["ra_summary"]
    split_summary = case_rows.get("split", {}).get("ra_summary")
    coalesce_summary = case_rows.get("coalesce", {}).get("ra_summary")

    if case == "control_low_pressure":
        ok = own_summary["spilled_values"] == 0
        return ok, f"spilled_values={own_summary['spilled_values']}"
    if case == "gpr_pressure_hot":
        ok = own_summary["assigned_gpr_intervals"] > 0 and own_summary["spilled_values"] > 0
        return ok, f"gpr={own_summary['assigned_gpr_intervals']}, spilled={own_summary['spilled_values']}"
    if case == "fpr_pressure_hot":
        ok = own_summary["assigned_fpr_intervals"] > 0 and own_summary["spilled_values"] > 0
        return ok, f"fpr={own_summary['assigned_fpr_intervals']}, spilled={own_summary['spilled_values']}"
    if case == "cross_call_float":
        ok = own_summary["assigned_fpr_intervals"] > 0 and own_summary["spilled_values"] > 0
        return ok, f"fpr={own_summary['assigned_fpr_intervals']}, spilled={own_summary['spilled_values']}"
    if case == "long_live_across_call":
        if split_summary is None:
            return None, "missing split lane"
        ok = split_summary["split_count"] > 0
        return ok, f"split_count={split_summary['split_count']}"
    if case == "branch_phi_copy_no_pressure":
        if coalesce_summary is None:
            return None, "missing coalesce lane"
        ok = (
            coalesce_summary["eliminated_copies"] > 0
            and coalesce_summary["move_instruction_count"] < own_summary["move_instruction_count"]
        )
        return ok, (
            f"baseline_moves={own_summary['move_instruction_count']}, "
            f"coalesce_moves={coalesce_summary['move_instruction_count']}, "
            f"coalesce_eliminated={coalesce_summary['eliminated_copies']}"
        )
    if case == "loop_carried_copy_no_pressure":
        ok = own_summary["spilled_values"] == 0
        return ok, (
            f"baseline_moves={own_summary['move_instruction_count']}, "
            f"spilled_values={own_summary['spilled_values']}"
        )
    if case == "phi_copy_dense":
        if coalesce_summary is None:
            return None, "missing coalesce lane"
        ok = coalesce_summary["eliminated_copies"] > 0
        return ok, (
            f"baseline_moves={own_summary['move_instruction_count']}, "
            f"coalesce_moves={coalesce_summary['move_instruction_count']}, "
            f"coalesce_eliminated={coalesce_summary['eliminated_copies']}"
        )
    if case == "copy_chain_no_pressure":
        ok = own_summary["spilled_values"] == 0 and own_summary["move_instruction_count"] > 0
        return ok, (
            f"baseline_moves={own_summary['move_instruction_count']}, "
            f"spilled_values={own_summary['spilled_values']}"
        )
    if case == "mixed_pressure_hot":
        ok = own_summary["assigned_gpr_intervals"] > 0 and own_summary["assigned_fpr_intervals"] > 0
        return ok, (
            f"gpr={own_summary['assigned_gpr_intervals']}, "
            f"fpr={own_summary['assigned_fpr_intervals']}, spilled={own_summary['spilled_values']}"
        )
    return None, "no expectation registered"


def microbench_summary_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    gaps = {(row["suite"], row["case"]): row for row in backend_gap_rows(rows, microbench=True)}
    by_case = runtime_index(rows)
    result: list[dict[str, Any]] = []
    for (suite, case), gap in sorted(gaps.items()):
        case_rows = by_case[(suite, case)]
        own_none = case_rows.get("none")
        coalesce = case_rows.get("coalesce")
        split = case_rows.get("split")
        signal_ok, signal_detail = microbench_signal(case, case_rows)
        result.append(
            {
                **gap,
                "hypothesis": MICROBENCH_HYPOTHESES.get(case, ""),
                "signal_ok": signal_ok,
                "signal_detail": signal_detail,
                "own_none_spilled_values": own_none["ra_summary"]["spilled_values"] if own_none is not None else None,
                "own_none_assigned_gpr_intervals": own_none["ra_summary"]["assigned_gpr_intervals"] if own_none is not None else None,
                "own_none_assigned_fpr_intervals": own_none["ra_summary"]["assigned_fpr_intervals"] if own_none is not None else None,
                "coalesce_eliminated_copies": coalesce["ra_summary"]["eliminated_copies"] if coalesce is not None else None,
                "split_count": split["ra_summary"]["split_count"] if split is not None else None,
            }
        )
    return result


def strongest_interactions(normalized: list[dict[str, Any]], limit: int = 10) -> list[dict[str, Any]]:
    by_case: dict[tuple[str, str], dict[str, dict[str, Any]]] = defaultdict(dict)
    for row in normalized:
        by_case[(row["suite"], row["case"])][row["config"]] = row
    interactions: list[dict[str, Any]] = []
    singles = {"callee_saved_fpr", "coalesce", "split"}
    for (suite, case), rows in by_case.items():
        if not singles.issubset(rows):
            continue
        single_ratios = {name: rows[name]["normalized_runtime"] for name in singles}
        for config in RA_CONFIGS:
            if len(config["features"]) < 2 or config["name"] not in rows:
                continue
            expected = math.prod(single_ratios[feature] for feature in config["features"])
            actual = rows[config["name"]]["normalized_runtime"]
            factor = actual / expected if expected else math.inf
            interactions.append(
                {
                    "suite": suite,
                    "case": case,
                    "config": config["name"],
                    "actual_ratio": actual,
                    "expected_ratio": expected,
                    "interaction_factor": factor,
                    "score": abs(math.log(factor)) if factor > 0 else math.inf,
                }
            )
    return sorted(interactions, key=lambda item: item["score"], reverse=True)[:limit]


def worst_regressions(normalized: list[dict[str, Any]], limit: int = 10) -> list[dict[str, Any]]:
    rows = [row for row in normalized if row["config"] != "none" and row["normalized_runtime"] > 1.0]
    rows.sort(key=lambda row: row["normalized_runtime"], reverse=True)
    result: list[dict[str, Any]] = []
    for row in rows[:limit]:
        result.append(
            {
                "suite": row["suite"],
                "case": row["case"],
                "config": row["config"],
                "normalized_runtime": row["normalized_runtime"],
                "delta_spilled_values": row["ra_summary"]["spilled_values"] - row["baseline_summary"]["spilled_values"],
                "delta_reloads": row["ra_summary"]["estimated_reloads"] - row["baseline_summary"]["estimated_reloads"],
                "delta_moves": row["ra_summary"]["move_instruction_count"] - row["baseline_summary"]["move_instruction_count"],
                "delta_machine_insts": row["ra_summary"]["machine_instruction_count"] - row["baseline_summary"]["machine_instruction_count"],
            }
        )
    return result


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        path.write_text("\n", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def markdown_table(headers: list[str], rows: list[list[Any]]) -> str:
    def render(value: Any) -> str:
        if isinstance(value, float):
            return f"{value:.4f}"
        return str(value)

    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join(["---"] * len(headers)) + " |"]
    lines.extend("| " + " | ".join(render(value) for value in row) + " |" for row in rows)
    return "\n".join(lines)


def write_markdown_report(
    path: Path,
    summaries: list[dict[str, Any]],
    llvm_rows: list[dict[str, Any]],
    gap_rows: list[dict[str, Any]],
    microbench_rows: list[dict[str, Any]],
    interactions: list[dict[str, Any]],
    regressions: list[dict[str, Any]],
    skipped_rows: list[dict[str, Any]],
) -> None:
    suites = sorted(key.removeprefix("geomean_") for key in summaries[0] if key.startswith("geomean_")) if summaries else []
    content: list[str] = ["# Register Allocation Evaluation Report", ""]
    if summaries:
        headers = ["config", "overall", *suites, "wins", "losses"]
        rows = [
            [summary["config"], summary["overall_geomean"], *[summary.get(f"geomean_{suite}") for suite in suites], summary["wins"], summary["losses"]]
            for summary in summaries
        ]
        content.extend(["## Own RA Runtime Ranking", "", markdown_table(headers, rows), ""])
        single_names = {"coalesce", "split", "callee_saved_fpr"}
        single_rows = [summary for summary in summaries if summary["config"] in single_names]
        content.extend(
            [
                "## Own RA Ablation",
                "",
                markdown_table(
                    ["feature", "overall", "Δspilled_values", "Δreloads", "Δcopies", "Δmoves"],
                    [
                        [
                            row["config"],
                            row["overall_geomean"],
                            row["mean_delta_spilled_values"],
                            row["mean_delta_estimated_reloads"],
                            row["mean_delta_eliminated_copies"],
                            row["mean_delta_move_instruction_count"],
                        ]
                        for row in single_rows
                    ],
                ),
                "",
            ]
        )
    if llvm_rows:
        content.extend(
            [
                "## LLVM RA Sensitivity",
                "",
                markdown_table(
                    ["lane", "vs llvm_ra_greedy", "vs own:none", "cases"],
                    [
                        [
                            row["config"],
                            row["geomean_vs_llvm_ra_greedy"],
                            row["geomean_vs_own_none"],
                            row["cases"],
                        ]
                        for row in llvm_rows
                    ],
                ),
                "",
            ]
        )
    if gap_rows:
        content.extend(
            [
                "## Backend Decomposition",
                "",
                markdown_table(
                    ["metric", "geomean", "meaning"],
                    [
                        ["own_ra_effect", aggregate_ratio(gap_rows, "own_ra_effect"), "best own RA / own none"],
                        ["llvm_ra_spread", aggregate_ratio(gap_rows, "llvm_ra_spread"), "llvm fast / llvm greedy"],
                        ["backend_gap", aggregate_ratio(gap_rows, "backend_gap"), "llvm greedy / own none"],
                        ["extra_llvm_opt_gap", aggregate_ratio(gap_rows, "extra_llvm_opt_gap"), "same-IR clang O2 / llvm greedy"],
                        ["whole_compiler_gap", aggregate_ratio(gap_rows, "whole_compiler_gap"), "direct clang O2 / own none"],
                    ],
                ),
                "",
            ]
        )
    if microbench_rows:
        content.extend(
            [
                "## Diagnostic Microbenchmarks",
                "",
                markdown_table(
                    ["case", "signal", "own RA", "llvm spread", "backend gap", "detail"],
                    [
                        [
                            row["case"],
                            row["signal_ok"],
                            row["own_ra_effect"],
                            row["llvm_ra_spread"],
                            row["backend_gap"],
                            row["signal_detail"],
                        ]
                        for row in microbench_rows
                    ],
                ),
                "",
            ]
        )
    if interactions:
        content.extend(
            [
                "## Strongest Interactions",
                "",
                markdown_table(
                    ["suite/case", "config", "actual", "expected", "factor"],
                    [
                        [f"{row['suite']}/{row['case']}", row["config"], row["actual_ratio"], row["expected_ratio"], row["interaction_factor"]]
                        for row in interactions
                    ],
                ),
                "",
            ]
        )
    if regressions:
        content.extend(
            [
                "## Worst Regressions",
                "",
                markdown_table(
                    ["suite/case", "config", "ratio", "Δspill", "Δreload", "Δmove", "Δinst"],
                    [
                        [
                            f"{row['suite']}/{row['case']}",
                            row["config"],
                            row["normalized_runtime"],
                            row["delta_spilled_values"],
                            row["delta_reloads"],
                            row["delta_moves"],
                            row["delta_machine_insts"],
                        ]
                        for row in regressions
                    ],
                ),
                "",
            ]
        )
    if skipped_rows:
        by_lane: dict[tuple[str, str], int] = defaultdict(int)
        for row in skipped_rows:
            by_lane[(row["config"], row.get("skip_reason", ""))] += 1
        content.extend(
            [
                "## Incomplete Lanes",
                "",
                markdown_table(
                    ["lane", "skipped cases", "reason"],
                    [[lane, count, reason] for (lane, reason), count in sorted(by_lane.items())],
                ),
                "",
            ]
        )
    path.write_text("\n".join(content), encoding="utf-8")


def main() -> int:
    args = parse_args()
    output_dir = args.output_dir or args.records.parent
    output_dir.mkdir(parents=True, exist_ok=True)
    records = load_records(args.records)
    benchmark_rows = collect_benchmark_rows(records)
    skipped_rows = collect_skipped_benchmark_rows(records)
    normalized = build_normalized_rows(benchmark_rows)
    real_normalized = [row for row in normalized if not is_microbench_suite(row["suite"])]
    summaries = config_summary(real_normalized)
    case_rows = case_summary_rows(normalized)
    external_rows = external_baseline_rows(benchmark_rows)
    llvm_rows = llvm_regalloc_summary(benchmark_rows)
    gap_rows = backend_gap_rows(benchmark_rows, microbench=False)
    microbench_rows = microbench_summary_rows(benchmark_rows)
    interactions = strongest_interactions(real_normalized)
    regressions = worst_regressions(real_normalized)

    write_csv(output_dir / "config_summary.csv", summaries)
    write_csv(output_dir / "case_summary.csv", case_rows)
    write_csv(output_dir / "external_baselines.csv", external_rows)
    write_csv(output_dir / "llvm_regalloc_summary.csv", llvm_rows)
    write_csv(output_dir / "backend_gap_summary.csv", gap_rows)
    write_csv(output_dir / "microbench_summary.csv", microbench_rows)
    write_csv(output_dir / "strongest_interactions.csv", interactions)
    write_csv(output_dir / "worst_regressions.csv", regressions)
    write_markdown_report(output_dir / "summary.md", summaries, llvm_rows, gap_rows, microbench_rows, interactions, regressions, skipped_rows)

    if summaries:
        print(f"best config: {summaries[0]['config']} overall geomean={summaries[0]['overall_geomean']:.4f}")
    print(f"reports: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
