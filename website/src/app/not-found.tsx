import { Button } from '@/ui/Button';
import { Container } from '@/ui/Surface';
import { P } from '@/ui/Prose';
import { t } from '@/lib/strings';

/** Next's own 404 is unstyled and light, on a site that declares
 *  `color-scheme: dark` - so a wrong URL was the one page that did not look
 *  like the rest of it. */
export default function NotFound() {
  return (
    <Container className="pt-band pb-band">
      <h1 className="text-h1 text-primary leading-tight font-semibold tracking-tight">
        {t('notFound.title')}
      </h1>

      <P className="mt-stack">{t('notFound.body')}</P>

      <div className="mt-section">
        <Button variant="primary" href="/">
          {t('notFound.home')}
        </Button>
      </div>
    </Container>
  );
}
