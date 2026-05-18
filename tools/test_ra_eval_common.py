#!/usr/bin/env python3
"""Small self-tests for the RA evaluation helpers."""

from __future__ import annotations

import copy
import unittest

from analyze_ra_eval import backend_gap_rows, collect_skipped_benchmark_rows, config_summary
from ra_eval_common import LLVM_LANES, RAStatsValidationError, geometric_mean, parse_objdump_metrics, summarize_ra_stats, timing_summary, validate_ra_stats_payload


class RAEvalCommonTests(unittest.TestCase):
    def setUp(self) -> None:
        self.payload = {
            "schema_version": 1,
            "target": "RISCV64",
            "config": {"callee_saved_fpr": False, "coalesce": False, "split": False},
            "functions": [
                {
                    "name": "main",
                    "assigned_reg_intervals": 3,
                    "assigned_gpr_intervals": 3,
                    "assigned_fpr_intervals": 0,
                    "spilled_intervals": 1,
                    "spilled_values": 1,
                    "estimated_reloads": 2,
                    "estimated_spill_stores": 1,
                    "eliminated_copies": 0,
                    "split_count": 0,
                    "frame_size": 16,
                    "used_callee_saved_gpr": [9],
                    "used_callee_saved_fpr": [],
                    "machine_instruction_count": 10,
                    "stack_load_count": 2,
                    "stack_store_count": 1,
                    "move_instruction_count": 3,
                }
            ],
        }

    def test_valid_payload_summarizes(self) -> None:
        validate_ra_stats_payload(self.payload)
        summary = summarize_ra_stats(self.payload)
        self.assertEqual(summary["spilled_values"], 1)
        self.assertEqual(summary["used_callee_saved_gpr_count"], 1)

    def test_missing_metric_is_rejected(self) -> None:
        broken = copy.deepcopy(self.payload)
        del broken["functions"][0]["spilled_values"]
        with self.assertRaises(RAStatsValidationError):
            validate_ra_stats_payload(broken)

    def test_timing_summary_is_stable(self) -> None:
        summary = timing_summary([110, 100, 105, 95, 100, 102, 98])
        self.assertEqual(summary["median_us"], 100.0)
        self.assertEqual(summary["mad_us"], 2.0)

    def test_geometric_mean(self) -> None:
        self.assertAlmostEqual(geometric_mean([1.0, 4.0]), 2.0)

    def test_llvm_lane_matrix_is_stable(self) -> None:
        self.assertEqual(
            [lane["name"] for lane in LLVM_LANES],
            ["llvm_ra_fast", "llvm_ra_basic", "llvm_ra_greedy", "same_ir_clang_o2", "direct_clang_o2"],
        )

    def test_objdump_metrics_parse_no_aliases_output(self) -> None:
        metrics = parse_objdump_metrics(
            """
               0: 86aa                 c.mv    a3,a0
               2: 0005061b             addiw   a2,a0,0
               6: fe842783             lw      a5,-24(s0)
               a: e122                 c.sdsp  s0,128(sp)
               c: 20b50553             fsgnj.s fa0,fa0,fa0
            """
        )
        self.assertEqual(metrics["objdump_move_like_count"], 3)
        self.assertEqual(metrics["objdump_stack_load_count"], 1)
        self.assertEqual(metrics["objdump_stack_store_count"], 1)

    def test_backend_gap_formulas(self) -> None:
        rows = [
            {"suite": "2026_performance", "case": "x", "config": "none", "median_us": 100.0},
            {"suite": "2026_performance", "case": "x", "config": "split", "median_us": 90.0},
            {"suite": "2026_performance", "case": "x", "config": "llvm_ra_fast", "median_us": 80.0},
            {"suite": "2026_performance", "case": "x", "config": "llvm_ra_basic", "median_us": 70.0},
            {"suite": "2026_performance", "case": "x", "config": "llvm_ra_greedy", "median_us": 60.0},
            {"suite": "2026_performance", "case": "x", "config": "same_ir_clang_o2", "median_us": 50.0},
            {"suite": "2026_performance", "case": "x", "config": "direct_clang_o2", "median_us": 40.0},
        ]
        gap = backend_gap_rows(rows)[0]
        self.assertAlmostEqual(gap["own_ra_effect"], 0.9)
        self.assertAlmostEqual(gap["llvm_ra_spread"], 80.0 / 60.0)
        self.assertAlmostEqual(gap["backend_gap"], 0.6)
        self.assertAlmostEqual(gap["extra_llvm_opt_gap"], 50.0 / 60.0)
        self.assertAlmostEqual(gap["whole_compiler_gap"], 0.4)

    def test_skipped_lane_records_are_preserved(self) -> None:
        rows = collect_skipped_benchmark_rows(
            [
                {
                    "record_type": "benchmark",
                    "kind": "llvm_regalloc",
                    "suite": "2026_performance",
                    "case": "x",
                    "config": "llvm_ra_fast",
                    "skipped": True,
                    "skip_reason": "missing tool(s): llc",
                }
            ]
        )
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["config"], "llvm_ra_fast")

    def test_microbench_rows_can_be_excluded_from_overall(self) -> None:
        real = {
            "suite": "2026_performance",
            "case": "real",
            "config": "none",
            "normalized_runtime": 1.0,
            "ra_summary": self._summary_with_moves(3),
            "baseline_summary": self._summary_with_moves(3),
        }
        micro = {
            "suite": "ra_microbench",
            "case": "diag",
            "config": "none",
            "normalized_runtime": 4.0,
            "ra_summary": self._summary_with_moves(3),
            "baseline_summary": self._summary_with_moves(3),
        }
        summary = config_summary([real])
        polluted = config_summary([real, micro])
        self.assertEqual(summary[0]["overall_geomean"], 1.0)
        self.assertEqual(polluted[0]["overall_geomean"], 2.0)

    @staticmethod
    def _summary_with_moves(moves: int) -> dict[str, int]:
        return {
            "spilled_values": 0,
            "estimated_reloads": 0,
            "estimated_spill_stores": 0,
            "eliminated_copies": 0,
            "split_count": 0,
            "machine_instruction_count": 0,
            "move_instruction_count": moves,
        }


if __name__ == "__main__":
    unittest.main()
