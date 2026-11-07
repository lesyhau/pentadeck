import type { Config } from 'tailwindcss'

/** A Pentadeck colour token (theme.css), as RGB channels so opacity modifiers work: bg-accent/15. */
const token = (name: string) => `rgb(var(--pd-${name}) / <alpha-value>)`

export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        canvas: token('canvas'),
        surface: token('surface'),
        raised: token('raised'),
        field: token('field'),
        line: { DEFAULT: token('line'), strong: token('line-strong') },
        ink: { DEFAULT: token('ink'), 2: token('ink-2'), 3: token('ink-3') },
        accent: { DEFAULT: token('accent'), hover: token('accent-hover'), on: token('on-accent'), ink: token('accent-ink') },
        hud: { DEFAULT: token('hud-rule'), ink: token('hud-ink') },
        warn: { DEFAULT: token('warn'), ink: token('warn-ink') },
        danger: { DEFAULT: token('danger'), ink: token('danger-ink') },
      },
      fontFamily: {
        sans: ['Inter', 'ui-sans-serif', 'system-ui', '-apple-system', 'Segoe UI', 'Roboto', 'sans-serif'],
        display: ['Orbitron', 'Inter', 'ui-sans-serif', 'sans-serif'],
      },
      boxShadow: {
        card: 'var(--pd-shadow-card)',
        pop: 'var(--pd-shadow-pop)',
      },
      height: { header: 'var(--pd-header)', tabbar: 'var(--pd-tabbar)' },
    },
  },
} satisfies Config
