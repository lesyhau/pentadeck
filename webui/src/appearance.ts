/**
 * Appearance: the choice (system, light or dark), where it is kept, and the tiny script that stamps
 * the theme on <html> before the first paint so a light-theme visitor never sees a dark flash.
 *
 * No DOM access at module level: vite.config.ts imports this file in Node to inline the script.
 */

export type Appearance = 'system' | 'light' | 'dark'
export type Theme = 'light' | 'dark'

export const APPEARANCE_KEY = 'pd_appearance'
/** Dark until chosen: the device sits on a desk and is mostly looked at in the evening. */
export const DEFAULT_APPEARANCE: Appearance = 'dark'
/** The browser chrome colour for each theme: the canvas colour in theme.css. */
export const THEME_COLORS: Record<Theme, string> = { dark: '#0A0B0C', light: '#F3F4F6' }

export const isAppearance = (v: unknown): v is Appearance => v === 'system' || v === 'light' || v === 'dark'

/** Inlined at the end of <head> by vite.config.ts; mirrors applyTheme() in ui/useAppearance.ts. */
export function prePaintScript(): string {
  const key = JSON.stringify(APPEARANCE_KEY)
  const fallback = JSON.stringify(DEFAULT_APPEARANCE)
  const colors = JSON.stringify(THEME_COLORS)
  return [
    '(function(){var p;',
    `try{p=localStorage.getItem(${key})}catch(e){}`,
    `if(p!=="system"&&p!=="light"&&p!=="dark")p=${fallback};`,
    'var t=p==="system"?(matchMedia("(prefers-color-scheme: light)").matches?"light":"dark"):p;',
    'var r=document.documentElement;r.setAttribute("data-theme",t);r.style.colorScheme=t;',
    `var m=document.querySelector('meta[name="theme-color"]');if(m)m.setAttribute("content",${colors}[t]);`,
    '})()',
  ].join('')
}
