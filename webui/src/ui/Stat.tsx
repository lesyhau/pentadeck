import type { ReactNode } from 'react'
import { cn } from './cn'
import { Skeleton } from './Feedback'

export type MeterTone = 'accent' | 'warn' | 'danger' | 'muted'

const METER: Record<MeterTone, string> = {
  accent: 'text-accent',
  warn: 'text-warn',
  danger: 'text-danger',
  muted: 'text-ink-3',
}

/** The device's alert colours for a load: teal, orange from 60 %, red from 80 %. */
export const loadTone = (pct: number): MeterTone => (pct >= 80 ? 'danger' : pct >= 60 ? 'warn' : 'accent')

/** A bar in ten cells under a figure. Decorative: the figure above it says the same. */
export function Meter({ value, tone = 'accent', className }: { value: number; tone?: MeterTone; className?: string }) {
  const pct = Math.max(0, Math.min(100, value))
  return (
    <div aria-hidden="true" className={cn('pd-meter', METER[tone], className)}>
      <span style={{ width: `${pct}%` }} />
    </div>
  )
}

/** Three across on a phone; the caller widens it on larger screens. */
export function StatGrid({ children, className }: { children: ReactNode; className?: string }) {
  return <div className={cn('grid grid-cols-3 gap-2 sm:gap-3', className)}>{children}</div>
}

/**
 * One reading: its name in small capitals, the figure in the display face with its unit beside it,
 * and a line or a meter under it. `text` sets the figure in the UI face for words ("Just now").
 */
export function StatTile({ label, value, unit, text, sub, meter, loading }: {
  label: string
  value?: ReactNode
  unit?: string
  text?: boolean
  sub?: ReactNode
  meter?: ReactNode
  loading?: boolean
}) {
  const empty = value === null || value === undefined || value === ''
  return (
    <div className="pd-brackets min-w-0 rounded border border-line bg-surface px-3 py-2.5 shadow-card sm:px-4 sm:py-3">
      <div className="truncate text-[11px] font-medium uppercase tracking-[0.08em] text-ink-2">{label}</div>
      <div className="mt-1.5 flex h-7 items-baseline gap-1 sm:h-8">
        {loading ? (
          <Skeleton className="h-5 w-12 self-center" />
        ) : empty ? (
          <span className="text-lg text-ink-3" aria-label="No reading">—</span>
        ) : (
          <>
            <span className={cn(
              'min-w-0 truncate text-ink',
              text ? 'text-base font-medium sm:text-[17px]' : 'font-display text-xl font-bold tabular-nums sm:text-2xl',
            )}>
              {value}
            </span>
            {unit && <span className="shrink-0 text-xs font-medium text-ink-2">{unit}</span>}
          </>
        )}
      </div>
      {meter ? <div className="mt-2">{meter}</div> : sub ? <div className="mt-1 truncate text-xs text-ink-2">{loading ? ' ' : sub}</div> : null}
    </div>
  )
}
