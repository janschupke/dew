import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import Features from '@/app/features/page';
import Home from '@/app/page';
import { features } from '@/content/features';

/*  Every page here is a plain synchronous component and stays one.
    Testing Library cannot render an async server component, so a page that
    reached for `await` would be a page nothing on this side could see.
*/
describe('pages', () => {
  it('the home page states what dew is, and that it is a prototype', () => {
    render(<Home />);

    expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent('dew');
    expect(screen.getByText(/working prototype/i)).toBeInTheDocument();
  });

  it('the features page renders every feature', () => {
    render(<Features />);

    // The coverage half: a feature added to the content module and not to the
    // page is exactly the omission nobody notices.
    for (const feature of features)
      expect(screen.getByRole('heading', { name: feature.name })).toBeInTheDocument();

    expect(features.length).toBeGreaterThan(8);
  });

  it('no feature is left without a sentence', () => {
    for (const feature of features) expect(feature.body.length).toBeGreaterThan(40);
  });
});
