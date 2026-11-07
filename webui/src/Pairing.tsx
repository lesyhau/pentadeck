import { useCallback, useEffect, useRef, useState } from 'react'
import { Button, CheckCircleIcon, Dialog, InlineNotice, LinkIcon, Spinner } from './ui'
import { pairPoll, pairStart, type WriteResult } from './api'

/**
 * PAIRING: this browser asks the device, the device's screens ask its owner ("PAIR THIS BROWSER?
 * 4821"), and a press of power there hands this browser the token every write needs. No token is
 * ever typed: the device's button is the proof.
 */

/** What to do again once paired: the action a refusal interrupted. */
export interface Retry {
  label: string
  run: () => void
}

/**
 * Sends a write with this browser's token. Without one, nothing is sent and the pairing dialog
 * opens; a refusal (401, 403) opens it too. Either way the result says what happened.
 */
export type Send = (go: (token: string) => Promise<WriteResult>, retry?: Retry) => Promise<WriteResult>

export type PairPhase =
  | { k: 'idle' }
  | { k: 'starting' }
  | { k: 'asking'; id: string; code: string }
  | { k: 'done' }
  | { k: 'no'; why: string }

/** The device shows its question for 60 s; past this, the page stops waiting. */
const GIVE_UP_MS = 70_000
const POLL_MS = 1000

/** One pairing: start it, then it polls until the device answers, refuses, or time runs out. */
export function usePairing(onToken: (token: string) => void) {
  const [phase, setPhase] = useState<PairPhase>({ k: 'idle' })
  const got = useRef(onToken)
  got.current = onToken

  useEffect(() => {
    if (phase.k !== 'asking') return
    let alive = true
    const started = Date.now()
    const tick = async () => {
      if (!alive) return
      const r = await pairPoll(phase.id)
      if (!alive) return
      if (r.state === 'allowed') { got.current(r.token); setPhase({ k: 'done' }); return }
      if (r.state === 'refused') { setPhase({ k: 'no', why: 'Refused on the device, or no answer in time.' }); return }
      if (Date.now() - started > GIVE_UP_MS) { setPhase({ k: 'no', why: 'No answer from the device.' }); return }
      timer = window.setTimeout(tick, POLL_MS)
    }
    let timer = window.setTimeout(tick, POLL_MS)
    return () => { alive = false; window.clearTimeout(timer) }
  }, [phase])

  const start = useCallback(async () => {
    setPhase({ k: 'starting' })
    const r = await pairStart()
    setPhase(r.ok
      ? { k: 'asking', id: r.id, code: r.code }
      : { k: 'no', why: r.busy ? 'The device is already asking. Try again in a few seconds.' : 'The device is not answering.' })
  }, [])
  const reset = useCallback(() => setPhase({ k: 'idle' }), [])

  return { phase, start, reset }
}

/** The code both sides show, and what to do with it, while the device waits for its owner. */
export function PairCode({ code }: { code: string }) {
  return (
    <div className="pd-fade-in rounded-lg border border-accent/40 bg-accent/10 px-4 py-4 text-center" role="status">
      <div className="text-[11px] font-medium uppercase tracking-[0.12em] text-ink-2">Code on the screens</div>
      <div className="mt-1.5 font-display text-[32px] font-bold leading-none tracking-[0.28em] text-ink tabular-nums [padding-left:0.28em]">
        {code}
      </div>
      <p className="mx-auto mt-3 max-w-[32ch] text-[13px] leading-snug text-ink-2">
        The device asks <b className="font-medium text-ink">Pair this browser?</b> If the code matches, press{' '}
        <b className="font-medium text-ink">power</b> on the device.
      </p>
      <p className="mt-3 flex items-center justify-center gap-2 text-xs text-ink-2">
        <Spinner className="size-3" /> Waiting for the device…
      </p>
    </div>
  )
}

/**
 * The pairing dialog, over whatever page refused a change. Once paired, the interrupted action is
 * one tap away (`retry`), or the person simply does it again.
 */
export function PairDialog({ refused, retry, onToken, onRetry, onClose }: {
  /** This browser had a token and the device turned it away (not just no token yet). */
  refused: boolean
  retry?: Retry
  onToken: (token: string) => void
  onRetry: () => void
  onClose: () => void
}) {
  const { phase, start } = usePairing(onToken)
  const done = phase.k === 'done'

  const title = done ? 'Paired' : phase.k === 'asking' ? 'Check the code' : phase.k === 'no' ? 'Not paired' : 'Pair this browser'
  const description = done
    ? retry ? 'This browser can change things on the device now.' : 'This browser can change things on the device now. Try that again.'
    : phase.k === 'asking' ? undefined
      : refused
        ? 'The device did not accept this browser. Pair it again to go on.'
        : 'To change anything on the device, pair this browser with it first. It takes one press of the power button.'

  return (
    <Dialog
      title={title}
      description={description}
      tone="accent"
      icon={done ? <CheckCircleIcon size={20} /> : <LinkIcon size={20} />}
      onClose={onClose}
      actions={
        done ? (
          retry ? (
            <>
              <Button key="close" variant="secondary" onClick={onClose}>Close</Button>
              <Button key="retry" onClick={onRetry} autoFocus>{retry.label}</Button>
            </>
          ) : <Button key="done" onClick={onClose} autoFocus>Done</Button>
        ) : phase.k === 'asking' ? (
          <Button key="cancel-asking" variant="secondary" onClick={onClose} autoFocus>Cancel</Button>
        ) : (
          <>
            <Button key="cancel" variant="secondary" onClick={onClose}>Cancel</Button>
            <Button key={phase.k === 'no' ? 'again' : 'pair'} onClick={start} loading={phase.k === 'starting'} autoFocus>
              {phase.k === 'no' ? 'Try again' : 'Pair with the device'}
            </Button>
          </>
        )
      }
    >
      {phase.k === 'asking' ? <PairCode code={phase.code} /> : phase.k === 'no' ? <InlineNotice tone="warning">{phase.why}</InlineNotice> : null}
    </Dialog>
  )
}
