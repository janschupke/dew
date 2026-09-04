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
import { repositoryUrl } from '@/content/setup';

export interface Download {
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
    platform: 'macOS 11 or newer',
    asset: 'dew-macos-universal.dmg',
    what: 'Apple Silicon and Intel in one binary. Open it and drag dew to Applications.',
    firstLaunch:
      'macOS will refuse it: “Apple could not verify dew is free of malware.” Open System Settings → Privacy & Security, scroll to Security, and press Open Anyway. On macOS 15 and later that is the only route: the Control-click shortcut that used to work was removed.',
  },
  {
    platform: 'Windows 10 or 11, x64',
    asset: 'dew-windows-x64-setup.exe',
    what: 'Installs for you alone, under Local AppData, so it never asks for an administrator.',
    firstLaunch:
      'SmartScreen will say “Windows protected your PC”. Choose More info, then Run anyway. On a machine with Smart App Control switched on it will be blocked outright rather than warned about, and there is no way past that but a signed build.',
  },
  {
    platform: 'Windows, portable',
    asset: 'dew-windows-x64.zip',
    what: 'The same program in a folder. No uninstall entry and no Start menu shortcut.',
    firstLaunch: 'The same SmartScreen prompt as the installer.',
  },
  {
    platform: 'Linux, x86_64',
    asset: 'dew-linux-x86_64.AppImage',
    what: 'One file, with its libraries inside it. chmod +x it and run it. Built against glibc 2.35, so it wants Ubuntu 22.04 or newer.',
    firstLaunch:
      'Nothing to get past — Linux asks no permission to run a program you downloaded. If it exits without a window, it is missing a library the AppImage did not bundle; run it from a terminal and the loader will name the file.',
  },
  {
    platform: 'Linux, tarball',
    asset: 'dew-linux-x86_64.tar.gz',
    what: 'The binary and its licences, unbundled. Needs ALSA, X11, freetype and fontconfig from your distribution.',
    firstLaunch:
      'Nothing to get past, and nothing bundled either: if it will not start, your distribution is missing one of the libraries above. Run it from a terminal to be told which.',
  },
] as const;

/** The checksums file every release carries. */
export const checksumsAsset = 'SHA256SUMS.txt';

export const downloadUrl = (asset: string) => `${repositoryUrl}/releases/latest/download/${asset}`;

/** Every release, which is the archive of old versions. */
export const releasesUrl = `${repositoryUrl}/releases`;

/** Verifying a download, as a command rather than a paragraph. */
export const verifyCommands = [
  'shasum -a 256 -c SHA256SUMS.txt --ignore-missing',
  'gh attestation verify dew-macos-universal.dmg --repo janschupke/dew',
] as const;
