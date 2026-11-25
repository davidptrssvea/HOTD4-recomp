# HOTD4 Local Builder

## Audio skipping test (v1.3.25-audio-test.1)

Download the [Windows audio test builder](https://github.com/davidptrssvea/HOTD4-recomp/releases/tag/v1.3.25-audio-test.1), extract it separately, close HOTD4 and run **Setup.cmd** with your same supported arcade dump and existing installation folder. Settings and saves are retained. This prerelease fixes a reproduced streaming-audio cursor defect; the user's audible skips still need confirmation. It is installed manually because the stable updater excludes prereleases. The launcher retains its v1.3.24 base-version stamp for stable-update compatibility. See RELEASE-NOTES.md for the exact test and validation limits.

## Gamepad

In the Windows desktop launcher, choose **Gamepad (P1 + P2)** in **Settings → Graphics / Input** and save. The first controller is assigned to P1 and the second to P2. Either stick aims. **RT / A** shoots, **X / LB** reloads, **LT / B** throws a grenade, **Y / RB** changes fire mode and **Menu / Start** joins. Click the left stick to insert a credit. **View / Back** opens the pause menu, where the D-pad navigates and **A** selects.

Download and extract **HOTD4-Local-Builder-v*.zip** from the [latest release](https://github.com/davidptrssvea/HOTD4-recomp/releases/latest). Run **Setup.cmd**, select your own extracted arcade dump, then wait for the local build. Open **HOTD4-PC\Play HOTD4.exe** for the graphical menu or **Model Viewer.exe** to go directly to the viewer. All menus are in English.

To update, extract the new builder and run `Setup.cmd -InstallDir "C:\Path\To\HOTD4-PC"`, using your existing installation path. Select the same arcade dump when prompted. This keeps your settings, cabinet saves and trophies.


## Keyboard aiming (v1.3.17)

Choose **Settings > Graphics / Input > Input Mode > Keyboard (P1 + P2)**, save, and restart the game. Open **Settings > Key Bindings** to change either player's movement and action keys. The in-game pause menu also provides Key Bindings (PgUp/PgDn switches pages).

| Action | Player 1 | Player 2 |
| --- | --- | --- |
| Aim | Arrow keys | WASD |
| Shoot | Right Ctrl | Space |
| Reload / shake | Right Shift | Q |
| Grenade | Right Alt | E |
| Fire mode | End | Tab |
| Coin / Start | 5 / 1 | 6 / 2 |

Keyboard mode keeps the two seats independent. Use P2's Coin and Start to play solo from P2, or join both players for co-op. Movement is frame-rate independent and stops when the game loses focus or opens the pause menu. The keyboard-specific Service key defaults to F10 so S remains available for P2 aiming. Mouse and lightgun modes retain their existing controls and profiles.

## Display options (v1.3.16)

In **Settings > Graphics / Input**, choose **V-Sync > On** to synchronize desktop presentation with the display. Save and restart the game to apply. Off remains the default. The desktop setting does not alter OpenXR headset timing.

On the main **Settings** page, choose **Aspect Ratio > 4:3** (below Display Mode, next to HUD) for a 4:3 game canvas, including 640 x 480 and 800 x 600 outputs. Wider displays show side bars. The game, HUD and pointer use the same fitted picture area. 16:9 remains the default. Save, exit the game and relaunch to apply an aspect-ratio change. This is a desktop option; OpenXR keeps its headset view.

## Co-op result panel fix (v1.3.13)

Chapter result screens and the final player ranking now keep each player's scores, accuracy, critical-shot counters, portraits and rating pieces aligned with their panel. This fixes the reported P1 misalignment after chapters 5 and 6. Result screens retain their original layout regardless of the gameplay HUD scale, including supersampled output. All v1.3.12 features are included.

## FAQ

**What can I delete after installing?** You can delete the downloaded ZIP, the extracted builder files and the build-tool cache at `%LOCALAPPDATA%\HOTD4-Builder`. If you used the default install path, `HOTD4-PC` is inside the extracted builder folder. Leave that subfolder in place when cleaning up. The next Setup will download the build tools again if you clear the cache.

**What should I keep for future updates?** Keep your original extracted arcade dump and your installed `HOTD4-PC` folder. Your settings, cabinet saves and trophies are stored inside `HOTD4-PC\HOTD4_Recomp`. Point the new builder at the existing installation with `-InstallDir` and select the same dump when prompted.

**Setup reports `target not found: mingw-w64-x86_64-python-pyelftools`.** Use v1.3.4 or newer. MSYS2 removed that dependency from its active MINGW64 repository; this installer obtains the pinned ELF reader automatically. Extract the new builder, run `Setup.cmd` with the same dump, and select your existing install folder if updating. No manual script edits or separate Python installation are needed.

**Setup reports `Build-tools download failed its SHA-256 check`.** Setup v1.3.11 retries failed downloads and tries the official mirror automatically. If all attempts still fail, the downloaded archive differs from the pinned file. You can download [the exact MSYS2 archive](https://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20260927.sfx.exe) in your browser, save it as `msys2-base-x86_64-20260927.sfx.exe` in `%LOCALAPPDATA%\HOTD4-Builder` (the default tool cache), then rerun Setup. Setup verifies and reuses a matching archive. The verified file is 43,117,824 bytes, SHA-256 `AD336CCCFDA47758B5E15CDA993FBBA421115CB0B126697DAEF1EE4DFE37209F`. If it still fails, report your builder version and the downloaded file's name, byte size and SHA-256.

## Supported arcade dump

Starting with v1.2.2, Setup accepts your own complete, extracted **The House of the Dead 4 (Export), Rev A (DVP-0003A)** Sega Lindbergh dump. It prepares the HD ELF locally from the stock executable. The previously supported HD ELF can also be used directly.

| Supported ELF input | SHA-256 |
| --- | --- |
| Original Rev A (DVP-0003A) | `65EC92C55EF48AD5EA565DFDD40FF43759DAACFA2CEBD6D5B83CE0368F9BFE14` |
| Previously supported HD variant | `72FDD45830ACD9317EC1E4F97DE35A270C94746068FDAF827381C91BB47AA296` |

Select the extracted folder containing either of these layouts:

- `elf/hod4M.elf`, `fs/` and `drv/` (often under `disk0`).
- `hod4prog/hod4M.elf`, `hod4data/` and `hod4drv/`.

Setup verifies the input hash and checks that the locally prepared copy matches the expected HD hash before building. Setup leaves the original dump in place. It expects extracted files on disk.

If `hod4M.elf` has an unsupported hash, Setup checks for `hod4Ms_HD.elf` in the same folder and uses it if it matches the HD SHA-256 above. Leave both files under their original names. Setup writes the verified HD executable as `hod4M.elf` in the installation folder.

## System requirements

- **Operating system:** Windows 10 or Windows 11 on an x64 (Intel or AMD) PC, with Windows PowerShell 5.1 and the Windows .NET Framework 4.x compiler available. The v1.3.1 clean setup was verified on Windows 11 x64. An earlier installer was verified on Windows 10 Enterprise LTSC 2021 x64 (build 19044.1288).
- **Disk and permissions:** Allow around **10 GB of free space** beyond your existing dump for the toolchain, build files and installed copy of the game. Use writable folders for the extracted builder and game installation. The toolchain uses `%LOCALAPPDATA%\HOTD4-Builder\msys64` by default, so allow space there as well if the builder is on another drive. Setup can run under a regular user account.
- **Internet:** Required for the first setup to download MSYS2 and its build dependencies. Setup installs its own compiler, Python and runtime dependencies. A separate xz tool is unnecessary. The pure-Python ELF reader is downloaded from PyPI/files.pythonhosted.org with a pinned SHA-256 and kept inside the HOTD4 build-tool cache.
- **Graphics:** Desktop gameplay and the optional menu background use a GPU driver with desktop OpenGL compatibility support. The desktop build requests an OpenGL 2.1 context with the extensions it uses for rendering. The installer can compile on a VM without working 3D graphics. No minimum CPU, RAM or GPU for gameplay has been benchmarked.
- **Optional OpenXR VR:** A compatible PC VR headset, tracked controllers, an active OpenXR runtime with OpenGL support, and a driver that can create an OpenGL 4.3 compatibility context.

## What happens on your PC

1. Verify this package and your ELF.
2. Download the official self-extracting MSYS2 archive, verify its pinned SHA-256, and install signed compiler/runtime dependencies using pacman. Install the pure-Python ELF reader in the isolated cache from a SHA-256-verified PyPI wheel, without pip or changes to system Python. The archive extracts without a system xz utility.
3. Prepare the HD ELF from your copy, lift it to C locally, generate OpenGL bindings, and compile the Windows executable.
4. Copy your dump into the chosen installation folder and add the launcher, runtime DLLs, menus and original project artwork. The source dump stays unchanged.
5. Run the locally built game in a hidden window with isolated temporary settings, muted audio and fresh temporary cabinet state. Render the Chapter 1 zombie scene at 1920x1080 and save it locally as the menu background. This usually adds about one minute. Your controls, trophies and cabinet saves are untouched.

The first build can take several minutes and uses multiple gigabytes of temporary disk space. Later runs reuse the tools and unchanged generated code. Build tools default to `%LOCALAPPDATA%\HOTD4-Builder\msys64`. Game settings, cabinet NVRAM and local trophies live in the installation's `HOTD4_Recomp` directory; rerunning setup preserves them. Existing game data is refreshed from the selected dump.

## Included features

Resolution selection up to 4K, chapter practice, HUD scale, native pause menu, Trophies gallery with 12 base-game goals, Sinden border with selectable thickness, crosshair style/size/color previews, Raw Input device selection, and selectable MSAA. The default Pause key is Esc; it can be changed. This Windows release also includes Photo Mode, the animated Model Viewer and locally enhanced character textures in the viewer.

This package contains hand-maintained source, build tools/scripts, procedural crosshair previews and newly generated metallic star artwork. It contains **no arcade ELF, game assets, game movies, game audio, textures from the game, prebuilt game executable, lifted game code, saved progress or cabinet NVRAM**. The game is never uploaded. Only the build tools and their dependencies are downloaded.

Choose **Settings > Crosshair Style > Name Entry (Original)** to use the game's original red P1 / blue P2 crosshairs, including the idle pulse and shot animation. The launcher reads the sprite textures, crop rectangles and animation timelines from your own `fs/test/playerranking.bnk` and creates a local cache. Size, color, animated settings preview and the in-game pause selector are supported. The existing styles remain available. Generated `native-crosshair.bin` files belong to the user's installation and are never included in the downloadable builder.

The menu background is created on the user's PC from their own dump. No screenshot or game artwork is included in the distribution. Existing menu backgrounds are preserved on reinstall. If the optional render fails (for example without working OpenGL graphics), installation completes with a plain background and a local menu-background.log; rerun Setup to retry.

## Start directly without the menu

Create a shortcut with this target (using your installation path):

```text
"D:\Games\HOTD4-PC\Play HOTD4.exe" --no-launcher
```

This starts Arcade directly with your saved resolution, display mode, HUD scale and other settings. The temporary ELF is prepared automatically and cleaned up on exit. Returning to Main Menu exits to the desktop in this mode. Launch without the argument to open the normal graphical menu. `--skip-launcher` is an alias; both flags also work with `Play HOTD4.cmd` and the performance-logging CMD file. Legacy numeric CMD chapter arguments remain available.

## Photo Mode (Windows desktop)

Pause during gameplay and select **Photo Mode**. The game stays frozen as you move the camera through the rendered 3D scene.

- **Right-drag** to look around, and use the **mouse wheel** to zoom.
- **Shift + right-drag** sideways to roll the camera. **Z / C** also roll it left or right.
- **WASD** moves the camera. **Q / E** move down or up, and the **arrow keys** also look around.
- **R** resets the camera. **H** toggles the original arcade HUD, which starts hidden in Photo Mode.
- **F** cycles **Original**, **Black & White**, **Film Noir** (grain and film scratches), and **High Saturation**. **Shift + F** cycles backwards. The selected filter appears in the preview and the saved image.
- **Enter** saves a full-resolution PNG in **Photos** beside the launcher. **Tab** hides or shows the guide, and **Esc** returns to the pause menu. Press **F1** to expand the compact guide into the full control list.

The PNG excludes the photo guide, recomp crosshair, trophy notifications and Sinden border. Scenes without a live 3D camera can still be captured from the frozen view.

## Model Viewer

Choose **Model Viewer** in the start menu or open **Model Viewer.exe** from a current installation folder. Search by character name, model filename or bank, drag to rotate, scroll to zoom, and double-click to reset. The viewer reads static meshes, original diffuse textures and alpha directly from the local model and texture banks. The preview uses fixed lighting, and surfaces without an available diffuse texture use the model's material color. The list starts with **Main models**, which shows one primary mesh per character family where the banks identify one. Alternate meshes, level geometry and props remain searchable. Named characters show their archive filename beneath the name. **HD textures** upscales and sharpens the original diffuse textures for James Taylor, Kate Green and Justice locally. The viewer also uses matching replacements from the optional local HD test pack. Other surfaces use the original textures. Choose **Original textures** to compare.

Filter the catalogue by **Enemies**, **Bosses**, **Players**, **Weapons**, **Environments**, **Props & Items**, **Effects & UI**, or **Other**. The viewer preselects a clip when one is available, with idle and walking clips preferred. Press **Play** to start it or choose another clip from the list. The animation controls are hidden for models without clips. Use **Pause**, the timeline, **Loop**, or **Bind pose** to inspect individual frames. The viewer reads original skeletal motion keys, joint hierarchy and skin weights from the same local dump. Clips are matched using archive names and compatible joint layouts. The preview follows the model as it moves.

The bank and Sega NN format layouts were cross-checked against [Sega NN Tools](https://github.com/Argx2121/Sega_NN_tools).

## High Scores

The in-game display shows your current **P1 TOTAL**, a separate **P2 TOTAL** when the second player joins, and **HI-SCORE** from the arcade's own saved ranking. Totals accumulate across all six chapters of the current run; they are not separate records for each chapter. Chapter practice is labelled **P1/P2 PRACTICE** instead. Open **Pause > High Scores** to see all ten places with their original initials and scores. **Pause > Settings > Score Display** toggles the small in-game display; the choice is saved.

Scores follow the original game rules: qualify for the ranking and confirm your initials when prompted. The arcade saves that ranking in the cabinet NVRAM, which survives normal restarts and installer updates. Chapter practice uses a temporary cabinet copy and does not replace your arcade records. The display reads the existing ranking; it does not add a separate score database or alter scoring rules.

## Key bindings and controls overview

Open **Settings > Key Bindings** to change Add Coin, Start, Shoot, Reload / Shake, Grenade, Fire Mode, Test Menu, Service, Mute, Pause, Restart, Fullscreen, Toggle Crosshair and Mouse Lock. P1 and P2 have separate coin/start bindings. Click an action and press the desired key, then choose **Save & Back**. Mouse buttons can be assigned to the four shooting actions. Duplicate assignments are rejected. **Reset Defaults** restores the factory keys; save to apply.

The start menu displays all current controls, including Add Coin and Start. The chapter-selection hint also follows the selected Restart key. In the native pause menu, open **Settings > Input & Anti-Aliasing > Key Bindings**; changes there apply and save immediately. Esc goes back inside menus or cancels a pending capture; use Cancel when binding Esc to Pause.

In **Settings > Input & Anti-Aliasing**, select **Mouse (Raw Relative)** to adjust **Mouse Speed** (0.50x to 8.00x) and **Mouse Smoothing** (Off, Low, High). Both settings update live from the pause menu.

In either mouse input mode, shake the mouse back and forth to escape zombie grabs and other struggle events. A straight sweep or small jitter does not count. During a grab, the Reload and Fire Mode buttons do not substitute for shaking. Outside grabs, Reload works normally. Physical lightgun controls and OpenXR controller gestures keep their existing behavior.

Profiles are stored locally in `HOTD4_Recomp/controls-profile.txt` and survive restarts and reinstalls. Once a custom profile is saved, legacy coin/start aliases and Alt+Enter are disabled so that the overview lists the actual bindings. Keyboard bindings work in all input modes. In Raw Lightgun mode, the selected lightgun's trigger/buttons keep their direct device mapping; mouse bindings apply to mouse input modes. Menu navigation remains arrow keys, Enter and Esc.

## Gameplay options: lightgun reload and blood

Open **Settings > Gameplay** in the launcher or the desktop pause menu.

- **Lightgun Off-Screen Reload** defaults to **On**. Point outside the play area and pull the trigger to reload that player's gun, without firing an on-screen shot. P1 and P2 are handled independently. **Off** suppresses this off-screen trigger action; the ordinary Reload button still works.
- Select **Lightgun (Raw Input)** and assign the appropriate devices in Input settings. The game needs an off-screen position from the gun driver. If Sinden/DolphinBar software retains the last on-screen position when tracking is lost, configure its off-screen trigger to send **Reload / right mouse button** instead. The game cannot infer tracking loss from a frozen coordinate. Keep the Sinden border enabled if your gun requires it. Normal relative-mouse aim remains bounded to the screen.
- **Blood Splatter** defaults to **Original**. **Enhanced** uses a stronger original body-hit emitter variant with a modest scale increase. Head, dismemberment and special/boss effect selections are preserved. These effects come from the user's own dump. Testing confirmed that blood effects already run in the original game; this is an optional enhancement, not a verified restoration of cut content.

In VR, the headset settings menu has separate **Off-screen reload** and **Blood splatter** choices, saved in `vr.cfg`. For VR reload, aim the controller outside the game's aim area and pull the trigger. Grip reload remains available.

## Parsec co-op: host lightgun + guest mouse (experimental)

Choose **Settings > Input & Anti-Aliasing > Co-op: Gun P1 + Mouse P2**. Select the host's Sinden or DolphinBar under **Player 1 Device**. Explicitly select a relative device such as a DolphinBar; **Auto** only assigns absolute guns. Player 2 uses the guest's injected Windows mouse automatically. The P2 raw-device selector is hidden in this mode. Enable the Sinden border when needed.

The host gun's buttons control P1. The guest mouse uses the configured Shoot / Reload / Grenade / Fire Mode bindings for P2; the shared keyboard bindings for these four actions also control P2. Coin and Start retain their separate P1/P2 bindings (defaults: **5 / 1** for P1, **6 / 2** for P2). P2 has a separate sight and a contrasting color. Choose **Co-op: Gun P1 + Mouse P2 (Relative)** for a separate relative cursor with adjustable speed and smoothing. Restart after changing input mode or device assignments.

In Parsec, allow the guest keyboard and mouse access and disable **Exclusive Input**, then restart the stream. See [Parsec's input troubleshooting guide](https://support.parsec.app/hc/en-us/articles/32381827815188-Mouse-and-Keyboard-Isn-t-Working-Correctly-When-Connected). A host lightgun which only controls the shared Windows cursor cannot be separated this way: it must expose its own Raw Input device. The Parsec virtual mouse placeholder is not a P2 gun to select.

This mode separates the selected raw gun from injected remote mouse packets. Local checks cover packet routing, two-player game input and cursor calculations; a real two-computer Parsec session with Sinden/DolphinBar still needs validation. Other remote software or virtual HID drivers which present guest movement as ordinary physical hardware may need a different input path.

## Optional OpenXR PC VR

**VR remains experimental.** The optional OpenXR build in v1.3.8 includes the earlier stereo/X-button fixes plus session teardown, sequential Reset Game relaunch, persistent scene-composite filtering and cutscene pacing improvements. Local tests use native graphics and a simulated runtime; **Quest 3 / Virtual Desktop (VDXR and SteamVR) testing is still needed**.

Select **Yes** when Setup asks whether to build the OpenXR version. For unattended setup, pass `-OpenXR`. Setup builds a separate VR executable from the same local dump and installs **Play HOTD4 (VR OpenXR).cmd** next to the desktop launcher.

Connect your PC VR headset and start an OpenXR runtime that supports OpenGL before using the VR launcher. Aim with a tracked controller, pull the trigger to shoot, squeeze the grip to reload and click the thumbstick to start. VR uses its own headset menu, controller bindings and cabinet save.

To update, extract the current release ZIP and run **Setup.cmd** again with your existing extracted arcade dump and installation folder; select **Yes** for OpenXR. The builder compiles the new VR executable locally and preserves existing profiles and cabinet saves.

The VR launcher writes a separate session log under **HOTD4_Recomp\VR Logs**. Please test each eye separately, confirm there is no extra gameplay rectangle over the scene, and press X during gameplay. Include the log and your headset/runtime/GPU when reporting results. For Virtual Desktop, mention VDXR or SteamVR. Please also check Alt+F4 and relaunch, the glass-walled Chapter 1 corridor, and cutscene timing. Orientation and gesture reports remain open; no physical-headset FPS or compatibility claim is made.

The X-button render-target dump requires the developer switch `LINDBERGH_XR_DEBUG_CAPTURE=1`; leave it unset for normal play. Enabling it intentionally causes GPU readbacks and file writes.

## Lower GPU load

On the desktop, select **960 Ã— 540** or **1280 Ã— 720** under **Settings > Render Resolution**. **Display Mode** independently controls whether the game uses a window or borderless fullscreen. Turn MSAA off under **Settings > Input & Anti-Aliasing** and restart the game to apply it.

**Render Resolution** accepts presets or a size you enter, such as **2560x1440**. **Desktop** uses the screen dimensions. The game fits its 16:9 picture within the window or desktop, with rendering capped at 3840 Ã— 2160. Ultrawide screens show centered gameplay with side bars.

## Recording the Chapter 1 elevator slowdown

1. Run **Play HOTD4 (Performance Log).cmd** from your installed game folder.
2. Use the settings you want to test. For the reported elevator slowdown, select 1080p (Desktop resolution on a 1080p display) and turn MSAA **Off** under **Settings > Input & Anti-Aliasing**.
3. Choose **Chapter Select > Chapter 1** and play through the elevator fight. If FPS drops, finish the fight or stop just afterwards, then use **Pause > Main Menu** and **Quit** so the game writes the recording.
4. Open **Performance Logs** in your installation folder and share the newest CSV. The launcher opens the folder when it closes. Mention where the slowdown started, your CPU and GPU, graphics settings, and whether you were playing in fullscreen. GPU utilization during the dip is useful if you already have it.

Each game run writes a separate CSV with frame numbers and `frame_ms`, the time in milliseconds between completed presents. The file records timing data only. Long pauses appear as gaps in the timeline.

## Arcade operator Test Menu (Windows desktop)

Press your **Test Menu** binding (default **T**) to enter the arcade's original operator program. Entering Test Menu ends the current game. **Service** (default **S**) moves the selection; **Test** confirms a selection or changes a value. Open **Game Assignments** to change difficulty, initial lives and maximum lives. Select **EXIT** in the submenu, then **EXIT** on the main operator menu to save and restart the game. Custom Test/Service key bindings apply here too. Settings use the same local cabinet NVRAM as the game; chapter practice refreshes its temporary copy after returning.

Setup builds the operator program locally when your dump includes a verified `hod4testM.elf` or `hod4testM_HD.elf` alongside the game ELF (SHA-256 `156761ef42b71ec00872867dfd7bb4ad88318a148a131f302dd60f4bc49135bb`). If it is absent or unsupported, normal installation can still complete and Setup reports that Test Menu is unavailable. Re-run Setup with the complete Rev A dump to add it. Missing arcade boot fonts are generated locally from Windows system fonts. No operator executable, font atlas or other game data is distributed in this package.

## Error reports

If the game or Model Viewer exits with an error when started through its EXE launcher, the complete error text is saved under **Crash Logs** beside the launcher. The error dialog shows the report path and a shortened preview. Include the full `.log` file when reporting a crash; a screenshot of the dialog can omit the cause.

## Command-line options

```powershell
.\setup.ps1 -DumpPath 'E:\My dump\disk0' -InstallDir 'E:\Games\HOTD4' -Jobs 4
.\setup.ps1 -DumpPath 'E:\My dump\disk0' -ValidateOnly
.\setup.ps1 -DumpPath 'E:\My dump\disk0' -NoDownload
```

`-ToolchainPath` selects another isolated MSYS2 directory. `-NoDownload` requires a previously initialized toolchain. Use a writable installation directory. Keep the full extracted builder package together. **Do not redistribute the locally built game folder, generated source, build directory or copied dump.** To share the builder, use the original distribution ZIP.

## Credits and licenses

Based on lindbergh-recomp-ports, lindberghrecomp and pcrecomp by sp00nz and contributors (including the lialan ports). MIT license notices are retained in source. Runtime dependency license notices are copied from the downloaded toolchain into the local game's licenses directory. dlmalloc's public-domain notice is in its source. Metallic stars are newly generated project artwork; crosshair previews are rendered from our procedural drawing code. System fonts are used without bundling font files.

The House of the Dead 4 is a Sega property. This independent local build project provides no game content and is not affiliated with Sega or PSN. Local trophies do not synchronize with PlayStation accounts.


Sinden border thickness is selectable in Settings and in Esc > Settings > Border Width. The default is Thin, 1.5% of screen height (half the previous 3%). Choices are 0.75%, 1.5%, 2.25% and 3% (original). The black inner edge scales too. The choice is saved in HOTD4_Recomp/sinden-width.txt and is applied immediately from the pause menu. Existing On/Off preferences are preserved; profiles without a saved thickness use 1.5%.

## Setup diagnostics and recovery

Setup verifies download size and SHA-256, retries interrupted or mismatched downloads, and tries the official GitHub mirror for the same pinned MSYS2 archive. Existing compiler executables, headers and Python imports are checked before a cached toolchain is reused. No checksum check is bypassed.

The default compiler worker count adapts to available RAM and CPU cores; use `-Jobs 1` to reduce memory pressure further. Long-running commands print a periodic working message. Setup checks writable/protected paths and reports available disk space before downloading; keep the free-space allowance listed in the system requirements. Concurrent installers using the same builder, destination or tool cache are blocked.

Each run writes a diagnostic log under `%LOCALAPPDATA%\HOTD4-Builder\Logs`. If Setup fails, send the latest log with the error report (review local paths/usernames before sharing). Rerun Setup to reuse verified downloads and repair missing tool packages. If the tool cache remains broken, run `Setup.cmd -RepairTools`: it preserves the previous recognizable MSYS2 folder under a `.backup-...` name and installs a fresh isolated cache. This needs internet access. Your original dump, installed settings and saves are retained.

## Game Library

Open **Game Library** in the main menu to choose between installed copies of HOTD4, HOTD4 Special, HOTD EX and Scarlet Dawn. **Play** starts the game using its saved settings. **Settings** opens that game's own settings, and **Menu** opens its regular launcher for features such as chapter selection. Older installations may offer **Menu** only. Setup registers each installation for your Windows account. The library also checks nearby folders and remembered Setup locations. Use **Locate installed game...** if an existing installation is missing.


## Solo character selection

In the desktop launcher, open **Settings > Gameplay > Solo Player** and choose **James (P1)** or **Kate (P2)**, then Save. The native pause menu exposes the same choice under Settings > Gameplay; restart Arcade/Chapter to apply a change made during play. Available in v1.3.12 and newer.

Your primary mouse or Raw Input lightgun follows the selected character automatically, including the sight, Shoot, Reload/Shake, Grenade, Coin and Start bindings. You can keep using the normal primary Start button (default 1/Enter). The primary Raw Input device is labelled for the selected character. Default James behavior is unchanged. Mixed Gun P1 + Mouse P2 modes retain their normal co-op assignments and disable the solo override. The choice is saved per installation in solo-player.txt. This is a desktop HOTD4 feature; it does not change Special or OpenXR input.

## Menu updates

The main menu checks GitHub for the latest stable release in the background. Open **Updates** to read the release notes and choose whether to download and install a newer version. Setup verifies the package and builds the update locally from the arcade dump in your installation. The menu reopens after installation.

The update replaces the launcher and runtime files. It keeps settings, cabinet saves, the existing game dump and installed OpenXR support. The update folder beside the installation holds the Setup log and the previous versions of replaced files. Internet access and free disk space for a local build are required.

To add the updater to an installation from an earlier release, run this builder's Setup and select the existing installation folder. Update `ui/hotd-update-version.txt` to match the tag when preparing a release.

## Remembered Setup folders

Manual Setup remembers your last successfully used **arcade dump** and **installation** folders, separately for HOTD4, Special and EX. Both paths are shown in the opening dialog; edit them or use **Browse** beside either field. Cancelled/failed builds and validation-only runs do not replace the last successful choices. Explicit command-line paths still take precedence, and the launcher updater remains automatic and does not change this history.

Folder history is stored locally in `%LOCALAPPDATA%\HOTD-Setup\Normal.json`, `Special.json` or `EX.json`. It contains paths only. A deleted/moved dump can be replaced using Browse; a corrupt history file falls back to the usual defaults. Settings and cabinet saves remain in your installed game folder.
