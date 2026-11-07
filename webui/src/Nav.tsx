import type { ComponentType, MouseEvent } from 'react'
import { ChipIcon, GaugeIcon, ScreensIcon, SlidersIcon, cn, type IconProps } from './ui'
import { PAGES, type Page } from './router'

/**
 * The four pages as tabs: a bar along the bottom on a phone, in reach of a thumb, and in the
 * header from `lg`. The Settings tab carries a dot while there are unsaved changes.
 */

const ICONS: Record<Page, ComponentType<IconProps>> = { overview: GaugeIcon, remote: ScreensIcon, settings: SlidersIcon, device: ChipIcon }

interface NavProps {
  page: Page
  /** Unsaved settings: the Settings tab shows a dot. */
  dirty: boolean
}

/** Tapping the tab of the page you are on takes you back to its top. */
const toTop = (on: boolean) => (e: MouseEvent) => {
  if (!on) return
  e.preventDefault()
  window.scrollTo({ top: 0, behavior: 'smooth' })
}

function Dot({ className }: { className?: string }) {
  return <span aria-hidden="true" className={cn('absolute size-2 rounded-full bg-accent ring-2', className)} />
}

const unsaved = <span className="sr-only">, unsaved changes</span>

export function TabBar({ page, dirty }: NavProps) {
  return (
    <nav
      aria-label="Pages"
      className="fixed inset-x-0 bottom-0 z-[35] border-t border-line bg-surface/95 pb-[env(safe-area-inset-bottom)] backdrop-blur-md lg:hidden"
    >
      <ul className="mx-auto grid h-tabbar max-w-xl grid-cols-4 px-[max(4px,env(safe-area-inset-left))]">
        {PAGES.map(p => {
          const on = p.page === page
          const Icon = ICONS[p.page]
          return (
            <li key={p.page} className="min-w-0">
              <a
                href={p.hash}
                aria-current={on ? 'page' : undefined}
                onClick={toTop(on)}
                className={cn(
                  'relative flex h-full min-h-11 select-none flex-col items-center justify-center gap-1 rounded-lg transition-colors',
                  '[-webkit-touch-callout:none] focus-visible:-outline-offset-2',
                  on ? 'text-accent-ink' : 'text-ink-2 active:text-ink',
                )}
              >
                {on && <span aria-hidden="true" className="absolute inset-x-5 -top-px h-0.5 rounded-b-full bg-accent" />}
                <span className="relative">
                  <Icon size={22} />
                  {p.page === 'settings' && dirty && <Dot className="-right-1 -top-0.5 ring-surface" />}
                </span>
                <span className="text-[10px] font-medium uppercase leading-none tracking-[0.1em]">
                  {p.label}
                  {p.page === 'settings' && dirty && unsaved}
                </span>
              </a>
            </li>
          )
        })}
      </ul>
    </nav>
  )
}

export function TopTabs({ page, dirty }: NavProps) {
  return (
    <nav aria-label="Pages" className="ml-6 hidden self-stretch lg:block">
      <ul className="flex h-full">
        {PAGES.map(p => {
          const on = p.page === page
          const Icon = ICONS[p.page]
          return (
            <li key={p.page}>
              <a
                href={p.hash}
                aria-current={on ? 'page' : undefined}
                onClick={toTop(on)}
                className={cn(
                  'relative flex h-full items-center gap-2 px-4 text-sm font-medium transition-colors focus-visible:-outline-offset-2',
                  on ? 'text-ink' : 'text-ink-2 hover:text-ink',
                )}
              >
                <span className="relative">
                  <Icon size={18} className={on ? 'text-accent-ink' : undefined} />
                  {p.page === 'settings' && dirty && <Dot className="-right-1 -top-0.5 ring-canvas" />}
                </span>
                {p.label}
                {p.page === 'settings' && dirty && unsaved}
                {on && <span aria-hidden="true" className="absolute inset-x-3 -bottom-px h-0.5 rounded-t-full bg-accent" />}
              </a>
            </li>
          )
        })}
      </ul>
    </nav>
  )
}
