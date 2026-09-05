/*  What a stranger downloads, and what they will see the first time they run it.
 *
 *  Per .ai/rules/README.md, website/src/content/ owns the CLAIM and never the
 *  argument: which file each platform takes, not why the release workflow is
 *  shaped the way it is. That is .ai/rules/release.md's job.
 *
 *  THE LINKS CARRY NO VERSION. GitHub resolves
 *  /releases/latest/download/<asset> server-side to the newest non-prerelease
 *  holding an asset of that name, so this page needs no API call - which it
 *  could not make anyway, since the site fetches nothing at runtime and
 *  api.github.com rate-limits by IP - and no generated file. It also cannot
 *  advertise a version whose build has not finished: the site deploys on a
 *  push and the three builds land after it.
 *
 *  The asset names are asserted against .github/workflows/release.yml by
 *  tests/download.test.ts, so a renamed artefact fails a test naming the file
 *  rather than leaving a button that 404s.
 */
import type { PlatformSystem } from '@/ui/Icon';
import { repositoryUrl } from '@/content/setup';

export interface Download {
  /** Which of the three systems this is for.
   *
   *  A closed key rather than a match against `platform`, which is free text a
   *  reader is meant to read - "Windows, portable" is two entries under one
   *  system, and a mark chosen by string search would be a mark that goes wrong
   *  the first time that text is edited. The page keys the platform mark, the
   *  per-system button and the grouping off this. */
  readonly system: PlatformSystem;
  readonly platform: string;
  readonly asset: string;
  readonly what: string;
  /** What the operating system says the first time. None of these builds is
   *  signed, so every entry has something to say here; tests/download.test.ts
   *  refuses one that does not. */
  readonly firstLaunch: string;
}

export const downloads: readonly Download[] = [
  {
    system: 'macos',
    platform: 'macOS 11 or newer',
    asset: 'dew-macos-universal.dmg',
    what: 'Apple Silicon and Intel in one binary. Open it and drag dew to Applications.',
    firstLaunch:
      'macOS will refuse it: “Apple could not verify dew is free of malware.” Open System Settings → Privacy & Security, scroll to Security, and press Open Anyway. On macOS 15 and later that is the only route.',
  },
  {
    system: 'windows',
    platform: 'Windows 10 or 11, x64',
    asset: 'dew-windows-x64-setup.exe',
    what: 'Installs for you alone, under Local AppData, so it never asks for an administrator.',
    firstLaunch:
      'SmartScreen will say “Windows protected your PC”. Choose More info, then Run anyway. With Smart App Control switched on it is blocked outright rather than warned about.',
  },
  {
    system: 'windows',
    platform: 'Windows, portable',
    asset: 'dew-windows-x64.zip',
    what: 'The same program in a folder. No uninstall entry and no Start menu shortcut.',
    firstLaunch: 'The same SmartScreen prompt as the installer.',
  },
  {
    system: 'linux',
    platform: 'Linux, x86_64',
    asset: 'dew-linux-x86_64.AppImage',
    what: 'One file, with its libraries inside it. chmod +x it and run it. Built against glibc 2.35, so it wants Ubuntu 22.04 or newer.',
    firstLaunch:
      'Nothing to get past. If it exits without a window, it is missing a library the AppImage did not bundle; run it from a terminal and the loader will name the file.',
  },
  {
    system: 'linux',
    platform: 'Linux, tarball',
    asset: 'dew-linux-x86_64.tar.gz',
    what: 'The binary and its licences, unbundled. Needs ALSA, X11, freetype and fontconfig from your distribution.',
    firstLaunch:
      'Nothing to get past, and nothing bundled: if it will not start, your distribution is missing one of the libraries above. Run it from a terminal to be told which.',
  },
] as const;

/** The checksums file every release carries. */
export const checksumsAsset = 'SHA256SUMS.txt';

export const downloadUrl = (asset: string) => `${repositoryUrl}/releases/latest/download/${asset}`;

/** Every release, which is the archive of old versions. */
export const releasesUrl = `${repositoryUrl}/releases`;

/** The first entry for each system, in the order the list declares them: what a
 *  reader on that system should press.
 *
 *  Derived rather than written out. The page used to hard-code `downloads[0]`
 *  as its one primary button, which offered a macOS DMG to everybody and left
 *  two thirds of its readers to find their own row in the table. */
export const primaryDownloads: readonly Download[] = downloads.filter(
  (download, i) => downloads.findIndex((other) => other.system === download.system) === i,
);

/** Verifying a download, as a command rather than a paragraph. */
export const verifyCommands = [
  'shasum -a 256 -c SHA256SUMS.txt --ignore-missing',
  'gh attestation verify dew-macos-universal.dmg --repo janschupke/dew',
] as const;
