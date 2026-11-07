import type { ReactNode } from 'react'
import { Mark } from '../brand/Mark'

/**
 * The bar across the top: the mark and the wordmark at the left, then `nav` (the page tabs, on
 * wide screens), and the page's controls at the right. It stays put while the page scrolls and
 * runs up under the status bar on a phone.
 */
export function AppBar({ nav, children }: { nav?: ReactNode; children?: ReactNode }) {
  return (
    <header className="sticky top-0 z-30 border-b border-line bg-canvas/85 pt-[env(safe-area-inset-top)] backdrop-blur-md">
      <div className="pd-wrap flex h-header items-center gap-3">
        <a href="#/" className="flex min-w-0 items-center gap-2.5 rounded-md" aria-label="Pentadeck, overview">
          <span aria-hidden="true" className="flex shrink-0"><Mark size={28} /></span>
          <span className="truncate font-display text-[15px] font-bold uppercase tracking-[0.14em] text-ink">Pentadeck</span>
        </a>
        {nav}
        <div className="ml-auto flex shrink-0 items-center gap-1.5">{children}</div>
      </div>
    </header>
  )
}
