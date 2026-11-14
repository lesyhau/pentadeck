import { useCallback, useEffect, useRef, useState } from 'react'
import { Button, HudHeading, InlineNotice, Meter, Slider, SpeakerIcon, StatTile, StopIcon, cn, type MeterTone } from '../ui'
import { getJson, refusal, streamUrl, type DeviceStatus, type Spectrum, type WriteResult } from '../lib/api'
import type { Send } from './Pairing'

/**
 * SOUND, on the overview: the screens' 16-band spectrum live, the room in this browser's speakers
 * (the device's mic stream, port 8081), and the day's sound dose.
 */

const SPECTRUM_MS = 120
const RATE = 32000
/** How far ahead of the speakers the stream is scheduled: room for Wi-Fi's hiccups. */
const LEAD_S = 0.25
/** Further ahead than this (after a stall), drop what is queued and catch up. */
const MAX_LAG_S = 1.0

/** /api/spectrum several times a second while the page is in front of someone. */
function useSpectrum(): Spectrum | null {
  const [spec, setSpec] = useState<Spectrum | null>(null)
  useEffect(() => {
    let stopped = false
    let timer: ReturnType<typeof setTimeout> | undefined
    const tick = async () => {
      if (stopped) return
      if (!document.hidden) {
        try { setSpec(await getJson<Spectrum>('/api/spectrum', 2000)) } catch { /* the header's dot says so */ }
      }
      if (!stopped) timer = setTimeout(tick, document.hidden ? 1000 : SPECTRUM_MS)
    }
    tick()
    return () => { stopped = true; clearTimeout(timer) }
  }, [])
  return spec
}

/**
 * The mic stream into Web Audio. The WAV's 44-byte header is skipped (the format is fixed: 16-bit
 * mono at 32 kHz), and the samples go out in short buffers scheduled back to back.
 */
function useListen() {
  const [on, setOn] = useState(false)
  const [volume, setVolume] = useState(80)
  const volumeRef = useRef(volume)
  volumeRef.current = volume
  const run = useRef<{ ctl: AbortController; ctx: AudioContext; gain: GainNode } | null>(null)

  const stop = useCallback(() => {
    const r = run.current
    run.current = null
    if (r) { r.ctl.abort(); r.ctx.close().catch(() => {}) }
    setOn(false)
  }, [])

  useEffect(() => { if (run.current) run.current.gain.gain.value = (volume / 100) ** 2 }, [volume])
  useEffect(() => stop, [stop])

  const start = useCallback(async (token: string): Promise<WriteResult> => {
    stop()
    // Made inside the click, so the browser lets it play.
    const ctx = new AudioContext()
    const gain = ctx.createGain()
    gain.gain.value = (volumeRef.current / 100) ** 2
    gain.connect(ctx.destination)
    const ctl = new AbortController()
    let r: Response
    try {
      r = await fetch(streamUrl(token), { cache: 'no-store', signal: ctl.signal })
    } catch {
      ctx.close().catch(() => {})
      return { ok: false, status: 0, text: 'The mic stream is not answering.' }
    }
    if (!r.ok || !r.body) {
      ctx.close().catch(() => {})
      const text = (await r.text().catch(() => '')).trim()
      return {
        ok: false,
        status: r.status,
        text: /turned off/.test(text)
          ? 'Turn on “Let PCs use the mic” in Settings first.'
          : /too many/.test(text) ? 'Too many are listening already.' : text,
      }
    }
    run.current = { ctl, ctx, gain }
    setOn(true)
    const reader = r.body.getReader()
    let skip = 44
    let carry: number | null = null      // an odd byte left over from the last chunk
    let at = 0
    void (async () => {
      try {
        for (;;) {
          const { done, value } = await reader.read()
          if (done || run.current?.ctl !== ctl) break
          let bytes = value
          if (skip > 0) {
            const n = Math.min(skip, bytes.length)
            skip -= n
            bytes = bytes.subarray(n)
          }
          if (carry !== null && bytes.length) {
            const b = new Uint8Array(bytes.length + 1)
            b[0] = carry
            b.set(bytes, 1)
            bytes = b
            carry = null
          }
          if (bytes.length & 1) { carry = bytes[bytes.length - 1]; bytes = bytes.subarray(0, bytes.length - 1) }
          const n = bytes.length >> 1
          if (!n) continue
          const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.length)
          const buf = ctx.createBuffer(1, n, RATE)
          const ch = buf.getChannelData(0)
          for (let i = 0; i < n; i++) ch[i] = view.getInt16(i * 2, true) / 32768
          const now = ctx.currentTime
          if (at < now + 0.02 || at > now + MAX_LAG_S) at = now + LEAD_S
          const src = ctx.createBufferSource()
          src.buffer = buf
          src.connect(gain)
          src.start(at)
          at += buf.duration
        }
      } catch { /* aborted, or the connection dropped */ }
      if (run.current?.ctl === ctl) stop()
    })()
    return { ok: true, status: 200, text: '' }
  }, [stop])

  return { on, start, stop, volume, setVolume }
}

/** At 100 % a hearing-safety meter would call the day's sound a full workday's limit. */
const doseTone = (pct: number): MeterTone => (pct >= 100 ? 'danger' : pct >= 50 ? 'warn' : 'accent')

/** Below about 20 dBA the mic hears nothing at all (or is not there): no figure beats a wrong one. */
const heard = (v: number | null | undefined) => (v == null || v < 20 ? undefined : Math.round(v))

export function SoundSection({ status, send }: { status: DeviceStatus | null; send: Send }) {
  const spec = useSpectrum()
  const listen = useListen()
  const [problem, setProblem] = useState<string | null>(null)
  const [starting, setStarting] = useState(false)
  const loading = !status
  const dose = status?.hearing?.dose_pct
  const leq = heard(status?.hearing?.leq_dba)
  const now = heard(spec?.dba ?? status?.mic.dba)

  const toggle = async () => {
    setProblem(null)
    if (listen.on) { listen.stop(); return }
    setStarting(true)
    const r = await send(t => listen.start(t), { label: 'Listen', run: toggle })
    setStarting(false)
    if (!r.ok && r.text) setProblem(refusal(r))
  }

  return (
    <section aria-label="Sound">
      <HudHeading
        right={listen.on && (
          <span className="flex items-center gap-2 text-[11px] font-medium uppercase tracking-[0.12em] text-ink-2">
            <span className="size-2 animate-pulse rounded-full bg-danger" aria-hidden="true" />
            Listening
          </span>
        )}
      >
        Sound
      </HudHeading>
      <div className="mt-3 grid gap-3 lg:grid-cols-[minmax(0,2fr)_minmax(0,1fr)] lg:gap-6">
        <div className="rounded-xl border border-line bg-surface p-4 shadow-card">
          <SpectrumBars bands={spec?.bands} />
          <div className="mt-4 flex flex-wrap items-center gap-x-5 gap-y-3">
            <Button
              variant={listen.on ? 'secondary' : 'primary'}
              onClick={toggle}
              loading={starting}
              icon={listen.on ? <StopIcon size={16} /> : <SpeakerIcon size={16} />}
            >
              {listen.on ? 'Stop' : 'Listen'}
            </Button>
            {listen.on ? (
              <div className="min-w-[12rem] flex-1">
                <Slider label="Volume" value={listen.volume} onChange={listen.setVolume} format={v => `${v}%`} />
              </div>
            ) : (
              <p className="min-w-0 flex-1 text-[13px] leading-snug text-ink-2">The room, live in this browser’s speakers.</p>
            )}
          </div>
          {problem && <div className="mt-3"><InlineNotice tone="warning">{problem}</InlineNotice></div>}
        </div>
        <div className="grid grid-cols-3 gap-2 sm:gap-3 lg:grid-cols-1">
          <StatTile label="Now" value={now} unit={now === undefined ? undefined : 'dBA'} sub="A-weighted" loading={loading} />
          <StatTile
            label="Today’s dose"
            value={dose == null ? undefined : Math.round(dose)}
            unit={dose == null ? undefined : '%'}
            meter={<Meter value={dose ?? 0} tone={dose == null ? 'muted' : doseTone(dose)} />}
            loading={loading}
          />
          <StatTile label="Today’s average" value={leq} unit={leq === undefined ? undefined : 'dBA'} sub="Since midnight" loading={loading} />
        </div>
      </div>
    </section>
  )
}

/** 16 bars, bass on the left, as on the AUDIO screen. Heights ease between readings. */
function SpectrumBars({ bands }: { bands?: number[] }) {
  const values = bands?.length === 16 ? bands : Array<number>(16).fill(0)
  return (
    <div>
      <div className="flex h-36 items-end gap-[2px] sm:h-44" role="img" aria-label="The spectrum the device hears now, bass on the left">
        {values.map((v, i) => (
          <div key={i} className="flex h-full flex-1 items-end justify-center">
            <div
              className={cn('w-full max-w-6 rounded-t-[4px] transition-[height] duration-150 ease-out', v > 85 ? 'bg-warn' : 'bg-accent')}
              style={{ height: `${Math.max(1.5, v)}%` }}
            />
          </div>
        ))}
      </div>
      <div className="mt-2 flex justify-between border-t border-line pt-1.5 text-[11px] text-ink-3">
        <span>Bass</span>
        <span>Treble</span>
      </div>
    </div>
  )
}
