/** What each system is CALLED, on a button.
 *
 *  `Download.platform` is a sentence a reader reads - "macOS 11 or newer",
 *  "Windows, portable" - and is the wrong length for a control. This is the
 *  short name, and it is a switch rather than a built key because
 *  .ai/rules/website.md requires `t()` to take a literal: `t(`download.system.
 *  ${system}`)` is exactly the runtime-built key that rule refuses.
 */
import type { PlatformSystem } from '@/ui/Icon';
import { t } from '@/lib/strings';

export function systemName(system: PlatformSystem): string {
  if (system === 'macos') return t('download.macos');
  if (system === 'windows') return t('download.windows');

  return t('download.linux');
}
