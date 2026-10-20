import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';

// User site repo "Francocaccia.github.io" is served from the domain root.
export default defineConfig({
  site: 'https://francocaccia.github.io',
  trailingSlash: 'always',
  build: { format: 'directory' },
  compressHTML: true,
  integrations: [sitemap({ filter: (page) => !page.endsWith('/404/'), lastmod: new Date() })],
  devToolbar: { enabled: false },
});
