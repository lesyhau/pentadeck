import { useCallback, useEffect, useId, useRef, useState, type ComponentType, type RefObject } from 'react'
import {
  BulbIcon, Card, InlineNotice, ModeIcon, PowerIcon, Reveal, Switch, TriangleLeftIcon, TriangleRightIcon, cn,
  type IconProps, type Tone,
} from './ui'
import { SCREEN_PX, getScreen, type ButtonEvent, type ButtonKey } from './api'

/**
 * THE REMOTE: the five screens as they are now, and the five buttons on top of the device. Its page
 * mounts it only while open, so leaving the page stops the frames.
 */

const SCREENS = [0, 1, 2, 3, 4]

/** A round is the five frames, fetched one after another (the device serves one request at a time,
 *  so a button press waits behind one frame at most). Rounds start about 2.5 times a second, and
 *  never sooner than GAP_MS after the last one ended. Each round is 160 KB. */
const ROUND_MS = 400
const GAP_MS = 100
const ROUND_TIMEOUT_MS = 5000
/** While the device does not answer (rebooting, say), try again this often. */
const RETRY_MS = 2000
/** After a press, the next round comes this soon, to show what the press did. */
const AFTER_PRESS_MS = 150

/** The device's own timings (firmware buttons.c): any release before the long press is a short press;
 *  LIGHT and MODE are long at 800 ms and power at 1500 ms; the arrows repeat every 110 ms once held
 *  for 450 ms. */
const REPEAT_AFTER_MS = 450
const REPEAT_EVERY_MS = 110

type Press = (k: ButtonKey, e: ButtonEvent) => Promise<boolean>

/** Big-endian RGB565 onto a 128 x 128 canvas, each channel widened to 8 bits. */
function draw(canvas: HTMLCanvasElement | null | undefined, frame: Uint8Array) {
  const ctx = canvas?.getContext('2d')
  if (!ctx) return
  const img = ctx.createImageData(SCREEN_PX, SCREEN_PX)
  const px = img.data
  for (let i = 0, o = 0; i < frame.length; i += 2, o += 4) {
    const v = (frame[i] << 8) | frame[i + 1]
    const r = v >> 11
    const g = (v >> 5) & 63
    const b = v & 31
    px[o] = (r << 3) | (r >> 2)
    px[o + 1] = (g << 2) | (g >> 4)
    px[o + 2] = (b << 3) | (b >> 2)
    px[o + 3] = 255
  }
  ctx.putImageData(img, 0, 0)
}

/** Whether the element is on screen and the page is in front of someone. */
function useShown(ref: RefObject<Element>): boolean {
  const [visible, setVisible] = useState(() => document.visibilityState === 'visible')
  const [onScreen, setOnScreen] = useState(false)
  useEffect(() => {
    const onChange = () => setVisible(document.visibilityState === 'visible')
    document.addEventListener('visibilitychange', onChange)
    return () => document.removeEventListener('visibilitychange', onChange)
  }, [])
  useEffect(() => {
    const el = ref.current
    if (!el || typeof IntersectionObserver === 'undefined') { setOnScreen(true); return }
    const io = new IntersectionObserver(entries => setOnScreen(entries[entries.length - 1].isIntersecting))
    io.observe(el)
    return () => io.disconnect()
  }, [ref])
  return visible && onScreen
}

/**
 * Fetches the five frames round after round while `active`, and draws them. One round at most is
 * ever in flight: pausing lets the current round finish and starts no other.
 */
function useScreens(active: boolean) {
  const canvases = useRef<(HTMLCanvasElement | null)[]>([])
  const [answering, setAnswering] = useState<boolean | null>(null)
  const loop = useRef({
    want: false,
    busy: false,
    gone: false,
    timer: undefined as ReturnType<typeof setTimeout> | undefined,
    run: () => {},
    abort: () => {},
  })

  useEffect(() => {
    const l = loop.current
    l.gone = false
    return () => { l.gone = true; l.abort() }
  }, [])

  useEffect(() => {
    const l = loop.current
    l.want = active
    if (!active) { clearTimeout(l.timer); return }
    const round = async () => {
      clearTimeout(l.timer)
      if (l.busy || !l.want) return
      l.busy = true
      const started = performance.now()
      const ctl = new AbortController()
      l.abort = () => ctl.abort()
      const limit = setTimeout(() => ctl.abort(), ROUND_TIMEOUT_MS)
      let ok = true
      try {
        for (const s of SCREENS) draw(canvases.current[s], await getScreen(s, ctl.signal))
      } catch {
        ok = false
      }
      clearTimeout(limit)
      l.busy = false
      if (l.gone) return
      setAnswering(ok)
      if (l.want) l.timer = setTimeout(l.run, ok ? Math.max(GAP_MS, ROUND_MS - (performance.now() - started)) : RETRY_MS)
    }
    l.run = round
    round()
    return () => { l.want = false; clearTimeout(l.timer) }
  }, [active])

  /** A round soon, unless one is on its way. */
  const soon = useCallback(() => {
    const l = loop.current
    if (!l.want || l.busy) return
    clearTimeout(l.timer)
    l.timer = setTimeout(l.run, AFTER_PRESS_MS)
  }, [])

  return { canvases, answering, soon }
}

// -- The buttons ----------------------------------------------------------------------------------

interface KeySpec {
  k: ButtonKey
  name: string
  label: string
  /** What a long press does; none for the arrows, which repeat instead. */
  hold?: string
  /** When a hold becomes a long press; none for the arrows. */
  longMs?: number
  Icon: ComponentType<IconProps>
}

// U+FE0E keeps the arrows as text: iOS would draw them as emoji.
const KEYS: KeySpec[] = [
  { k: 'light', name: 'Light', label: 'LIGHT', hold: 'hold: LEDs on/off', longMs: 800, Icon: BulbIcon },
  { k: 'left', name: 'Left', label: '◀︎', Icon: TriangleLeftIcon },
  { k: 'power', name: 'Power', label: 'POWER', hold: 'hold: sleep', longMs: 1500, Icon: PowerIcon },
  { k: 'right', name: 'Right', label: '▶︎', Icon: TriangleRightIcon },
  { k: 'mode', name: 'Mode', label: 'MODE', hold: 'hold: home', longMs: 800, Icon: ModeIcon },
]

interface Hold {
  at: number
  timer?: ReturnType<typeof setTimeout>
  repeat?: ReturnType<typeof setInterval>
  long: boolean
  stopped: boolean
  sending: boolean
}

const LOOK = {
  up: 'border-line-strong bg-raised text-ink [@media(hover:hover)]:hover:border-ink-3',
  down: 'scale-[0.96] border-accent bg-accent/15 text-accent-ink',
  held: 'scale-[0.96] border-accent bg-accent text-accent-on',
}

/**
 * One button. LIGHT, power and MODE send a short press when let go before the long press, or a long press
 * once the hold reaches its threshold (and nothing on release). The arrows send "down" at once, then
 * again every 110 ms after 450 ms, as the device repeats them. A refused press stops there.
 */
function RemoteKey({ spec, onPress }: { spec: KeySpec; onPress: Press }) {
  const { k, name, label, hold, longMs, Icon } = spec
  const captionId = useId()
  const [look, setLook] = useState<keyof typeof LOOK>('up')
  const current = useRef<Hold | null>(null)
  const press = useRef(onPress)
  press.current = onPress

  const fire = (h: Hold, e: ButtonEvent) => {
    if (h.stopped) return
    h.sending = true
    press.current(k, e).then(ok => {
      h.sending = false
      if (!ok) { h.stopped = true; clearTimeout(h.timer); clearInterval(h.repeat) }
    })
  }

  const begin = () => {
    if (current.current) return
    const h: Hold = { at: performance.now(), long: false, stopped: false, sending: false }
    current.current = h
    setLook('down')
    if (longMs) {
      h.timer = setTimeout(() => {
        h.long = true
        setLook('held')
        navigator.vibrate?.(12)
        fire(h, 'long')
      }, longMs)
    } else {
      fire(h, 'down')
      // A repeat is skipped while the last one is still on its way, so slow answers never pile up.
      const again = () => { if (!h.sending) fire(h, 'down') }
      h.timer = setTimeout(() => {
        again()
        h.repeat = setInterval(again, REPEAT_EVERY_MS)
      }, REPEAT_AFTER_MS)
    }
  }

  /** Let go: `commit` false when the press was taken away (a scroll, the page hidden). */
  const end = (commit: boolean) => {
    const h = current.current
    if (!h) return
    current.current = null
    setLook('up')
    clearTimeout(h.timer)
    clearInterval(h.repeat)
    if (commit && longMs && !h.long) fire(h, 'short')
    h.stopped = true
  }
  const endRef = useRef(end)
  endRef.current = end

  useEffect(() => {
    const onHide = () => { if (document.hidden) endRef.current(false) }
    document.addEventListener('visibilitychange', onHide)
    return () => { document.removeEventListener('visibilitychange', onHide); endRef.current(false) }
  }, [])

  const isKey = (key: string) => key === ' ' || key === 'Enter'

  return (
    <div className="flex min-w-0 flex-col items-center text-center">
      <button
        type="button"
        aria-label={name}
        aria-describedby={hold ? captionId : undefined}
        onPointerDown={e => {
          if (e.button !== 0) return
          e.preventDefault()
          e.currentTarget.setPointerCapture(e.pointerId)
          begin()
        }}
        onPointerUp={() => end(true)}
        onPointerCancel={() => end(false)}
        onLostPointerCapture={() => end(false)}
        onContextMenu={e => e.preventDefault()}
        onKeyDown={e => { if (isKey(e.key)) { e.preventDefault(); if (!e.repeat) begin() } }}
        onKeyUp={e => { if (isKey(e.key)) end(true) }}
        onBlur={() => end(false)}
        className={cn(
          'flex h-12 w-full max-w-36 touch-manipulation select-none items-center justify-center rounded-lg border shadow-card sm:h-14',
          '[-webkit-touch-callout:none] transition-[transform,background-color,border-color,color] duration-100',
          LOOK[look],
        )}
      >
        <Icon size={20} />
      </button>
      <span aria-hidden="true" className="mt-2 text-[11px] font-medium leading-none tracking-[0.08em] text-ink">{label}</span>
      {hold && <span id={captionId} className="mt-1 text-[11px] leading-tight text-ink-2">{hold}</span>}
    </div>
  )
}

// -- The card -------------------------------------------------------------------------------------

export function Remote({ mode, onPress, notice, className }: {
  /** The device's mode, from /api/status. */
  mode?: string
  /** Sends a press; false when it was refused or not delivered. */
  onPress: Press
  notice: { tone: Tone; text: string } | null
  className?: string
}) {
  const [live, setLive] = useState(true)
  const strip = useRef<HTMLDivElement>(null)
  const shown = useShown(strip)
  const { canvases, answering, soon } = useScreens(live && shown)
  const liveId = useId()

  const press = useCallback<Press>(async (k, e) => {
    const ok = await onPress(k, e)
    if (ok) soon()
    return ok
  }, [onPress, soon])

  const overlay = !live ? 'Paused' : answering === false ? 'Not answering' : null

  return (
    <Card
      title={mode ? `Screens · ${mode}` : 'Screens'}
      className={className}
      right={
        <>
          <span id={liveId} className="text-[11px] font-medium uppercase tracking-[0.12em] text-ink-2">Live</span>
          <Switch checked={live} onChange={setLive} labelledBy={liveId} />
        </>
      }
    >
      {/* As wide as the page allows: the 128-pixel frames are scaled up, pixel for pixel. */}
      <div className="select-none [-webkit-touch-callout:none]">
        <div ref={strip} className="relative">
          <div
            role="img"
            aria-label={`The device's five screens${live ? '' : ', paused'}`}
            className="grid grid-cols-5 gap-1 rounded-lg bg-black p-1.5 ring-1 ring-line sm:gap-2 sm:p-2"
          >
            {SCREENS.map(s => (
              <canvas
                key={s}
                ref={el => { canvases.current[s] = el }}
                width={SCREEN_PX}
                height={SCREEN_PX}
                className={cn('aspect-square w-full [image-rendering:pixelated] transition-opacity duration-300', overlay && 'opacity-40')}
              />
            ))}
          </div>
          {overlay && (
            <div className="pointer-events-none absolute inset-0 grid place-items-center">
              <span role="status" className="rounded-md bg-black/75 px-2.5 py-1 text-xs font-medium text-white">{overlay}</span>
            </div>
          )}
        </div>

        <div className="mt-4 grid grid-cols-5 items-start gap-1 px-1.5 sm:mt-5 sm:gap-2 sm:px-2">
          {KEYS.map(spec => <RemoteKey key={spec.k} spec={spec} onPress={press} />)}
        </div>
      </div>

      <Reveal show={!!notice}>
        {notice && <InlineNotice tone={notice.tone}>{notice.text}</InlineNotice>}
      </Reveal>
    </Card>
  )
}
