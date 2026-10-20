// The schema.org Person data for the landing page and the summary page. One place, so they cannot drift apart.
import { site } from './site';

export function personJsonLd(): string {
  const person = {
    '@context': 'https://schema.org',
    '@type': 'Person',
    name: site.name,
    jobTitle: 'Quant and software developer, commercial pilot',
    description: `${site.positioning} ${site.openTo}`,
    url: 'https://francocaccia.github.io/',
    address: { '@type': 'PostalAddress', addressRegion: 'Pennsylvania', addressCountry: 'US' },
    sameAs: [site.links.github, site.links.linkedin],
  };
  // "<" is escaped so the data can never close the script element it is placed in.
  return JSON.stringify(person).replace(/</g, '\\u003c');
}
