# 4T4R source adaptation ledger — base 710a53b

Plan: primary workspace docs/superpowers/plans/2026-10-12-4t4r-source-adaptation.md.

User approved implementation on 2026-10-12. Preserve algorithms; adapt necessary dimensions/parameters. Normal and calibration RF tables are pending; no flashable 4TX hardware package will be delivered with the old 2TX table.

Task 1: managed worktree created; branch codex/source-4t4r-adaptation. Primary 2T4R SDK untouched.

Task 1: baseline real-source 2TX pipeline and calibration PASS in the clean worktree.

Tasks 2/3/4: Four-TX parameter test first failed on 2TX/8VX. Real Flash-layout test first failed on 64-byte matrix; real CLI test of the baseline handler failed on its truncated 64-byte write. Green checks: 4TX waveform/16VX indices, 128 negative-spacing angles, 22 synthetic pipeline cases including all 8 subband boundaries, full 16-channel array calibration, real Flash 128-byte matrix roundtrip/legacy magic rejection, actual CLI 32float read/write/argument bounds.

Ruling: The baseline source has a compile-time 2TX-only guard although its processing loops use runtime numTxs. Expanded that guard to 2TX/4TX 506x128 only; algorithm body unchanged.

Ruling: Keep the existing interior-sample Hanning implementation (n+1)/(length+1). The initial generalized window test assumed a different Hann endpoint convention; corrected the test to check initialization/symmetry/span, without modifying the algorithm.

Ruling: The Git checkout lacks ignored .config.in and prebuilt SPL. Build stages use the previously verified primary SDK config and OTA SPL as explicit external inputs, record hashes, and modify the copied build config only. No primary SDK writes.

Task 6 (software): MIPS build PASS, 911508-byte RTOS (46956 bytes free in 0xEA000), exact16-channel defaults and128 theoretical angles verified; original algorithm/protocol source hashes preserved except detection compilation guard. Actual MIPS2TX/4TX shared struct sizes/offsets equal: (91172,112,36,308,11112,0,24,88,160,200,9472,252,66856,0,32). Two-TX source pipeline/Flash-layout regression PASS; RCS unit cases PASS.

Fresh read-only review found no firmware blockers. It identified test-script defaults still selecting a positive-spacing test angle table. Reproduced default run failure on angle assertion, corrected test flags to default4TX/theoretical and retain explicit TEST_TWO_TX=1 for baseline. No firmware algorithm change.

Normal4TX RF table supplied on 2026-10-14: vendor_bsis(2).c SHA256 matches the originally imported supplier file. Its 128x506 table is now selected in the four-TX build with FSTEP=0xAB. Pending: calibration-specific RF settings, final flash package and hardware validation.

Final software verification: default4TX invocation PASS with22 cases/full16-channel arraycal/invalid LAST channel rejection; explicit2TX invocation PASS with6 cases/full8-channel arraycal/invalid LAST channel rejection. Corrected default test entry point passes. All production source files still match the recorded MIPS snapshot; primary2TX vendor tree has no changes.

Pre-flight: Tasks 2/3 provide NUM_VX and geometry consumed by Task 4 matrix storage/CLI. Task 5 supplies RF tables consumed by live capture. Task 6 can test software independently of RF.

Ruling: Retain current CFAR/post/CRT/ego/tracking/RCS algorithms and thresholds. Default 4TX layout uses 8 DDMA bands/16 bins; test all boundary bins. Four-channel geometry uses user-confirmed negative spacing. Old 16-channel coefficients are defaults requiring new-waveform hardware validation.

Ruling: Four-TX RF readiness remains explicitly false while calibration-specific settings and hardware checks are pending. The updated diagnostic MIPS build contains the supplied normal four-TX table at 16.69921875 MHz/us but stops before configuring/capturing hardware; it is not a final burn package.

2026-10-14 RF slope: RED test confirmed no normal four-TX table was integrated. GREEN C-preprocessor checks pass for BPM on/off and combined ADC output; each branch has 93 rows, with only word0 bit31 and word1 bits12:0 of profile 0 changed. Normal-table FSTART, timing, ADC, TX, DDMA and BPM values remain identical. MIPS build PASS: 911764-byte RTOS, 46700 bytes free; actual linked register table matches all words. DSP/calibration algorithms and protocol source remain identical to commit 710a53b. Verifier now compares against that immutable Git base because the primary B 2TX tree advanced independently with an RCS default. The D-drive B 2TX vendor tree has no modifications.

2026-10-14 experimental live package, explicitly requested by user: SOURCE_4T4R_RF_READY=1; startup continues into normal RF/camera initialization. Phase-shifter parameters adapted to mask=0xF, 8 bands and phase cycles 1/8/4/8, preserving coarse/fine scan RF tables and calculation algorithms. Normal-waveform convergence check uses FSTEP=171 slope. Actual-template test RED on four-TX mask, then GREEN for both 2TX/4TX; full 22-case source pipeline and 16-channel array calibration PASS. MIPS build PASS (911860 bytes, 46604 bytes free); linked startup byte=1 and all 93 RF rows verified. Separate cfg MT4T4R_Source_16p699_16CH_256P32T_OTA_20261014.cfg and existing OTA SPL packaged; generic OTA checker PASS on both slots, boundaries, functions and source/file hashes. New cfg installed with matching hash, old Cloner spl/img and B 2T4R source unchanged. Pending: user hardware bring-up and calibration/performance validation; no board was flashed in this session.

2026-10-20 current state: Windows source synchronized to the delivered controlled-HS package. Startup/USB diagnostic instrumentation removed; V4 send mutex and ADC buffer fix retained. Tracked build configuration and source provenance added. See 4t4r_controlled_hs_20261020.md for the current version; earlier entries above describe historical stages. Hardware stability of this controlled revision is not yet verified.
