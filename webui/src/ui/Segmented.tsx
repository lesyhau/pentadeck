import { useRef, type KeyboardEvent } from 'react'
import { cn } from './cn'
import type { Option } from './Field'

/**
 * One of a few choices, all in view, sharing the width equally. A radio group to assistive tech:
 * one tab stop, arrow keys move the choice.
 */
export function Segmented<T extends string>({ value, options, onChange, ariaLabel, labelledBy, className }: {
  value: T
  options: Option<T>[]
  onChange: (value: T) => void
  ariaLabel?: string
  labelledBy?: string
  className?: string
}) {
  const refs = useRef<(HTMLButtonElement | null)[]>([])
  const current = options.findIndex(o => o.value === value)

  const onKey = (e: KeyboardEvent) => {
    const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[e.key]
    let next = step === undefined ? -1 : (Math.max(current, 0) + step + options.length) % options.length
    if (e.key === 'Home') next = 0
    if (e.key === 'End') next = options.length - 1
    if (next < 0) return
    e.preventDefault()
    onChange(options[next].value)
    refs.current[next]?.focus()
  }

  return (
    <div
      role="radiogroup"
      aria-label={ariaLabel}
      aria-labelledby={labelledBy}
      onKeyDown={onKey}
      className={cn('grid gap-1 rounded-lg border border-line-strong bg-field p-1', className)}
      style={{ gridTemplateColumns: `repeat(${options.length}, minmax(0, 1fr))` }}
    >
      {options.map((o, i) => {
        const on = i === current
        return (
          <button
            key={o.value}
            ref={el => { refs.current[i] = el }}
            type="button"
            role="radio"
            aria-checked={on}
            tabIndex={on || (current < 0 && i === 0) ? 0 : -1}
            onClick={() => onChange(o.value)}
            className={cn(
              'h-11 truncate rounded-md px-2 text-sm font-medium transition-colors duration-150',
              on
                ? 'bg-accent/15 text-accent-ink shadow-[inset_0_0_0_1px_rgb(var(--pd-accent)/0.5)]'
                : 'text-ink-2 hover:bg-raised hover:text-ink',
            )}
          >
            {o.label}
          </button>
        )
      })}
    </div>
  )
}
