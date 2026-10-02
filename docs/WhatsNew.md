---
product: LGA VideoDownloader
release_repo: legandrop/LGA_VideoDownloader
tech_changelog: ChangeLog.md
version_heading: "v{v}:"
platforms: [win, mac]
---
# What's new in LGA VideoDownloader
<!-- Editable while a version is unpublished. NOT append-only. Published versions are frozen. -->

## Unreleased

## v0.96
- [new] VimeoDownloader is now LGA VideoDownloader, a rebuilt app that downloads from YouTube, Vimeo, SoundCloud, Instagram and most other video and audio sites.
- [new][win] A browser extension for Chrome, Brave and Edge sends the page you are watching to the app with one click, along with your login, so private and members-only videos download without exporting cookies. Help explains how to install it.
- [new] Paste several links at once, or any text that contains them: each link becomes a card in the queue with its own progress, cancel and retry.
- [new] Choose between MP4 video or audio only (M4A); the default "Most compatible" quality gives H.264 files that open in any editor.
- [improved] Private videos no longer need a Vimeo username and password: the app uses the session of a browser where you are already logged in, or a cookies.txt file, and the old saved password is deleted.
- [new][win] The app checks for new versions and installs them for you in one click.
- [new][mac] The app tells you when a new version is available.
- [new] Update notices show what's new in each version, and Help has the full history.
- [new] yt-dlp and the other download tools update themselves in the background, so sites that change keep working without reinstalling.
- [improved] Errors explain what went wrong and how to fix it, for example when a video needs you to sign in or a site isn't supported; live streams and scheduled premieres are skipped with a clear message.
- [improved] Cancelling a download, or closing the app, removes its partial files.
- [new] The log is always visible at the bottom, with filters and a Copy button.
- [new] Help shows the app version, updates, the versions of the download tools and the credits.
- [improved] The window fits small laptop screens without overlapping controls.
- [improved][win] Installing removes the old VimeoDownloader and keeps your download folder, and everything the app downloads lives inside its install folder, so uninstalling removes it all.
- [improved][mac] The app is now called "LGA Video Downloader", installs from a DMG and no longer needs anything else installed on the Mac.
- [improved] New app icon, matching the other LGA apps.
