// The written answers behind the landing page's "ask" bar. They are not generated: every sentence is built at
// build time from src/data/site.json, so the page can only say what the data says. Each entry also carries a few
// keywords, which is how the bar picks one for what a visitor types.
import { gzipSync } from 'node:zlib';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { site, real, withBase } from './site';

export interface AnswerLink {
  label: string;
  href: string;
  mode?: string;
  external?: boolean;
}

export interface Answer {
  id: string;
  question: string;
  keys: string;
  text: string;
  links: AnswerLink[];
}

const first = (text: string) => text.split('. ')[0].replace(/\.$/, '') + '.';
const list = (items: string[]) => (items.length < 2 ? items.join('') : `${items.slice(0, -1).join(', ')} and ${items[items.length - 1]}`);

function coreSize(): string {
  try {
    const bytes = gzipSync(readFileSync(join(process.cwd(), 'src', 'generated', 'core.wasm'))).length;
    return `${Math.round(bytes / 1024)} KB`;
  } catch {
    return 'small';
  }
}

export function faq(): Answer[] {
  const [atb, sqr, defi] = site.projects;
  const email = real(site.email);
  const resume = real(site.resume);
  const active = site.certifications.filter((c) => c.status === 'active');
  const pending = site.certifications.filter((c) => c.status === 'in-progress');
  const certLine = (c: (typeof site.certifications)[number]) =>
    `${c.name}${'issuer' in c ? ` (${c.issuer})` : ''}${'detail' in c && c.detail ? (/^[0-9]/.test(c.detail) ? ` with ${c.detail}` : `, ${c.detail}`) : ''}`;
  const school = (e: (typeof site.education)[number]) => `${e.degree}, ${e.school}${'place' in e && e.place ? `, ${e.place}` : ''} (${e.period})`;

  return [
    {
      id: 'built',
      question: 'What has he built?',
      keys: 'built build made make project projects work bot code github shipped',
      text: `${atb.name}: ${first(atb.summary)} ${sqr.name}: ${first(sqr.summary)} ${defi.name}: ${first(defi.summary)} And the C++ core this site runs on.`,
      links: [
        { label: 'Read the finance view', href: withBase('/finance/'), mode: 'finance' },
        { label: 'GitHub', href: site.links.github, external: true },
      ],
    },
    {
      id: 'pilot',
      question: 'Is he a pilot?',
      keys: 'pilot fly flying flight aviation license licenses hours instrument rating faa plane',
      text: `Yes. ${list(active.filter((c) => c.track === 'aviation').map(certLine))}. In progress: ${list(pending.filter((c) => c.track === 'aviation').map((c) => c.name))}.`,
      links: [{ label: 'Open the aviation view', href: withBase('/aviation/'), mode: 'aviation' }],
    },
    {
      id: 'studying',
      question: 'What is he studying?',
      keys: 'study studying school degree education university lehigh lynchburg masters master bachelor college',
      text: `${site.education.map(school).join('. ')}.`,
      links: [],
    },
    {
      id: 'worked',
      question: 'Where has he worked?',
      keys: 'worked work experience job jobs employer fund internship intern career',
      text: `${site.experience.map((e) => `${e.role}, ${e.org} (${e.start} to ${e.end})`).join('. ')}.`,
      links: resume ? [{ label: 'Resume (PDF)', href: withBase(resume) }] : [],
    },
    {
      id: 'looking',
      question: 'What is he looking for?',
      keys: 'looking job role roles hire hiring open available want seeking opportunity',
      text: `${site.openTo} Based in ${site.location}.`,
      links: email ? [{ label: 'Get in touch', href: `mailto:${email}` }] : [],
    },
    {
      id: 'skills',
      question: 'Which languages and tools?',
      keys: 'language languages skill skills tools stack tech technologies speak spanish english portuguese mandarin python go',
      text: `Languages spoken: ${site.languages.map((l) => `${l.name} (${l.level})`).join(', ')}. Tools: ${site.technicalSkills.slice(0, 14).join(', ')}, and more on the resume.`,
      links: [{ label: 'Try `skills` in the terminal', href: withBase('/terminal/'), mode: 'terminal' }],
    },
    {
      id: 'bot',
      question: 'How does the bot do?',
      keys: 'bot trading performance return results stats finance autotradingbot paper returns profit',
      text: `It trades on paper only, and it has been tracked since ${site.bot.trackingSince}. Its results are shown only as percentages against the S&P 500 and a 60/40 portfolio, and it is too early to read anything into them. The Finance view has the figures and the limits written in code.`,
      links: [{ label: 'Open the finance view', href: withBase('/finance/'), mode: 'finance' }],
    },
    {
      id: 'site',
      question: 'How is this site built?',
      keys: 'site website built build made how stack astro c++ cpp webassembly wasm tech',
      text: `One data file feeds this page and the three views. The logic is C++ compiled to WebAssembly (${coreSize()} gzipped): a weather decoder, a chart engine, the shell in the terminal view. The pages themselves are static.`,
      links: [{ label: 'Open the terminal view', href: withBase('/terminal/'), mode: 'terminal' }],
    },
    {
      id: 'contact',
      question: 'How do I contact him?',
      keys: 'contact email reach linkedin github resume cv message hire talk',
      text: `${email ? `Email ${email}. ` : ''}GitHub and LinkedIn are below${resume ? ', and the resume is a PDF' : ''}.`,
      links: [
        ...(email ? [{ label: 'Email', href: `mailto:${email}` }] : []),
        { label: 'GitHub', href: site.links.github, external: true },
        { label: 'LinkedIn', href: site.links.linkedin, external: true },
        ...(resume ? [{ label: 'Resume (PDF)', href: withBase(resume) }] : []),
      ],
    },
  ];
}
