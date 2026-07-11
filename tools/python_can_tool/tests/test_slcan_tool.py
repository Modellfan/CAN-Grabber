from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import can
import pytest


TOOL_PATH = Path(__file__).resolve().parents[1] / "slcan_tool.py"
spec = importlib.util.spec_from_file_location("slcan_tool", TOOL_PATH)
slcan_tool = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules["slcan_tool"] = slcan_tool
spec.loader.exec_module(slcan_tool)


def test_fixed_message_encodes_standard_can() -> None:
    msg = slcan_tool.build_fixed_message("3E8", "02", False, 0)
    assert slcan_tool.encode_slcan_message(msg) == "t3E8102"


def test_fixed_message_rejects_out_of_range_standard_id() -> None:
    with pytest.raises(ValueError, match="outside the standard CAN ID range"):
        slcan_tool.build_fixed_message("800", "02", False, 0)


def test_parse_slcan_line_round_trip() -> None:
    msg = slcan_tool.parse_slcan_line("t3E8102", 12.5)
    assert msg is not None
    assert msg.timestamp == 12.5
    assert msg.arbitration_id == 0x3E8
    assert bytes(msg.data) == b"\x02"
    assert not msg.is_extended_id


def test_filter_exact_and_range_match() -> None:
    msg = can.Message(arbitration_id=0x3E8, is_extended_id=False, data=b"\x02")
    args = slcan_tool.build_parser().parse_args(["analyze", "--asc-file", "dummy.asc", "--filter-id", "300-3FF"])
    assert slcan_tool.message_matches_filters(msg, args)
    args = slcan_tool.build_parser().parse_args(["analyze", "--asc-file", "dummy.asc", "--filter-id", "100-1FF"])
    assert not slcan_tool.message_matches_filters(msg, args)


def test_rewrite_id_parser() -> None:
    assert slcan_tool.parse_rewrite_id("3E8:3E9") == (0x3E8, 0x3E9)


def test_subcommand_required() -> None:
    with pytest.raises(SystemExit):
        slcan_tool.build_parser().parse_args([])


def test_playback_asc_reader(tmp_path: Path) -> None:
    asc_path = tmp_path / "sample.asc"
    asc_path.write_text(
        "\n".join(
            [
                "date Sun Jul 05 23:07:56.648 2026",
                "base hex  timestamps absolute",
                "internal events logged",
                "Begin Triggerblock Sun Jul 05 23:07:56.668 2026",
                " 0.000000 Start of measurement",
                " 0.000000 1  3E8             Rx   d 1 02",
            ]
        ),
        encoding="ascii",
    )
    messages = slcan_tool.iter_asc_messages(asc_path)
    assert len(messages) == 1
    assert messages[0].arbitration_id == 0x3E8
