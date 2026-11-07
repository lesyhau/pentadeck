import { useId, type ReactNode } from 'react'
import { cn } from './cn'

/**
 * A heading as the device screens draw one: small tracked capitals in teal, then a thin rule to
 * the edge, and whatever belongs at the right after the rule.
 */
export function HudHeading({ as: Tag = 'h2', id, children, right, className }: {
  as?: 'h1' | 'h2' | 'h3'
  id?: string
  children: ReactNode
  right?: ReactNode
  className?: string
}) {
  return (
    <div className={cn('flex min-h-7 items-center gap-3', className)}>
      <Tag id={id} className="shrink-0 text-xs font-medium uppercase leading-none tracking-[0.16em] text-hud-ink">{children}</Tag>
      <span aria-hidden="true" className="h-px min-w-4 flex-1 bg-hud" />
      {right && <div className="flex shrink-0 items-center gap-2">{right}</div>}
    </div>
  )
}

/** A settings card: its heading, a line about it, and its fields. */
export function Card({ title, caption, right, children, className }: {
  title: string
  caption?: ReactNode
  right?: ReactNode
  children?: ReactNode
  className?: string
}) {
  const id = useId()
  return (
    <section aria-labelledby={id} className={cn('rounded-xl border border-line bg-surface p-4 shadow-card sm:p-6', className)}>
      <HudHeading id={id} right={right}>{title}</HudHeading>
      {caption && <p className="mt-2 max-w-[60ch] text-[13px] leading-snug text-ink-2">{caption}</p>}
      {children && <div className="mt-6 space-y-6">{children}</div>}
    </section>
  )
}

/** A page-level title with a line under it. */
export function PageTitle({ as: Tag = 'h1', title, meta, className }: {
  as?: 'h1' | 'h2'
  title: string
  meta?: ReactNode
  className?: string
}) {
  return (
    <div className={className}>
      <Tag className="text-xl font-medium tracking-[-0.01em] text-ink sm:text-2xl">{title}</Tag>
      {meta && <p className="mt-1 text-[13px] text-ink-2">{meta}</p>}
    </div>
  )
}

export type BadgeTone = 'accent' | 'warn' | 'neutral'

const BADGE: Record<BadgeTone, string> = {
  accent: 'border-accent/40 bg-accent/10 text-accent-ink',
  warn: 'border-warn/50 bg-warn/10 text-warn-ink',
  neutral: 'border-line-strong text-ink-2',
}

/** A short state word in a frame: "Token saved". */
export function Badge({ tone = 'neutral', children }: { tone?: BadgeTone; children: ReactNode }) {
  return (
    <span className={cn('inline-flex h-6 items-center whitespace-nowrap rounded-md border px-2 text-xs font-medium', BADGE[tone])}>
      {children}
    </span>
  )
}
