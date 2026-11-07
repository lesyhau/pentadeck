import { useEffect, useId, useRef, type ReactNode } from 'react'
import { Button } from './Button'
import { cn } from './cn'
import { AlertIcon } from './icons'

const MARK = {
  warn: 'bg-warn/15 text-warn-ink',
  accent: 'bg-accent/15 text-accent-ink',
}

/**
 * A native modal <dialog>: the page behind is inert, and Escape or a tap outside closes it. An icon
 * in a round mark, a title, a line about it, anything else under that, and the answers at the foot.
 */
export function Dialog({ title, description, icon, tone = 'warn', children, actions, onClose }: {
  title: ReactNode
  description?: ReactNode
  icon?: ReactNode
  tone?: keyof typeof MARK
  children?: ReactNode
  /** The buttons at the foot: side by side on a phone, at the right from `sm`. */
  actions: ReactNode
  onClose: () => void
}) {
  const ref = useRef<HTMLDialogElement>(null)
  const id = useId()

  useEffect(() => {
    const dialog = ref.current
    if (dialog && !dialog.open) dialog.showModal()
    return () => dialog?.close()
  }, [])

  return (
    <dialog
      ref={ref}
      className="pd-dialog"
      aria-labelledby={`${id}-t`}
      aria-describedby={description ? `${id}-d` : undefined}
      onCancel={e => { e.preventDefault(); onClose() }}
      onClick={e => { if (e.target === e.currentTarget) onClose() }}
    >
      <div className="pd-dialog-panel p-5 sm:p-6">
        <div className="flex items-start gap-4">
          <span className={cn('grid size-10 shrink-0 place-items-center rounded-full transition-colors', MARK[tone])}>
            {icon ?? <AlertIcon size={20} />}
          </span>
          <div className="min-w-0 pt-1">
            <h2 id={`${id}-t`} className="text-[17px] font-medium text-ink">{title}</h2>
            {description && <p id={`${id}-d`} className="mt-1.5 text-sm leading-snug text-ink-2">{description}</p>}
          </div>
        </div>
        {children && <div className="mt-5">{children}</div>}
        <div className="mt-6 flex gap-3 [&>*]:flex-1 sm:justify-end sm:[&>*]:flex-none">{actions}</div>
      </div>
    </dialog>
  )
}

/** Asks before something that cannot be taken back. Focus starts on Cancel, the safe answer. */
export function ConfirmDialog({ title, description, confirmLabel, cancelLabel = 'Cancel', icon, onConfirm, onCancel }: {
  title: string
  description?: ReactNode
  confirmLabel: string
  cancelLabel?: string
  icon?: ReactNode
  onConfirm: () => void
  onCancel: () => void
}) {
  return (
    <Dialog
      title={title}
      description={description}
      icon={icon}
      onClose={onCancel}
      actions={(
        <>
          <Button variant="secondary" onClick={onCancel} autoFocus>{cancelLabel}</Button>
          <Button onClick={onConfirm}>{confirmLabel}</Button>
        </>
      )}
    />
  )
}
