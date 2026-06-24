#!/usr/bin/env python3
"""Analyze an ecmc motion diagnostics JSON dump."""

import argparse
import json
import sys


def yes_no(value):
    return "yes" if value else "no"


def axis_summary(axis):
    blockers = axis.get("motion_blockers", [])
    command = axis.get("command_text", "UNKNOWN")
    state = []
    if axis.get("error"):
        state.append(f"error=0x{axis.get('error_id', 0):x}")
    if axis.get("busy"):
        state.append("busy")
    if axis.get("blocked"):
        state.append("blocked")
    if not axis.get("enabled"):
        state.append("not-enabled")
    if not axis.get("hardware_ready", True):
        state.append("hw-not-ready")
    state_text = ", ".join(state) if state else "state-ok"
    return (
        f"axis {axis.get('index')} id={axis.get('axis_id')} "
        f"type={axis.get('axis_type')} cmd={command} "
        f"exec={yes_no(axis.get('execute'))} {state_text} "
        f"blockers={len(blockers)}"
    )


def print_axis(axis, verbose):
    print(axis_summary(axis))
    blockers = axis.get("motion_blockers", [])
    if blockers:
        for blocker in blockers:
            print(f"  - {blocker}")
    elif verbose:
        print("  - no obvious software blocker in dump")

    if verbose:
        print(
            "  pos: actual={actual_position} set={setpoint_position} target={target_position}".format(
                **axis
            )
        )
        print(
            "  vel: actual={actual_velocity} set={setpoint_velocity} target={target_velocity}".format(
                **axis
            )
        )
        print(
            "  src: traj={traj_source} enc={enc_source} allow_pos={allow_position_motion} "
            "allow_vel={allow_velocity_motion} allow_home={allow_home}".format(**axis)
        )


def print_findings(dump):
    diagnosis = dump.get("diagnosis") or {}
    findings = diagnosis.get("findings") or []
    if not findings:
        return False

    print(f"\nWorker diagnosis: {diagnosis.get('summary', '')} ({len(findings)} findings)")
    for finding in findings:
        prefix = (
            f"{finding.get('severity', 'info').upper()} "
            f"{finding.get('object_type')}[{finding.get('index')}] "
            f"{finding.get('code')}"
        )
        print(f"{prefix}: {finding.get('message', '')}")
        suggestion = finding.get("suggestion")
        if suggestion:
            print(f"  -> {suggestion}")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", help="Path to ecmc motion diagnostics JSON file")
    parser.add_argument("--axis", type=int, help="Only analyze one axis index")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show more state")
    args = parser.parse_args()

    try:
        with open(args.dump, "r", encoding="utf-8") as handle:
            dump = json.load(handle)
    except OSError as exc:
        print(f"error: cannot read {args.dump}: {exc}", file=sys.stderr)
        return 2
    except json.JSONDecodeError as exc:
        print(f"error: invalid JSON in {args.dump}: {exc}", file=sys.stderr)
        return 2

    print(
        "ecmc motion diag: version={version} time={time} axes={axes} groups={groups} sms={sms}".format(
            version=dump.get("ecmc_motion_diag_version", "?"),
            time=dump.get("timestamp", "?"),
            axes=dump.get("axis_count", "?"),
            groups=dump.get("axis_group_count", "?"),
            sms=dump.get("master_slave_state_machine_count", "?"),
        )
    )

    if dump.get("controller_error", 0) not in (0, -1):
        print(f"controller error: {dump.get('controller_error')} {dump.get('controller_error_text', '')}")

    printed_worker_findings = print_findings(dump)

    axes = dump.get("axes", [])
    if args.axis is not None:
        axes = [axis for axis in axes if axis.get("index") == args.axis]
        if not axes:
            print(f"axis {args.axis}: not found")
            return 1

    blocked_axes = [axis for axis in axes if axis.get("motion_blockers")]
    if blocked_axes and not printed_worker_findings:
        print("\nLikely motion blockers:")
        for axis in blocked_axes:
            print_axis(axis, args.verbose)
    elif not blocked_axes and not printed_worker_findings:
        print("\nNo obvious axis motion blockers found.")
        if args.verbose:
            for axis in axes:
                print_axis(axis, args.verbose)

    groups = dump.get("axis_groups", [])
    if groups and args.axis is None:
        print("\nAxis groups:")
        for group in groups:
            summary = group.get("summary", {})
            flags = []
            if group.get("blocked"):
                flags.append("blocked")
            if summary.get("any_interlocked"):
                flags.append("interlocked")
            if not summary.get("all_enabled", True):
                flags.append("not-all-enabled")
            if summary.get("any_busy"):
                flags.append("busy")
            flag_text = ", ".join(flags) if flags else "ok"
            print(f"group {group.get('index')} {group.get('name')} axes={group.get('axes')} {flag_text}")

    sms = dump.get("master_slave_state_machines", [])
    if sms and args.axis is None:
        print("\nMaster/slave state machines:")
        for sm in sms:
            print(
                f"sm {sm.get('index')} {sm.get('name')} state={sm.get('state_text')} "
                f"enabled={yes_no(sm.get('enabled'))} status={sm.get('status')}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
