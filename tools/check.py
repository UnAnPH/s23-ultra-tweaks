#!/usr/bin/env python3
"""Consistency and syntax checks for s23-ultra-tweaks."""
import ast
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SHELL_SCRIPTS = [
    "install.sh",
    "device/tweaks.sh",
    "module/build.sh",
    "module/customize.sh",
    "module/service.sh",
    "module/action.sh",
    "module/uninstall.sh",
    "tools/hideksupatch.sh",
]


def read_lf(rel_path):
    with open(os.path.join(ROOT, rel_path), "rb") as f:
        return f.read().replace(b"\r\n", b"\n").decode("utf-8")


def check_shell_syntax():
    for rel in SHELL_SCRIPTS:
        src = read_lf(rel)
        try:
            r = subprocess.run(
                ["bash", "-n"],
                input=src.encode("utf-8"),
                capture_output=True,
            )
        except FileNotFoundError:
            print("WARN: bash not found; skipping shell syntax checks")
            return
        if r.returncode != 0:
            err = r.stderr.decode("utf-8", "replace").strip()
            raise AssertionError(f"bash -n failed for {rel}: {err}")
    print(f"OK: bash -n passed for {len(SHELL_SCRIPTS)} shell scripts")


def check_python_syntax():
    tools_dir = os.path.join(ROOT, "tools")
    py_files = sorted(f for f in os.listdir(tools_dir) if f.endswith(".py"))
    for name in py_files:
        rel = os.path.join("tools", name)
        ast.parse(read_lf(rel), filename=rel)
    print(f"OK: AST parse passed for {len(py_files)} Python files in tools/")


def check_tweak_parity():
    tw_src = read_lf("device/tweaks.sh")
    tweak_ids = re.search(r'^TWEAK_IDS="([^"]+)"', tw_src, re.M).group(1).split()
    stays = re.search(r'^STAYS_TILL_REBOOT="([^"]+)"', tw_src, re.M).group(1).split()

    bad_stays = set(stays) - set(tweak_ids)
    assert not bad_stays, f"STAYS_TILL_REBOOT has unknown IDs: {sorted(bad_stays)}"

    inst_src = read_lf("install.sh")
    m = re.search(r"(^ids=\(.*?^why=\([^\n]*\))", inst_src, re.M | re.S)
    assert m, "Could not locate tweak arrays in install.sh"
    arr_src = m.group(1)
    probe_sh = (
        "can_airdrop=1 why_airdrop='' can_ct=1 why_ct='' can_lc=1 why_lc='' "
        "can_ksu=1 why_ksu='' can_rec=1 why_rec='' can_csc=1 why_csc=''\n"
        f"{arr_src}\n"
        'echo "LEN:${#ids[@]}:${#cat[@]}:${#desc[@]}:${#avail[@]}:${#why[@]}"\n'
        'echo "IDS:${ids[*]}"\n'
    )
    try:
        r = subprocess.run(
            ["bash", "-s"],
            input=probe_sh.encode("utf-8"),
            capture_output=True,
        )
        assert r.returncode == 0, r.stderr.decode("utf-8", "replace")
        out_lines = r.stdout.decode("utf-8").strip().splitlines()
        lens = [int(x) for x in out_lines[0].split(":")[1:]]
        assert len(set(lens)) == 1 and lens[0] == len(tweak_ids), (
            f"install.sh array lengths {lens} != {len(tweak_ids)}"
        )
        inst_ids = out_lines[1].split(":", 1)[1].split()
    except FileNotFoundError:
        inst_ids = re.search(r"^ids=\(([^)]+)\)", inst_src, re.M).group(1).split()

    html_src = read_lf("module/webroot/index.html")
    t_block = re.search(r"const TWEAKS = \[(.*?)\];", html_src, re.S).group(1)
    web_ids = re.findall(r'\{\s*id:\s*"([^"]+)"', t_block)

    assert len(tweak_ids) == len(set(tweak_ids)), "Duplicate IDs in tweaks.sh TWEAK_IDS"
    assert len(inst_ids) == len(set(inst_ids)), "Duplicate IDs in install.sh ids"
    assert len(web_ids) == len(set(web_ids)), "Duplicate IDs in index.html TWEAKS"

    s_tw, s_in, s_wb = set(tweak_ids), set(inst_ids), set(web_ids)
    assert s_tw == s_in == s_wb, (
        f"Tweak ID mismatch: tweaks-install={s_tw ^ s_in}, tweaks-web={s_tw ^ s_wb}"
    )

    b_block = re.search(r"const BUNDLES = \[(.*?)\];", html_src, re.S).group(1)
    bundle_ids = re.findall(r'ids:\s*\[([^\]]+)\]', b_block)
    for grp in bundle_ids:
        for bid in re.findall(r'"([^"]+)"', grp):
            assert bid in s_wb, f"Bundle ID {bid!r} not in TWEAKS"

    p_block = re.search(r"function probeJson\(\) \{(.*?)\n  \}", html_src, re.S).group(1)
    mock_ids = set(re.findall(r'tw\("([^"]+)"', p_block))
    assert mock_ids == s_wb, (
        f"Mock probeJson() ID mismatch: missing={sorted(s_wb - mock_ids)}, extra={sorted(mock_ids - s_wb)}"
    )

    print(
        f"OK: {len(tweak_ids)} tweak IDs match across tweaks.sh, install.sh, "
        f"index.html TWEAKS/BUNDLES/mock, and STAYS_TILL_REBOOT"
    )


def main():
    check_shell_syntax()
    check_python_syntax()
    check_tweak_parity()


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)
