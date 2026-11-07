import { useRef, useState, type DragEvent, type ReactNode } from 'react'
import { cn } from './cn'
import { UploadIcon } from './icons'

/**
 * A place to drop a file, or tap to choose one. Never `disabled` while busy: a disabled button
 * lets a dropped file through to the browser, which would open it in place of the page.
 */
export function FileDrop({ accept, onFile, busy, label, hint, icon }: {
  accept?: string
  onFile: (file: File) => void
  busy?: boolean
  label: ReactNode
  hint?: ReactNode
  icon?: ReactNode
}) {
  const input = useRef<HTMLInputElement>(null)
  const depth = useRef(0)
  const [over, setOver] = useState(false)

  const stop = (e: DragEvent) => { e.preventDefault(); e.stopPropagation() }
  const onDrop = (e: DragEvent) => {
    stop(e)
    depth.current = 0
    setOver(false)
    const file = e.dataTransfer.files[0]
    if (file && !busy) onFile(file)
  }

  return (
    <>
      <button
        type="button"
        aria-disabled={busy || undefined}
        onClick={() => { if (!busy) input.current?.click() }}
        onDragEnter={e => { stop(e); depth.current += 1; if (!busy) setOver(true) }}
        onDragOver={stop}
        onDragLeave={e => { stop(e); depth.current -= 1; if (depth.current <= 0) { depth.current = 0; setOver(false) } }}
        onDrop={onDrop}
        className={cn(
          'flex min-h-28 w-full flex-col items-center justify-center gap-1.5 rounded-lg border border-dashed px-4 py-5 text-center transition-colors duration-150',
          over ? 'border-accent bg-accent/10' : 'border-line-strong bg-field hover:border-ink-3',
          busy && 'cursor-not-allowed opacity-60 hover:border-line-strong',
        )}
      >
        <span className="mb-1 grid size-10 place-items-center rounded-full bg-accent/10 text-accent-ink">
          {icon ?? <UploadIcon size={20} />}
        </span>
        <span className="max-w-full break-all text-[15px] font-medium text-ink">{label}</span>
        {hint && <span className="text-[13px] text-ink-2">{hint}</span>}
      </button>
      <input
        ref={input}
        type="file"
        accept={accept}
        hidden
        onChange={e => {
          const file = e.target.files?.[0]
          if (file) onFile(file)
          e.target.value = ''
        }}
      />
    </>
  )
}

/** How far something has got: its name and figures over a bar. */
export function Progress({ label, figures, value }: { label: string; figures?: string; value?: number }) {
  const pct = value === undefined ? undefined : Math.max(0, Math.min(100, value))
  return (
    <div>
      <div className="mb-2 flex items-baseline justify-between gap-3 text-[13px]">
        <span className="font-medium text-ink">{label}</span>
        {figures && <span className="tabular-nums text-ink-2">{figures}</span>}
      </div>
      <div
        role="progressbar"
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={pct === undefined ? undefined : Math.round(pct)}
        className="h-1.5 overflow-hidden rounded-full bg-line-strong/60"
      >
        <div className="h-full rounded-full bg-accent transition-[width] duration-200" style={{ width: `${pct ?? 0}%` }} />
      </div>
    </div>
  )
}
