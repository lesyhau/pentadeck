import { useRef } from 'react'
import { Button } from './Button'
import { cn } from './cn'

export interface SaveStatus {
  tone: 'success' | 'danger'
  text: string
}

/**
 * The bar that rises while there is something to save or to say about a save: from the tab bar on
 * a phone, from the bottom edge from `lg`, where the tabs are in the header. While it sinks it keeps
 * its last words, so it does not go blank on the way down.
 */
export function SaveBar({ dirty, saving, status, onUndo, onSave }: {
  dirty: boolean
  saving: boolean
  status: SaveStatus | null
  onUndo: () => void
  onSave: () => void
}) {
  const shown = dirty || !!status
  const words = status ?? (dirty ? { tone: undefined, text: saving ? 'Saving…' : 'Unsaved changes' } : null)
  const last = useRef(words)
  if (shown) last.current = words
  const said = shown ? words : last.current

  return (
    <div
      className={cn(
        'fixed inset-x-0 bottom-[calc(var(--pd-tabbar)+env(safe-area-inset-bottom))] z-30 border-t border-line bg-surface/95 backdrop-blur-md',
        'lg:bottom-0 lg:pb-[env(safe-area-inset-bottom)]',
        'transition-[transform,visibility] duration-300 ease-[cubic-bezier(0.2,0.7,0.2,1)]',
        shown ? 'visible translate-y-0' : 'invisible translate-y-full',
      )}
    >
      <div className="pd-wrap flex items-center gap-3 py-3">
        <p
          role="status"
          className={cn(
            'mr-auto min-w-0 text-sm',
            said?.tone === 'success' ? 'text-accent-ink' : said?.tone === 'danger' ? 'text-danger-ink' : 'text-ink-2',
          )}
        >
          {said?.text}
        </p>
        {(dirty || (!shown && last.current?.tone === undefined)) && (
          <div className="flex shrink-0 gap-2 sm:gap-3">
            <Button variant="secondary" onClick={onUndo} disabled={saving || !dirty}>Undo</Button>
            <Button onClick={onSave} loading={saving} disabled={!dirty} className="min-w-[96px]">Save</Button>
          </div>
        )}
      </div>
    </div>
  )
}
