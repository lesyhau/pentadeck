import { forwardRef, useId, type InputHTMLAttributes, type ReactNode } from 'react'
import { cn } from './cn'
import { ChevronDownIcon } from './icons'

/** The well every text control sits in. 16 px text, or iOS zooms the page on focus. */
const CONTROL = cn(
  'block h-11 w-full rounded-lg border border-line-strong bg-field px-3 text-base text-ink',
  'placeholder:text-ink-3 transition-[border-color,box-shadow] duration-150',
  'focus:border-accent focus:shadow-[0_0_0_3px_rgb(var(--pd-accent)/0.22)] focus-visible:outline-none',
  'disabled:cursor-not-allowed disabled:opacity-50',
)
const CONTROL_ERROR = 'border-danger focus:border-danger focus:shadow-[0_0_0_3px_rgb(var(--pd-danger)/0.22)]'

const messageId = (id: string) => `${id}-msg`

/**
 * A label over a control, and a hint or an error under it. `group` is for a label over several
 * controls (a list, a segmented control): it names the group instead of one input.
 */
export function Field({ id, label, hint, error, aside, group, children, className }: {
  id: string
  label: ReactNode
  hint?: ReactNode
  error?: ReactNode
  /** Something at the label's right, such as a readout. */
  aside?: ReactNode
  group?: boolean
  children: ReactNode
  className?: string
}) {
  const labelId = `${id}-label`
  const labelClass = 'text-sm font-medium text-ink'
  return (
    <div className={cn('min-w-0', className)} role={group ? 'group' : undefined} aria-labelledby={group ? labelId : undefined}>
      <div className="mb-1.5 flex items-baseline justify-between gap-3">
        {group
          ? <div id={labelId} className={labelClass}>{label}</div>
          : <label id={labelId} htmlFor={id} className={labelClass}>{label}</label>}
        {aside}
      </div>
      {children}
      {error
        ? <p id={messageId(id)} className="mt-1.5 text-[13px] leading-snug text-danger-ink">{error}</p>
        : hint && <p id={messageId(id)} className="mt-1.5 text-[13px] leading-snug text-ink-2">{hint}</p>}
    </div>
  )
}

export interface InputProps extends Omit<InputHTMLAttributes<HTMLInputElement>, 'size'> {
  label: ReactNode
  hint?: ReactNode
  error?: string
  /** A unit shown inside the field at its right: °C, ms. */
  suffix?: string
  /** A control inside the field at its right, such as a button. */
  trailing?: ReactNode
}

export const Input = forwardRef<HTMLInputElement, InputProps>(function Input(
  { label, hint, error, suffix, trailing, id, className, ...rest },
  ref,
) {
  const auto = useId()
  const fid = id ?? auto
  return (
    <Field id={fid} label={label} hint={hint} error={error}>
      <div className="relative">
        <input
          ref={ref}
          id={fid}
          aria-invalid={error ? true : undefined}
          aria-describedby={error || hint ? messageId(fid) : undefined}
          className={cn(CONTROL, error && CONTROL_ERROR, suffix && 'pr-10', !!trailing && 'pr-12', className)}
          {...rest}
        />
        {suffix && (
          <span aria-hidden="true" className="pointer-events-none absolute inset-y-0 right-3 flex items-center text-sm text-ink-2">
            {suffix}
          </span>
        )}
        {trailing && <div className="absolute inset-y-0 right-0 flex items-center">{trailing}</div>}
      </div>
    </Field>
  )
})

export interface Option<T extends string = string> {
  value: T
  label: string
}

/** The native select, dressed: on a phone it opens the system picker. */
export function Select<T extends string>({ value, options, onChange, ariaLabel, id, className }: {
  value: T
  options: Option<T>[]
  onChange: (value: T) => void
  ariaLabel?: string
  id?: string
  className?: string
}) {
  return (
    <div className={cn('relative', className)}>
      <select
        id={id}
        aria-label={ariaLabel}
        value={value}
        onChange={e => onChange(e.target.value as T)}
        className={cn(CONTROL, 'cursor-pointer appearance-none pr-9')}
      >
        {options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
      </select>
      <ChevronDownIcon size={16} className="pointer-events-none absolute right-3 top-1/2 -translate-y-1/2 text-ink-2" />
    </div>
  )
}
