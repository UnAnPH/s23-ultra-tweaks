# One UI 9 quietly removed the Galaxy S23 Ultra's HDR processing. Planned obsolescence?

The S23 Ultra's One UI 6 firmware from 2023 had two ArcSoft camera processing steps: multi-frame HDR and
low-light HDR. On One UI 9 both are gone: the libraries and their entries in the camera's processing list.
The camera app still has the steps, and without them it runs a stand-in that returns the frame
unchanged. The evidence below shows what was removed, not why, so whether this is planned obsolescence
is a question it raises, not one it answers.

## How we tracked it down

1. **What looked wrong.** Owners report that on One UI 9 S23 Ultra photos look flat and soft, that the
   final image appears instantly, and that the old "processing" step after a shot is gone.
2. **Is processing dead?** No. A low-light shot in logcat ran the full path: a draft, 11 RAW frames
   through Super Night (3.1 s), and the processed photo 5.4 s after the shot
   ([details](#1-the-low-light-path-still-processes)).
3. **A first, ordinary cause.** If the camera process is killed while a photo is still processing, the
   unprocessed draft becomes the permanent photo
   ([details](#2-a-killed-camera-keeps-the-draft-forever)).
4. **Compare the firmware.** We put the S23's One UI 6 firmware (S918WVLS2BWKA, 2023) next to its
   current One UI 9 firmware (S918WVLU8GZIF) and the S24 Ultra's (S928BXXU6EZJB). We checked the
   `CAMERA_CONFIG_VENDOR_LIB_INFO` feature flag, `/system/lib64`, `public.libraries-arcsoft.txt` and
   `camera-feature.xml`.
5. **What was missing.** `mfhdr.arcsoft.v4` and `llhdr.arcsoft.v4` are gone from the list, and
   `libhigh_dynamic_range.arcsoft.so` (13 MB) and `liblow_light_hdr.arcsoft.so` (19 MB) are gone from
   the phone. The S24 Ultra doesn't have them either. `camera-feature.xml` has no processing-related
   change ([table](#3-what-one-ui-9-removed)).
6. **Look inside the camera app.** It still sends shot modes 20–29 to an `MF_HDR` chain and 30–35,
   37–39 and 60 to an `LL_HDR` chain. When a chain's feature isn't listed, it uses `MfHdrDummyNode` /
   `LlHdrDummyNode`, which return the input buffer unchanged. Today's `libnode-jni.so` still names both
   libraries, and the symbols it looks up match the 2023 libraries' exports exactly
   ([details](#4-why-the-2023-libraries-should-still-work)).
7. **Rule out the rest.** "Prioritize quality" is on maximum, there is no server-side processing
   config, and this module's own flag changes don't touch the processing list
   ([details](#6-settings-and-the-reddit-claims)).
8. **Put it back.** The `camerahdr` tweak adds the two 2023 libraries, their lines in
   `public.libraries-arcsoft.txt` and the two list entries, then goes through a framework restart.
   **It is untested** ([the fix and how to check it](#fix-camerahdr-tweak-untested)).

**Still open:** only dark scenes were shot, and they used Super Night or a single frame. No MF_HDR or
LL_HDR mode appeared ([shot modes](#5-shot-modes-seen-live-dark-scenes-only)). Daylight, backlit, 3x and
10x shots will show whether One UI 9 still picks those modes and falls back to the dummy. If it never
does, the removal is harmless on this firmware.

The camera isn't the only place where the firmware leaves out something the S23 can do. One UI 9 still
ships Director's view, which the S23 had in 2023, but hides it. The S23's camera already records 10-bit;
Log video only needs Samsung's switches turned on. Samsung compiled the S23's Settings to hide the
Adaptive color tone switch. Not every gap is like this: Samsung's model for the AI weather wallpaper
is built for newer NPUs, not the S23's (and Call Screening's on-device LLM failed in an initial NPU
test, though the maintainer later reported the AI summary working with Language Core 4.0.01.24; AI
replies remain untested)
([weather wallpaper](#ai-weather-wallpaper-what-samsungs-pipeline-expects),
[Call Screening](#call-screening-one-ui-9-phone-app-on-the-s23)).

## Findings

Research notes from porting S24 features to the Galaxy S23 Ultra (SM-S918W, S918WVLU8GZIF, One UI 9 /
Android 17). Each section says what was confirmed on the phone and what is still unverified.

## Camera: photos not processed after capture (One UI 9)

Written 2026-11-22. Camera app 17.0.00.43. Compared against the S23's One UI 6 firmware (S918WVLS2BWKA,
2023) and the S24 Ultra (S928BXXU6EZJB).

Owners report that on One UI 9 S23 Ultra photos look flat and soft, that the final image appears
instantly, and that the old "processing" step after a shot is gone.

### Summary

- **Processing is not skipped everywhere.** A low-light shot ran the full path. The camera saved a
  draft, fed 11 RAW frames through Super Night (3.1 s), and replaced the draft with the processed photo
  5.4 s after the shot.
- **One UI 9 removed two of the S23's processing steps:** ArcSoft multi-frame HDR (`mfhdr.arcsoft.v4`)
  and low-light HDR (`llhdr.arcsoft.v4`). Both the feature-list entries and the libraries are gone. The
  camera app still contains both steps. When a step isn't listed, it substitutes a "dummy" step that
  returns the frame unchanged. That would produce exactly the reported symptom: a flat image that is
  "done" instantly.
- **Unverified: whether One UI 9 still picks those HDR modes for any scene.** Only dark scenes were shot,
  and they used Super Night or a single frame. Daylight, backlit, 3x and 10x shots will settle it.
- **Confirmed second cause:** if the camera process is killed while a photo is still processing, the
  unprocessed draft becomes the permanent photo. This happens on a swipe-away, a memory kill or a
  background app killer.

### 1. The low-light path still processes

```
19:53:05.639 SingleCaptureController: requestSingleCapture : PROCESSING_POST
19:53:05.656 RearPhotoMaker: takePostProcessingPicture - ... dsMode = SUPER_NIGHT_HANDHELD
19:53:06.493 RearPhotoMaker: onDraftPostProcessingPictureTaken ...      (draft saved)
19:53:06.804 V2/MpiSuperNightNode: processPicture - Start : (1/11) ... RAW_SENSOR
19:53:10.889 MFP30IF : MFRP Lib processTime: 3078.8 ms
19:53:11.886 PostSavingStateManager: postSavingStateChanged(ppSequenceId:0)  E : DRAFT->COMPLETE
```

### 2. A killed camera keeps the draft forever

A Super Night shot (6 RAW frames) was cut off by a force-stop about 10 s after capture. On the next
launch the camera discarded the pending work:

```
RecoveryProcessManager: deleteDanglingDraftImageFiles - /data/sec/camera/native/20261122_195149_temp.jpg
```

The unprocessed draft stayed in Gallery as the final photo. Leave the camera open until the processing
indicator is gone, and keep the camera out of any app killer.

### 3. What One UI 9 removed

| Item | S23 One UI 6 (2023) | S23 One UI 9 | S24 |
|---|---|---|---|
| `mfhdr.arcsoft.v4` in `CAMERA_CONFIG_VENDOR_LIB_INFO` | yes | **no** | no |
| `llhdr.arcsoft.v4` in `CAMERA_CONFIG_VENDOR_LIB_INFO` | yes | **no** | no |
| `scene_detection.samsung.v1` in `CAMERA_CONFIG_VENDOR_LIB_INFO` | yes | no (now done in the camera HAL) | no |
| `image_codec.samsung.v2` in `CAMERA_CONFIG_VENDOR_LIB_INFO` | no | yes | yes |
| `/system/lib64/libhigh_dynamic_range.arcsoft.so` | 13,032,680 B, md5 `c6b7aa034c3704dcfccc87ca32582dc6` | **absent** | absent |
| `/system/lib64/liblow_light_hdr.arcsoft.so` | 19,178,408 B, md5 `656c3c400bb9ebf74a05e23865c334cb` | **absent** | absent |
| both listed in `/system/etc/public.libraries-arcsoft.txt` | yes | no | no |

- **`camera-feature.xml`:** only UI-guide and Camera Assistant entries differ; nothing processing-related.
- **Other camera flags:** nothing else was removed; One UI 9 added some (AIFRC, GPPM, PREWARMING_LEVEL…).
- **`aimfisp.samsung.v1`:** the camera rejects it as an invalid node name in both builds. Not a
  regression.

### 4. Why the 2023 libraries should still work

- **Routing:** the camera app (`NodeChainKeyContainer`) sends shot modes 20–29 to the `MF_HDR` chain and
  30–35, 37–39 and 60 to the `LL_HDR` chain.
- **Dummy fallback:** `NodeFeature` maps each chain to `MfHdrDummyNode` / `LlHdrDummyNode` when its
  feature isn't registered. Their `processPicture*` methods return the input buffer unchanged.
- **Where the list comes from:** only `SemFloatingFeature.getString("SEC_FLOATING_FEATURE_CAMERA_CONFIG_VENDOR_LIB_INFO")`.
- **Native code is still there:** today's `libnode-jni.so` still has `mfhdr::arcsoft::v4::ArcMfHdrNode`
  and `llhdr::arcsoft::v4::ArcLlHdrNode`, and names both libraries.
- **Exports match:** the symbols it looks up match the 2023 libraries' exports exactly (8 `ARC_HDR_*_V4`,
  11 `ARC_LLHDR_*`).
- **Dependencies are present:** both libraries need only libmpbase, liblog, libc, libm and libdl, which
  are all on the phone.
- **Unverified:** compatibility beyond symbol names (struct layouts). The 2023 libraries also carry
  2023 tuning.

### 5. Shot modes seen live (dark scenes only)

- `SINGLE` (processing mode 0x2: no app processing, so the photo is final instantly)
- `SUPER_NIGHT_HANDHELD` (80)
- `SUPER_NIGHT_EXTREME_DARK` (83)
- `HYBRID_NNHDR_MERGE_QZ` (241, preview only)

No MF_HDR or LL_HDR mode appeared. The camera HAL's string tables list the same 21 MFHDR/LLHDR/NNHDR
mode names in 2023, today and on the S24, so the strings alone don't settle whether they are still used.

### 6. Settings and the Reddit claims

- **"Prioritize quality isn't the cause":** agrees with the code. This phone is on maximum
  (`quality_optimization=0`, i.e. MAX), with Scene optimizer and HDR on.
- **"The final image appears instantly":** confirmed for any `SINGLE` shot and for a draft whose
  processing was killed.
- **"The processing setting disappears without Camera Assistant":** not checked. The setting still exists
  in the camera code; Camera Assistant isn't installed on this phone.
- **No server-side processing config:** the camera has no downloaded config, and the Secure, System and
  Global tables hold no camera-processing keys.
- **Our tweaks don't cause it:** the module's flag changes (Log video, Motion Photo extras, high-res
  binning, document scan) don't touch the processing list. The losses above are in Samsung's stock files.

### Fix: `camerahdr` tweak (untested)

`camerahdr` puts back what One UI 9 removed. Nothing is written to a partition, and all of it is gone after a reboot.

- The two 2023 libraries, added to `/system/lib64`: an overlay adds empty placeholders and the libraries are bind-mounted over them from storage, so they use no RAM.
- Their two lines in `public.libraries-arcsoft.txt`, also through an overlay.
- `mfhdr.arcsoft.v4,llhdr.arcsoft.v4` appended to `CAMERA_CONFIG_VENDOR_LIB_INFO`.

`SemFloatingFeature` parses the flag file once per process, and zygote may hold the parsed copy. So the
tweak goes through the framework restart, like the other flag tweaks.

**How to check it on the phone:**

1. **Stock baseline.** Shoot a bright backlit window, a normal indoor scene, 3x and 10x. In logcat look
   for `takePostProcessingPicture ... dsMode = MFHDR_MERGE_*` or `LLHDR_MERGE_*`, and above all for
   `NodeFactory: creating dummyNode(...MfHdrDummyNode` or `...LlHdrDummyNode`. That line proves the step
   is being skipped.
2. **With `camerahdr` on.** The camera's start-up log should show
   `loadNodeFeature - register node feature info [mfhdr.arcsoft.v4]` and `[llhdr.arcsoft.v4]`. Shooting
   the same scenes should show `ArcMfHdrNode` / `ArcLlHdrNode` and `DRAFT->COMPLETE`, with no
   `dlopen` errors and no crash.
3. **Compare** the before/after photos at 100 %: detail, highlights in the backlit shot, colour.

If step 1 never shows a dummy node, the removal is harmless on this firmware. Then the remaining causes
are killed drafts and Samsung's HAL-side changes, and `camerahdr` should stay off.

### Lessons

- **Don't read the stock file from under a bind mount with toybox `unshare -m`.** It doesn't make mounts
  private, so the `umount` reached init's namespace and removed two live overrides for about 3 minutes.
  Use util-linux `unshare -m --propagation private`, or `mount --make-rprivate /` first, or read the file
  from the firmware image instead.

## AI weather wallpaper: what Samsung's pipeline expects

Written 2026-11-22. Samsung's weather wallpaper is built for the S24's NPU (Hexagon V75). The S23 has V73,
so its three model files have to be rebuilt. These notes say what the rebuilt files must accept. They come
from `libSSNeuralCoreVNDK.so` and `liblvm.samsung.so` in Samsung AI Core, and the wallpaper package in
VisionModel 200517500.

### Confirmed from the code

- **Pipeline:** the wallpaper runs the "Adapter3" pipeline (`SS_SR_WALLPAPER_ADAPTER`, foundation model
  `SD_V15_I2I`, 10 steps, OLSS scheduler with `olss_weights.json`).
- **Encoder (SSNet1):** the image goes in normalised to −1..1. The pipeline multiplies the output by
  `encoder_scale = 0.18215`, hard-coded in `liblvm` and logged as "scaling SSNet1 output by %f". So the
  encoder model must output **unscaled** latents.
- **Conditioning:** the pipeline concatenates `[one channel of ones, scaled image latents (4)]` for the
  **whole** image. No segmentation mask goes into the UNet. The sky mask (`segMaskBuffer`, fast guided
  filter in `liblvm`) is only used to blend the result afterwards.
- **Decoder (SSNet3):** the denoised latents go in unchanged, and afterwards the pipeline only clamps to
  0..1. So the decoder model must apply `1/0.18215` itself and output `x/2 + 0.5`.
- **Guidance:** each prompt pair has two scales (`wallpaper_prompt_scale.txt`, e.g. `6 7 6.5 1.6`), logged
  as "using guidance scaling factors %f and %f". There is a three-way concatenation in the denoise loop.
  This matches InstructPix2Pix guidance, with a text scale and an image scale.
- **Mode-2 scaling isn't this path:** the `Image2ImagePipeline` class also scales latents by 0.18215 / 1/0.18215,
  but only in its mode 2. The wallpaper uses Adapter3.

### Tested on a PC (float, public models, Samsung's conventions)

- **SD1.5-inpainting UNet (first rebuild): fails.** With Samsung's conditioning (mask all ones, full-image
  latents) it only blows out the colours and makes no weather change. It produced good edits only with a
  sky mask and masked latents, which Samsung's pipeline never provides.
- **InstructPix2Pix UNet (SD1.5 base): works.** With the image latents un-scaled inside the graph and
  three-pass guidance using Samsung's own prompt pairs and scales, it gives scene-preserving snow,
  snowy-evening, golden-hour, night, clear-after-rain and rainy-night edits. "Dark night" at text scale 9.0
  comes out nearly black.
- **Next:** rebuild the UNet and its time-embedding file from InstructPix2Pix for V73. Then test on the
  phone; the order of the UNet's 9 input channels is only inferred (`[noisy latents, ones, image latents]`).

## Call Screening (One UI 9 Phone app on the S23)

**Status: screening, greeting, voice messages and AI summary work; AI replies untested.** An initial
live-call test on 2026-11-23 failed the summary and AI replies on the NPU (V81 vs. V73, logged below);
the maintainer later confirmed the AI summary works with Language Core 4.0.01.24 (MD5
`994620f3071706391d17ff132ea16c42`), while AI replies remain untested.

| Part | Status |
|---|---|
| Call screening switch (Phone settings) | Works once Language Core is installed |
| Screen call button on incoming calls | Works |
| Greeting read to the caller (TTS, Hindi tested) | Works |
| Recording the caller / voice message | Works |
| Caller name and purpose summary | Works with Language Core 4.0.01.24 (earlier test failed with "Something went wrong while summarizing this call.") |
| AI-generated replies to the caller | Untested (failed with `FAILED_SLC_PROCESS_KILLED` in the earlier test; screening carries on without them) |

### How it was enabled

- The Phone settings app (TelephonyUI) only checks that Samsung Language Core
  (`com.samsung.android.offline.languagemodel`) lists `CallScreeningResponse` in its `FUNCTION_INFO`
  manifest metadata. The S23 firmware has no Language Core at all, so turning the switch on opened
  Galaxy Store, which refuses the S23 with "not compatible (9908)". (The 1.0.00.38 stub seen during
  testing came from a test overlay of S24 system apps; after a clean reboot the package is gone.)
- Fix: install the real Language Core directly. **4.0.01.24** (versionCode 400124010, the Snapdragon S26
  build from APKMirror, 3.1 GB, md5 `994620f3071706391d17ff132ea16c42`) is Samsung-signed (SHA-256
  `34df0e7a…5f900a42`, the S24 stub's cert too) and installs as an ordinary app.
- **This is persistent, unlike the other tweaks:** it's a /data/app install that survives reboot. Undo with
  `pm uninstall com.samsung.android.offline.languagemodel` (`pm uninstall-system-updates` crashes with a
  NullPointerException, because on the S23 it isn't an update of a system app).
- No Galaxy Store spoofing is needed. The Store checks `deviceId` server-side, and the stub-download service
  answers "Application is not approved as stub" even for the S24 Ultra.

### Why the summary and replies fail

- Every LLM step (caller/purpose summary, replies, Lite mode's `ToneConversion`) runs Samsung's on-device
  LLM inside Language Core. Each build carries it as a QNN context binary for one NPU: 4.0.01.x V81 (S26
  Snapdragon), 3.1.x Exynos `.nnc`, 3.2.x MediaTek `.dla`. No build targets the S23's V73.
- On the S23 it fails safely, before any model reaches the DSP. libQnnHtp picks the stub by SoC, and the APK
  ships only `libQnnHtpV81Stub.so`; the vendor's V73 stub is outside the app's linker namespace:
  `QnnDsp: Failed in loading stub: libQnnHtpV73Stub.so not found` → AI Core `LlmWrapperService` error 8400
  "Language core is closed by system". No CDSP crash. (Contrast: the weather model crashed the CDSP because
  AI Core bundles its own V73 stub.)
- Its SSNeural allowlist does include SM8550, so it isn't a chip-check rejection.

### Paths not taken

- InCallUI test mode (`key_boolean_test_mode_master` + `key_preference_list_call_screening_solution`:
  1 on-device, 2 on-device+cloud, 3 Gemini, 4 Gemini advanced, 5 Lite) can route the LLM step off-device.
  Mode 2 posts transcripts over plain HTTP to `43.202.243.81:7002` (Samsung internal test server); modes
  3/4 call Gemini with an API key embedded in InCallUI. Not used: the first is unencrypted and goes to an
  unknown host, and the second uses Samsung's credentials.
- A replacement-model route is possible but wasn't built: an LSPosed hook on InCallUI's
  `CallScreeningOnDeviceAdapter` answering with a V73-compiled model (Qualcomm's Llama 3.2 3B for SM8550
  via Genie) or a CPU model.

## Front-camera 10-bit (tenbit)

Android only accepts a 10-bit stream (`OutputConfiguration.setDynamicRangeProfile(HLG10)`) from a camera
that lists `REQUEST_AVAILABLE_CAPABILITIES_DYNAMIC_RANGE_TEN_BIT` (18) and a dynamic range profile map. On
S918WVLU8GZIF the rear cameras (0, 2, 20, 21, 23, 52, 54, 56, 58) list both; the front ones (1, 3, and the
secure-only 4 and 90) list neither, although Samsung Camera records HDR10+ selfie video through its own
vendor path.

### Where CamX drops them

All three come from the static-metadata builder in `/vendor/lib64/hw/camera.qcom.so` (a large tag switch
that the symbols mislabel `CamX::Stabilization::ExecuteStabilization`, from 0x16887d0). It fills a local
camera-info struct at `x29-0xb0` whose first field is the facing (1 = front, as the `LENS_FACING` case at
0x1688b3c shows).

- **Profile map** (`REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP`): the case branches past the map for a
  front camera at 0x168aa48. The fix is a `nop` there.
- **Recommended profile**: also skipped for a front camera, at 0x168a9a4. The fix branches to the common
  path at 0x168de98.
- **Capabilities**: the per-sensor list (count at caps+0x13f8, at most 20) has no 18 for front sensors.
  The list is copied byte by byte into `x19+0x2060`, then written with tag 0xc000c. The fix replaces
  `mov w28, w10` at 0x1690234 with a branch to 15 instructions placed in the 1,120 zero bytes after `.plt`
  (0x1945ba0, still inside the executable segment's last page). For a front camera whose list has at
  least 2 entries and room for one more, they append 18, then do the replaced `mov` and jump back.
  Cameras 4 and 90 list only `SECURE_IMAGE_DATA` and are left alone.

### Finding them on any build

The tweak doesn't use fixed offsets. `tb_find` scans the driver word by word (`od | awk`, about 14 s on
the phone, once per driver version) for each spot's instructions. Every word must match exactly except
the PC-relative ones (`b.cond`, `adrp`), so the struct offsets and registers the cave relies on are
confirmed by the match. Each spot must match exactly once. The cave goes right after the executable
`PT_LOAD`'s file contents, and must fit in that segment's last 4 KB page and be all zeros. The 2023
driver (One UI 6, S918WVLS2BWKA) matches all three at different offsets (0x1686124, 0x16861c8,
0x168b9b4, cave 0x1941598), and its patched copy disassembles correctly. After the bind, the tweak
checks `dumpsys media.camera` for a front camera with TEN_BIT and goes back to stock if there's none.

A first attempt kept the old copy of the library mapped while the bind changed underneath it. The
restarted camera driver then failed `hw_get_module` (-2) and crash-looped until the bind came off.
Stopping the service (`setprop ctl.stop sec-camera-provider`) before touching the bind avoids that.

### Tested (S918WVLU8GZIF, 2026-11-23)

- `dumpsys media.camera`: cameras 1 and 3 list TEN_BIT with map `[2 0]` and recommended profile 2.
  Cameras 4 and 90 are unchanged.
- A Camera2 test app (HLG10 on both a P010 `ImageReader` and an HEVC encoder surface) on cameras 1, 3
  and 0: the session configures, P010 frames carry true 10-bit samples (the low 2 bits are set in about
  75% of samples; an 8-bit source would give 0%), and the MP4 is HEVC Main 10, BT.2020, ARIB STD-B67
  (HLG).
- Samsung Camera: rear photo, front photo and front HDR10+ video still work. No driver crash.
