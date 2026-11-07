import { forwardRef, type ButtonHTMLAttributes, type ReactNode } from 'react'
import { cn } from './cn'

export type ButtonVariant = 'primary' | 'secondary' | 'ghost'

const VARIANT: Record<ButtonVariant, string> = {
  primary: 'bg-accent text-accent-on hover:bg-accent-hover',
  secondary: 'border border-line-strong bg-surface text-ink hover:bg-raised hover:border-ink-3',
  ghost: 'text-ink-2 hover:bg-raised hover:text-ink',
}

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant
  /** Shows a spinner in place of the icon; the button stays its size. */
  loading?: boolean
  icon?: ReactNode
}

/** 44 px tall everywhere, so every button is a comfortable tap target. */
export const Button = forwardRef<HTMLButtonElement, ButtonProps>(function Button(
  { variant = 'primary', loading, icon, className, children, disabled, type = 'button', ...rest },
  ref,
) {
  return (
    <button
      ref={ref}
      type={type}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={cn(
        'inline-flex h-11 select-none items-center justify-center gap-2 whitespace-nowrap rounded-lg px-4 text-[15px] font-medium',
        'transition-colors duration-150 disabled:cursor-not-allowed disabled:opacity-50',
        VARIANT[variant],
        className,
      )}
      {...rest}
    >
      {loading ? <Spinner /> : icon}
      {children}
    </button>
  )
})

export function Spinner({ className }: { className?: string }) {
  return <span aria-hidden="true" className={cn('size-4 shrink-0 animate-spin rounded-full border-2 border-current border-r-transparent', className)} />
}

/** A row of buttons: equal shares of the width on a phone, as wide as their words from `sm`. */
export function ButtonRow({ children, className }: { children: ReactNode; className?: string }) {
  return <div className={cn('flex flex-wrap gap-3 [&>*]:flex-1 sm:[&>*]:flex-none', className)}>{children}</div>
}
