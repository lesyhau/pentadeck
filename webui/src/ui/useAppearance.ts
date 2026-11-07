import { useSyncExternalStore } from 'react'
import { APPEARANCE_KEY, DEFAULT_APPEARANCE, THEME_COLORS, isAppearance, type Appearance, type Theme } from '../appearance'

/**
 * The appearance choice for this browser, as a tiny store: read once, written on change, applied
 * to <html> the way the pre-paint script does it, and followed across tabs and OS theme changes.
 */

const listeners = new Set<() => void>()
const lightQuery = window.matchMedia('(prefers-color-scheme: light)')

function read(): Appearance {
  try {
    const v = localStorage.getItem(APPEARANCE_KEY)
    if (isAppearance(v)) return v
  } catch { /* storage blocked */ }
  return DEFAULT_APPEARANCE
}

let current = read()

function applyTheme() {
  const theme: Theme = current === 'system' ? (lightQuery.matches ? 'light' : 'dark') : current
  const root = document.documentElement
  root.setAttribute('data-theme', theme)
  root.style.colorScheme = theme
  document.querySelector('meta[name="theme-color"]')?.setAttribute('content', THEME_COLORS[theme])
}

function update(next: Appearance) {
  current = next
  applyTheme()
  listeners.forEach(l => l())
}

lightQuery.addEventListener('change', () => { if (current === 'system') applyTheme() })
window.addEventListener('storage', e => { if (e.key === APPEARANCE_KEY) update(read()) })

export function setAppearance(next: Appearance) {
  try { localStorage.setItem(APPEARANCE_KEY, next) } catch { /* private mode: this visit only */ }
  update(next)
}

const subscribe = (l: () => void) => { listeners.add(l); return () => { listeners.delete(l) } }

export function useAppearance(): Appearance {
  return useSyncExternalStore(subscribe, () => current)
}
