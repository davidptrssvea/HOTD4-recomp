# HOTD4 audio skipping test — v1.3.25-audio-test.1

Windows local builder prerelease for issue #22, based on stable v1.3.24. **No game assets, arcade ELF, generated game code or prebuilt game executable are included.** Setup builds the fix locally from your own supported arcade dump.

## What changed

Correct a streaming-audio playback cursor that could briefly report byte 65536 for a 65536-byte looping buffer. The mixer now wraps its published cursor at callback completion and synchronizes playback-position reads. Fractional sample phase is preserved; nonlooping effects retain their end behavior.

The invalid cursor was reproduced in a Chapter 1 run. A corrected 3,600-frame run completed with zero invalid positions in over 10,000 position reads. Boundary and phase tests cover 100,000 callbacks at each of 22.05, 44.1 and 48 kHz. Windows desktop and optional OpenXR targets build.

**This confirms the cursor correction, not that every reported audible skip is resolved.** The reporter's HyperX wireless/DTS output still needs confirmation. This is a test release, not the latest stable update.

## Install / update

1. Download and extract **HOTD4-Local-Builder-v1.3.25-audio-test.1.zip** into a separate folder.
2. Close the game and launcher. Run **Setup.cmd**, choose the same supported arcade dump, and select your existing HOTD4 installation folder. Settings, cabinet saves and trophies are preserved.
3. Let Setup rebuild locally, then launch normally.

The stable launcher updater does not offer prereleases. **Install this test manually.** Its launcher keeps the base v1.3.24 version stamp so future stable updates continue to work; identify this build by the audio-test ZIP filename and these release notes.

## Please test

Replay Chapter 1 on the same audio output/device settings and listen for the short music skips described in issue #22. Check that gunshots, voices and pause/resume still work normally. If the music still skips, share a short clip with the timestamp and the CSV from **Play HOTD4 (Performance Log).cmd**. No further chapter recordings are needed yet.
