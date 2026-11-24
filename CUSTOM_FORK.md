# Custom Fork (`UnAnPH/s23-ultra-tweaks`)

Personal fork of [`dtechterminal/s23-ultra-tweaks`](https://github.com/dtechterminal/s23-ultra-tweaks) (`v1.3.9` base).

## Branch Layout

- **`custom`** *(default branch)*: Upstream `main` + PR #24 (`sharing_package_whitelist_check_bypass` for GMS `26.46+`) + audited One UI 9 / CyberK customizations listed below.
- **`main`**: Clean mirror of `dtechterminal/s23-ultra-tweaks:main` (keep untouched so GitHub's **Sync fork** works with zero conflicts).
- **`fix/airdrop-gms-whitelist-bypass`**: PR #24 branch targeting upstream.

## Custom Additions on `custom`

1. **AirDrop on Play Services `26.46+`** ([`device/tweaks.sh`](device/tweaks.sh), [`src/pbflag/pbflag.c`](src/pbflag/pbflag.c)):
   - Adds `sharing_package_whitelist_check_bypass` to `NEARBY_FLAGS` so `com.google.android.mosey` binds despite GMS `26.46.32` source-stamp verification.
2. **Surgical `perfboostsconfig.xml` Launch-Boost Disable** (`perfboost_apply()` / `perfboost_unbind()` in [`device/tweaks.sh`](device/tweaks.sh)):
   - Flips `Id="0x00001081" Type="10" Enable="true"` → `Enable="false"` in `$R/perfboostsconfig.xml` (so `hidemnt` hides the bind mount) and restarts `perf-hal-2-3`, preserving One UI 9's WALT scheduler and 120 Hz tunings.
3. **Reversible Deep Doze & `sys-whitelist` Trim** (`battery_doze_on()` / `battery_doze_off()` in [`device/tweaks.sh`](device/tweaks.sh)):
   - Applies faster Deep Doze timings, `cached_apps_freezer enabled`, and `cmd deviceidle sys-whitelist -<pkg>`, tracked by `$V/battery_doze` and restored cleanly on unpick or `cleanup`.
4. **Reversible Background Package Suppression & Extra One UI 9 Features** (`features_pkgs_off()` / `features_pkgs_on()` in [`device/tweaks.sh`](device/tweaks.sh)):
   - Suppresses `gos`, `diagmonagent`, `dqagent`, and `rubin.app` (tracked in `$V/features_disabled_pkgs` so only newly disabled packages are re-enabled on unpick or `cleanup`).
   - Adds extra floating features for `battery`, `features`, `display` (`LCD_CONFIG_AOD_REFRESH_RATE=0`), and `camextras` while preserving stock `dolby_game_spk_off`.
5. **Vulkan GL Preload Disable** ([`device/tweaks.sh`](device/tweaks.sh)):
   - Sets `ro.zygote.disable_gl_preload=1` via `resetprop` when `vulkan` is enabled.
6. **Host Preflight Tool** ([`tools/preflight_root_check.py`](tools/preflight_root_check.py)):
   - Read-only ADB check for `cnss2` ThinLTO symbols, symbol CRCs, and `/efs/FactoryApp/*`.

## Syncing Future Upstream Releases

```sh
git remote add upstream https://github.com/dtechterminal/s23-ultra-tweaks.git  # once
git fetch upstream
git checkout main && git merge --ff-only upstream/main && git push origin main
git checkout custom && git merge main
```
