import { useEffect, useRef, useState, type ReactNode } from 'react'
import { cn } from './cn'
import { TONE_ICON, TONE_TEXT, type Tone } from './tone'

export type DotTone = 'live' | 'idle' | 'down'

const DOT: Record<DotTone, string> = {
  live: 'bg-accent shadow-[0_0_0_3px_rgb(var(--pd-accent)/0.2)]',
  idle: 'bg-ink-3',
  down: 'bg-danger shadow-[0_0_0_3px_rgb(var(--pd-danger)/0.2)]',
}

/** A small round state light. Pair it with words: colour alone says nothing to some readers. */
export function StatusDot({ tone, className }: { tone: DotTone; className?: string }) {
  return <span aria-hidden="true" className={cn('inline-block size-2 shrink-0 rounded-full transition-colors', DOT[tone], className)} />
}

const NOTICE_FRAME: Record<Tone, string> = {
  info: 'border-line-strong bg-raised',
  success: 'border-accent/40 bg-accent/10',
  warning: 'border-warn/45 bg-warn/10',
  danger: 'border-danger/45 bg-danger/10',
}

/** A framed message across the width: what happened, and what can be done about it. */
export function Notice({ tone, title, children, action, role }: {
  tone: Tone
  title: ReactNode
  children?: ReactNode
  action?: ReactNode
  role?: 'status' | 'alert'
}) {
  const Icon = TONE_ICON[tone]
  return (
    <div role={role} className={cn('flex flex-wrap items-start gap-x-3 gap-y-3 rounded-xl border p-4', NOTICE_FRAME[tone])}>
      <Icon className={cn('mt-0.5 shrink-0', TONE_TEXT[tone])} />
      <div className="min-w-0 flex-1">
        <p className="text-[15px] font-medium text-ink">{title}</p>
        {children && <p className="mt-0.5 text-[13px] leading-snug text-ink-2">{children}</p>}
      </div>
      {action && <div className="w-full pl-[30px] sm:w-auto sm:pl-0">{action}</div>}
    </div>
  )
}

/** A one-line result under the buttons that caused it. */
export function InlineNotice({ tone, children }: { tone: Tone; children: ReactNode }) {
  const Icon = TONE_ICON[tone]
  return (
    <p role="status" className={cn('flex items-start gap-2 text-[13px] leading-snug', TONE_TEXT[tone])}>
      <Icon size={16} className="mt-px shrink-0" />
      <span>{children}</span>
    </p>
  )
}

/** A grey block where something is still loading. */
export function Skeleton({ className }: { className?: string }) {
  return <span aria-hidden="true" className={cn('pd-skeleton block', className)} />
}

/**
 * Content that eases open and shut. While closing, the last children stay on screen until the
 * motion is over, so a message does not vanish before its box has folded.
 */
export function Reveal({ show, children }: { show: boolean; children: ReactNode }) {
  const [mounted, setMounted] = useState(show)
  const [open, setOpen] = useState(show)
  const last = useRef<ReactNode>(children)
  if (show) last.current = children

  useEffect(() => {
    if (show) {
      setMounted(true)
      // Two frames: mounted closed first, so the opening has something to ease from.
      let inner = 0
      const outer = requestAnimationFrame(() => { inner = requestAnimationFrame(() => setOpen(true)) })
      return () => { cancelAnimationFrame(outer); cancelAnimationFrame(inner) }
    }
    setOpen(false)
    const t = setTimeout(() => setMounted(false), 240)
    return () => clearTimeout(t)
  }, [show])

  if (!mounted) return null
  return (
    <div className="pd-reveal" data-open={open} aria-hidden={!show || undefined}>
      <div>{last.current}</div>
    </div>
  )
}

/** An action's result, kept beside it: "Playing on the device", "Pair this browser first". */
export interface Note {
  tone: Tone
  text: string
}

/** A Note under the buttons that caused it, easing in and out. */
export function NoticeLine({ notice }: { notice: Note | null }) {
  return (
    <Reveal show={!!notice}>
      {notice && <div className="pt-1"><InlineNotice tone={notice.tone}>{notice.text}</InlineNotice></div>}
    </Reveal>
  )
}
