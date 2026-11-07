import { cn } from './cn'
import { CloseIcon } from './icons'
import { TONE_ICON, TONE_TEXT, type Tone } from './tone'

export interface ToastItem {
  id: number
  tone: Tone
  text: string
  /** Set while it fades out, just before it is removed. */
  leaving?: boolean
}

/** Short-lived messages, stacked under the header, centred. */
export function Toasts({ items, onDismiss }: { items: ToastItem[]; onDismiss: (id: number) => void }) {
  return (
    <div className="pointer-events-none fixed inset-x-0 top-[calc(var(--pd-header)+env(safe-area-inset-top)+8px)] z-40 flex flex-col items-center gap-2 px-4">
      {items.map(t => {
        const Icon = TONE_ICON[t.tone]
        return (
          <div
            key={t.id}
            role="status"
            className={cn(
              'pointer-events-auto flex w-full max-w-sm items-start gap-3 rounded-xl border border-line-strong bg-raised py-0.5 pl-4 pr-0.5 shadow-pop',
              t.leaving ? 'pd-toast-out' : 'pd-toast-in',
            )}
          >
            <Icon className={cn('mt-3 shrink-0', TONE_TEXT[t.tone])} />
            <p className="min-w-0 flex-1 py-2.5 text-sm text-ink">{t.text}</p>
            <button
              type="button"
              onClick={() => onDismiss(t.id)}
              aria-label="Dismiss"
              className="grid size-11 shrink-0 place-items-center rounded-lg text-ink-2 transition-colors hover:text-ink"
            >
              <CloseIcon size={16} />
            </button>
          </div>
        )
      })}
    </div>
  )
}
