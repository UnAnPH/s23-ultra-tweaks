# Custom Fork (`UnAnPH/s23-ultra-tweaks`)

Personal fork of [`dtechterminal/s23-ultra-tweaks`](https://github.com/dtechterminal/s23-ultra-tweaks) (`v1.3.9` base).

## Branch Layout

- **`custom`** *(default branch)*: Upstream `main` + PR #24 (`sharing_package_whitelist_check_bypass` for GMS `26.46+`) + 6 separate opt-in toggles in the **Custom** category. All 23 official toggles remain untouched.
- **`main`**: Clean mirror of `dtechterminal/s23-ultra-tweaks:main` (keep untouched so GitHub's **Sync fork** works with zero conflicts).
- **`fix/airdrop-gms-whitelist-bypass`**: PR #24 branch targeting upstream.

## Custom Toggles (`Custom` Category in WebUI)

1. **`perfboost`** — Surgical `perfboostsconfig.xml` edit disabling the 2s all-core max-clock launch boost (`Id="0x00001081" Type="10"`) in `$R/perfboostsconfig.xml` (hidden by `hidemnt`) and restarting `perf-hal-2-3`, keeping One UI 9's WALT scheduler and 120 Hz tunings intact.
2. **`deepdoze`** — Reversible faster Deep Doze timings (`device_idle_constants`), `cached_apps_freezer enabled`, and system Doze whitelist trim (`cmd deviceidle sys-whitelist -<pkg>`).
3. **`debloat`** — Reversible suppression of `gos`, `diagmonagent`, `dqagent`, and `rubin.app` (tracked in `$V/features_disabled_pkgs`).
4. **`extrafeatures`** — Extra One UI 9 battery, system, display (`LCD_CONFIG_AOD_REFRESH_RATE=0`), and SoundAlive (`voice_boost`, keeping stock `dolby_game_spk_off`) floating features + scan/profiler props.
5. **`extracam`** — Extra One UI 9 camera AF/portrait/video floating features.
6. **`vulkangpu`** — `ro.zygote.disable_gl_preload=1` via `resetprop` alongside `vulkan`.

Also includes:
- **AirDrop on Play Services `26.46+`** (PR #24): `sharing_package_whitelist_check_bypass` in `NEARBY_FLAGS` + `pbflag.c` string-flag support.
- **`tools/preflight_root_check.py`**: Read-only ADB check for `cnss2` ThinLTO symbols, symbol CRCs, and `/efs/FactoryApp/*`.

## Versioning Scheme (KernelSU Auto-Update)

KernelSU compares `versionCode` (integer) to offer in-app updates via `module/update.json`:

| Upstream Base | Custom Tag & `version` | `versionCode` (`<UPSTREAM_CODE> * 100 + <N>`) |
|---|---|---|
| `v1.3.9` (`139`) | `v1.3.9-custom.1` | `13901` |
| `v1.3.9` (`139`) | `v1.3.9-custom.2` | `13902` |
| `v1.4.0` (`140`) | `v1.4.0-custom.1` | `14001` |

## Syncing Future Upstream Releases & Releasing

```sh
git fetch origin
git checkout main && git merge --ff-only origin/main && git push fork main
git checkout custom && git merge main
# Bump version/versionCode/zipUrl in module/module.prop & module/update.json (e.g. v1.4.0-custom.1 / 14001)
bash module/build.sh
git commit -am "release: v1.4.0-custom.1" && git push fork custom
git tag v1.4.0-custom.1 && git push fork v1.4.0-custom.1
```
