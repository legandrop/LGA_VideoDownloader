# Platform Differences - LGA Video Downloader

How the download tools, browser sessions and app updates behave on each platform.

## Tools (yt-dlp, deno, ffmpeg)

| | Windows | macOS |
|---|---|---|
| Bundled copy ("seed") | `<app>/tools/` (`ffmpeg.exe` + DLLs; `yt-dlp.exe`/`deno.exe` only if the build copied them) | `LGA Video Downloader.app/Contents/MacOS/toolsmac/` |
| Auto-updated copy | `<app>\tools\` — the same folder, updated in place (fallback: `%LOCALAPPDATA%\LGA\VideoDownloader\tools\` when the app folder is not writable) | `~/Library/Application Support/LGA/VideoDownloader/tools/` (never inside the bundle: writing there breaks the signature) |
| Resolution order (yt-dlp, deno) | updater folder → seed | updater folder → seed → Homebrew/PATH |
| ffmpeg | seed only | seed → Homebrew/PATH |

- `ToolsUpdater` checks GitHub at startup for yt-dlp and deno: the tag is resolved once from the `releases/latest` redirect, the binary is streamed to disk and verified against the release SHA-256 file (no hash, no install).
- Verified binaries wait in `tools/.staging` and are swapped in only when no yt-dlp process is running (at startup, before each download, or when the update ends with an idle queue).
- ffmpeg is not auto-updated.
- **Windows:** the installer seeds `yt-dlp.exe`/`deno.exe` only if they are missing, so installing a new app version never overwrites a newer tool; uninstalling removes `{app}\tools` and `{app}\session-cookies` whole. Tools left in `%LOCALAPPDATA%` by older versions are moved into the app folder at startup (only the newer ones) and that folder is then deleted.
- Session cookie files from the extension follow the same rule: `<app>\session-cookies` on Windows, Application Support on macOS.
- Downloads added while the tools are still installing wait in the queue and start by themselves.
- YouTube needs deno: its path is passed with `--js-runtimes deno:<path>`.

## Browser sessions (Use cookies from)

- The app never asks for a username or password. It passes `--cookies-from-browser <browser>` or `--cookies <cookies.txt>`; with **None** no cookies are used.
- Only installed browsers are listed (detected by their profile folder).
- **Windows:** Chrome, Edge, Brave, Opera and Vivaldi use app-bound encryption and yt-dlp cannot read them. They are listed disabled ("not supported on Windows"); Firefox is marked recommended.
- **macOS:** Safari, Firefox and the Chromium browsers can be used.

## App updates

- `UpdateService` reads the latest GitHub release of `legandrop/LGA_VideoDownloader` and requires the asset hash in its `SHA256SUMS`.
- **Windows:** downloads `VideoDownloader_Setup_v<version>.exe`, verifies it, stops the queue (killing yt-dlp and its children) and runs the installer silently over the same folder; the installer reopens the app.
- **macOS:** "Update" opens the release page.
- Development builds can check for updates but refuse to install.

## Process control

- **Windows:** cancelling kills the whole yt-dlp tree with `taskkill /T /F` (the onefile launcher runs the real yt-dlp as a child, which runs ffmpeg/deno).
- **macOS:** yt-dlp is started in its own process group and the group is killed.
- After cancelling, the partial files of that download are removed.

## Linux

Not packaged. The code builds, but tools are taken from PATH and there is no auto-update.
