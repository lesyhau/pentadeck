import { useId, type ReactNode } from 'react'
import { cn } from './cn'

/** An on/off switch. Its hit area is grown to 44 px around the 28 px track. */
export function Switch({ checked, onChange, disabled, id, ariaLabel, labelledBy, describedBy }: {
  checked: boolean
  onChange: (checked: boolean) => void
  disabled?: boolean
  id?: string
  ariaLabel?: string
  labelledBy?: string
  describedBy?: string
}) {
  return (
    <button
      id={id}
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={ariaLabel}
      aria-labelledby={labelledBy}
      aria-describedby={describedBy}
      disabled={disabled}
      onClick={() => onChange(!checked)}
      className={cn(
        'relative inline-flex h-7 w-12 shrink-0 items-center rounded-full transition-colors duration-200',
        "before:absolute before:-inset-2 before:content-['']",
        'disabled:cursor-not-allowed disabled:opacity-40',
        checked ? 'bg-accent' : 'bg-line-strong',
      )}
    >
      <span
        aria-hidden="true"
        className={cn(
          'absolute left-0.5 size-6 rounded-full bg-white shadow-[0_1px_3px_rgb(0_0_0/0.3)] transition-transform duration-200',
          checked && 'translate-x-5',
        )}
      />
    </button>
  )
}

/** A setting that is on or off: its name and a line about it, the switch at the right. The whole row toggles. */
export function SwitchRow({ title, caption, checked, onChange, disabled }: {
  title: ReactNode
  caption?: ReactNode
  checked: boolean
  onChange: (checked: boolean) => void
  disabled?: boolean
}) {
  const id = useId()
  return (
    <div className="flex min-h-11 items-center justify-between gap-4">
      <label htmlFor={id} className={cn('min-w-0 flex-1', disabled ? 'cursor-not-allowed' : 'cursor-pointer')}>
        <span id={`${id}-t`} className={cn('block text-[15px] font-medium', disabled ? 'text-ink-3' : 'text-ink')}>{title}</span>
        {caption && <span id={`${id}-c`} className="mt-0.5 block text-[13px] leading-snug text-ink-2">{caption}</span>}
      </label>
      <Switch
        id={id}
        checked={checked}
        onChange={onChange}
        disabled={disabled}
        labelledBy={`${id}-t`}
        describedBy={caption ? `${id}-c` : undefined}
      />
    </div>
  )
}
