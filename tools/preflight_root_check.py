#!/usr/bin/env python3
"""Read-only preflight check over ADB once root (DFRoot + KernelSU) is active.

Verifies:
1. Root access via `adb shell su -c id -u`.
2. Symbol CRCs in `/vendor_dlkm/lib/modules/*.ko` and `/system_dlkm/lib/modules/*/*.ko`
   against `payloads/airdrop_required_symvers.txt`.
3. Presence of `plat_env` (inside `cnss2` `.bss`) and `cnss_mhi_bw_scale.cfi_jt`
   in `/proc/kallsyms`.
4. Presence of `/efs/FactoryApp/` battery calibration files for the `battery` tweak.
"""
import os
import struct
import subprocess
import sys


ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REQ_SYMVERS = os.path.join(ROOT_DIR, "payloads", "airdrop_required_symvers.txt")


def adb_su(cmd: str) -> bytes:
    adb_cmd = ["adb"]
    if len(sys.argv) >= 3 and sys.argv[1] == "-s":
        adb_cmd += ["-s", sys.argv[2]]
    elif len(sys.argv) == 2 and not sys.argv[1].startswith("-"):
        adb_cmd += ["-s", sys.argv[1]]
    return subprocess.check_output(adb_cmd + ["exec-out", f"su -c '{cmd}'"])


def load_required_symvers(path: str) -> dict:
    req = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2:
                req[parts[1]] = int(parts[0], 16)
    return req


def parse_ko_versions(data: bytes) -> dict:
    if len(data) < 64 or data[:4] != b"\x7fELF":
        return {}
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    hdrs = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
    stro = hdrs[shstrndx][4]
    sects = {}
    for h in hdrs:
        name = data[stro + h[0]:data.index(b"\0", stro + h[0])].decode(errors="ignore")
        sects[name] = data[h[4]:h[4] + h[5]]
    v = sects.get("__versions", b"")
    out = {}
    for i in range(0, len(v), 64):
        crc, = struct.unpack_from("<Q", v, i)
        name = v[i + 8:i + 64].split(b"\0")[0].decode(errors="ignore")
        if name:
            out[name] = crc & 0xFFFFFFFF
    return out


def main() -> int:
    try:
        uid = adb_su("id -u").decode().strip()
    except subprocess.CalledProcessError as e:
        print(f"ERROR: adb/su failed: {e}", file=sys.stderr)
        return 1
    if uid != "0":
        print(f"ERROR: expected uid 0, got {uid!r}. Allow Shell in KernelSU > Superuser.", file=sys.stderr)
        return 1
    print("[1/4] Root OK (uid=0)")

    req = load_required_symvers(REQ_SYMVERS)
    print(f"[2/4] Checking {len(req)} required kernel symbol CRCs against device modules...")
    ko_paths = adb_su("ls /vendor_dlkm/lib/modules/*.ko /system_dlkm/lib/modules/*/*.ko 2>/dev/null").decode().splitlines()
    phone_crcs = {}
    for kp in ko_paths:
        kp = kp.strip()
        if not kp:
            continue
        data = adb_su(f"cat {kp}")
        phone_crcs.update(parse_ko_versions(data))

    mismatches = []
    missing = []
    for sym, want_crc in req.items():
        if sym not in phone_crcs:
            missing.append(sym)
        elif phone_crcs[sym] != want_crc:
            mismatches.append((sym, hex(want_crc), hex(phone_crcs[sym])))

    print(f"      Parsed {len(ko_paths)} device .ko files ({len(phone_crcs)} unique imported symbols)")
    print(f"      Matched: {len(req) - len(mismatches) - len(missing)}/{len(req)}")
    if mismatches:
        print(f"      MISMATCHES ({len(mismatches)}):")
        for m in mismatches[:15]:
            print(f"        {m[0]}: payload={m[1]} vs phone={m[2]}")
    if missing:
        print(f"      Not directly imported by stock .ko ({len(missing)}) (normal for module-only symbols): {missing[:10]}")

    print("[3/4] Checking /proc/kallsyms for cnss2 symbols...")
    ks = adb_su(
        "o=$(cat /proc/sys/kernel/kptr_restrict); echo 1 > /proc/sys/kernel/kptr_restrict; "
        "grep -F \"[cnss2]\" /proc/kallsyms | grep -E \" (plat_env(\\.llvm\\..*)?|cnss_mhi_bw_scale\\.cfi_jt)[[:space:]]\"; "
        "cat /sys/module/cnss2/sections/.bss 2>/dev/null; "
        "echo $o > /proc/sys/kernel/kptr_restrict"
    ).decode().strip()
    print("     ", ks.replace("\n", "\n      "))

    print("[4/4] Checking /efs/FactoryApp battery files (batt_full_status_usage is optional)...")
    batt = adb_su("ls -l /efs/FactoryApp/HwParamBattQR /efs/FactoryApp/batt_beginning_date /efs/FactoryApp/asoc /efs/FactoryApp/batt_discharge_level /efs/FactoryApp/batt_full_status_usage 2>&1 || true").decode().strip()
    print("     ", batt.replace("\n", "\n      "))

    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
