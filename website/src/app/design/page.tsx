import type { Metadata } from 'next';

import { Lead, Section } from '@/ui/Prose';
import { Shot } from '@/ui/Shot';
import { tokens } from '@/lib/tokens';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('design.title')} — ${t('site.name')}` };

/*  The design system showing itself.

    Every value on this page is read out of design-tokens.json, which
    `dew_shot tokens` wrote from darkPalette() - so this page cannot claim a
    colour the application does not paint. It is the web analogue of
    `dew_shot gallery`, and the gallery render sits at the bottom of it.
*/
function Swatch({ name, value }: { name: string; value: string }) {
  return (
    <li className="gap-md flex items-center">
      <span
        aria-hidden
        className="border-hairline size-xxl border-divider shrink-0 rounded-sm"
        style={{
          backgroundColor: `var(--color-${name.replace(/[A-Z]/g, (c) => `-${c.toLowerCase()}`)})`,
        }}
      />
      <span className="min-w-0">
        <span className="text-body text-primary block font-mono">{name}</span>
        <span className="text-caption text-disabled block font-mono">{value}</span>
      </span>
    </li>
  );
}

function Scale({ name, entries }: { name: string; entries: Readonly<Record<string, number>> }) {
  return (
    <div>
      <h3 className="text-title text-primary font-mono">{name}</h3>
      <dl className="mt-sm gap-x-lg gap-y-xs text-body grid grid-cols-[auto_1fr]">
        {Object.entries(entries).map(([key, value]) => (
          <div key={key} className="contents">
            <dt className="text-secondary font-mono">{key}</dt>
            <dd className="text-primary font-mono">{value}</dd>
          </div>
        ))}
      </dl>
    </div>
  );
}

export default function Design() {
  return (
    <div className="py-xxl">
      <h1 className="text-h1 text-primary font-semibold">{t('design.title')}</h1>
      <Lead>{t('design.lead')}</Lead>

      <Section title={t('design.coloursTitle')}>
        <p className="text-prose text-secondary max-w-[68ch]">{t('design.coloursBody')}</p>

        <ul className="mt-xl gap-lg grid sm:grid-cols-2 lg:grid-cols-3">
          {Object.entries(tokens.colour).map(([name, value]) => (
            <Swatch key={name} name={name} value={value} />
          ))}
        </ul>
      </Section>

      <Section title={t('design.liftTitle')}>
        <p className="text-prose text-secondary max-w-[68ch]">{t('design.liftBody')}</p>

        <ul className="mt-xl gap-lg grid sm:grid-cols-2 lg:grid-cols-3">
          {Object.entries(tokens.lift).map(([name, value]) => (
            <Swatch key={name} name={name} value={value} />
          ))}
        </ul>
      </Section>

      <Section title={t('design.rampTitle')}>
        <p className="text-prose text-secondary max-w-[68ch]">{t('design.rampBody')}</p>

        <ul className="mt-xl gap-md flex flex-wrap">
          {tokens.channelRamp.map((value, i) => (
            <li key={value} className="text-center">
              <span
                aria-hidden
                className="size-xxl block rounded-sm"
                style={{ backgroundColor: `var(--color-channel-${String(i)})` }}
              />
              <span className="mt-xs text-caption text-disabled block font-mono">{value}</span>
            </li>
          ))}
        </ul>
      </Section>

      <Section title={t('design.scalesTitle')}>
        <div className="gap-xxl grid sm:grid-cols-2 lg:grid-cols-3">
          <Scale name="space" entries={tokens.space} />
          <Scale name="radius" entries={tokens.radius} />
          <Scale name="stroke" entries={tokens.stroke} />
          <Scale name="type" entries={tokens.type} />
          <Scale name="motion" entries={tokens.motion} />
          <Scale name="emphasis" entries={tokens.emphasis} />
        </div>
      </Section>

      <Shot name="gallery" alt={t('design.galleryCaption')} caption={t('design.galleryCaption')} />
    </div>
  );
}
