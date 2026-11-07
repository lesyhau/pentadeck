import { useEffect, useRef, useState, type KeyboardEvent } from 'react'
import type { Appearance } from '../lib/appearance'
import { cn } from './cn'
import { CheckIcon, MonitorIcon, MoonIcon, SunIcon } from './icons'

const CHOICES = [
  { value: 'system', label: 'System', Icon: MonitorIcon },
  { value: 'light', label: 'Light', Icon: SunIcon },
  { value: 'dark', label: 'Dark', Icon: MoonIcon },
] as const

/** A button showing the current appearance, opening a menu of the three. */
export function AppearanceMenu({ value, onChange }: { value: Appearance; onChange: (value: Appearance) => void }) {
  const [open, setOpen] = useState(false)
  const root = useRef<HTMLDivElement>(null)
  const trigger = useRef<HTMLButtonElement>(null)
  const items = useRef<(HTMLButtonElement | null)[]>([])
  const current = CHOICES.find(c => c.value === value) ?? CHOICES[2]

  useEffect(() => {
    if (!open) return
    items.current[CHOICES.findIndex(c => c.value === value)]?.focus()
    const onPointer = (e: PointerEvent) => { if (!root.current?.contains(e.target as Node)) setOpen(false) }
    const onKey = (e: globalThis.KeyboardEvent) => {
      if (e.key === 'Escape') { setOpen(false); trigger.current?.focus() }
    }
    document.addEventListener('pointerdown', onPointer)
    document.addEventListener('keydown', onKey)
    return () => {
      document.removeEventListener('pointerdown', onPointer)
      document.removeEventListener('keydown', onKey)
    }
    // Only on opening: focus goes to the chosen item then, not each time the choice changes.
  }, [open])

  const onMenuKey = (e: KeyboardEvent) => {
    const at = items.current.findIndex(el => el === document.activeElement)
    const n = CHOICES.length
    const to = e.key === 'ArrowDown' ? (at + 1) % n : e.key === 'ArrowUp' ? (at - 1 + n) % n : e.key === 'Home' ? 0 : e.key === 'End' ? n - 1 : -1
    if (to >= 0) { e.preventDefault(); items.current[to]?.focus() }
    if (e.key === 'Tab') setOpen(false)
  }

  const choose = (v: Appearance) => {
    onChange(v)
    setOpen(false)
    trigger.current?.focus()
  }

  return (
    <div ref={root} className="relative">
      <button
        ref={trigger}
        type="button"
        aria-haspopup="menu"
        aria-expanded={open}
        aria-label={`Appearance: ${current.label}`}
        onClick={() => setOpen(o => !o)}
        className={cn(
          'grid size-11 place-items-center rounded-lg text-ink-2 transition-colors hover:bg-raised hover:text-ink',
          open && 'bg-raised text-ink',
        )}
      >
        <current.Icon size={20} />
      </button>
      {open && (
        <div
          role="menu"
          aria-label="Appearance"
          onKeyDown={onMenuKey}
          className="pd-fade-in absolute right-0 top-full z-50 mt-2 w-48 rounded-xl border border-line-strong bg-raised p-1 shadow-pop"
        >
          {CHOICES.map((c, i) => {
            const on = c.value === value
            return (
              <button
                key={c.value}
                ref={el => { items.current[i] = el }}
                type="button"
                role="menuitemradio"
                aria-checked={on}
                tabIndex={-1}
                onClick={() => choose(c.value)}
                className={cn(
                  'flex h-11 w-full items-center gap-3 rounded-lg px-3 text-left text-[15px] transition-colors hover:bg-line/60 focus-visible:bg-line/60 focus-visible:outline-none',
                  on ? 'text-ink' : 'text-ink-2',
                )}
              >
                <c.Icon size={18} className={on ? 'text-accent-ink' : undefined} />
                <span className="flex-1">{c.label}</span>
                {on && <CheckIcon size={16} className="text-accent-ink" />}
              </button>
            )
          })}
        </div>
      )}
    </div>
  )
}
