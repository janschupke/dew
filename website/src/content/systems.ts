/** What each system is CALLED, on a button. A switch rather than a built key,
 *  because `t()` takes a literal. */
import type { PlatformSystem } from '@/ui/Icon';
import { t } from '@/lib/strings';

export function systemName(system: PlatformSystem): string {
  if (system === 'macos') return t('download.macos');
  if (system === 'windows') return t('download.windows');

  return t('download.linux');
}
