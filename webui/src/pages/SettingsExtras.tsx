import { useCallback, useEffect, useRef, useState } from 'react'
import { Button, ButtonRow, Card, FileDrop, ImageIcon, Input, NoticeLine, Progress, Switch, type Note } from '../ui'
import { EMBLEM_PX, getJson, postBinary, post, refusal, soundsPost, type SoundsState } from '../lib/api'
import type { Send } from '../components/Pairing'

/**
 * THE SETTINGS THAT TAKE EFFECT AT ONCE: learned sounds and the clock image live on the device
 * outside the settings, so these cards act straight away - no Save.
 */

// -- Learned sounds ---------------------------------------------------------------------------------

const LEARN_MS = 4000
const NAME_MAX = 15

export function LearnedSoundsCard({ send }: { send: Send }) {
  const [state, setState] = useState<SoundsState | null>(null)
  const [missing, setMissing] = useState(false)
  const [names, setNames] = useState(['', '', ''])
  const [busy, setBusy] = useState<number | null>(null)
  const [notice, setNotice] = useState<Note | null>(null)
  const learning = state?.learning ?? -1
  const wasLearning = useRef(-1)

  const load = useCallback(async () => {
    try { setState(await getJson<SoundsState>('/api/sounds')) } catch { setMissing(true) }
  }, [])
  useEffect(() => { load() }, [load])

  // While a sound is being learned, follow it; say how it went when it ends.
  useEffect(() => {
    if (learning < 0) {
      if (wasLearning.current >= 0 && state) {
        setNotice(state.result === 'learned'
          ? { tone: 'success', text: `Learned. The screens show ${state.slots[wasLearning.current]?.name} when it is heard.` }
          : { tone: 'warning', text: 'Nothing loud enough was heard. Make the sound closer to the device, and try again.' })
      }
      wasLearning.current = -1
      return
    }
    wasLearning.current = learning
    const t = setTimeout(load, 300)
    return () => clearTimeout(t)
  }, [learning, state, load])

  const act = async (slot: number, query: string, retry: () => void) => {
    setBusy(slot)
    setNotice(null)
    const r = await send(t => soundsPost(t, slot, query), { label: 'Try again', run: retry })
    setBusy(null)
    if (r.ok) {
      try { setState(JSON.parse(r.text) as SoundsState) } catch { load() }
    } else if (r.text || r.status) {
      setNotice({ tone: 'danger', text: refusal(r) })
    }
    return r.ok
  }
  const learn = async (slot: number) => {
    const name = names[slot].trim().toUpperCase()
    if (!name) { setNotice({ tone: 'warning', text: 'Give the sound a name first.' }); return }
    if (await act(slot, `learn=${encodeURIComponent(name)}`, () => learn(slot))) {
      setNames(n => n.map((v, i) => (i === slot ? '' : v)))
    }
  }

  if (missing) return null
  return (
    <Card
      title="Learned sounds"
      caption="Teach the device up to three sounds, such as a doorbell, a kettle or a phone. When it hears one, the screens say so. These take effect at once."
    >
      {!state ? (
        <p className="text-[13px] text-ink-2">Loading…</p>
      ) : (
        <ul className="divide-y divide-line rounded-lg border border-line">
          {state.slots.map((s, i) => (
            <li key={i} className="space-y-3 p-3">
              {learning === i ? (
                <Progress
                  label="Listening… make the sound now"
                  figures={`${Math.ceil(state.left_ms / 1000)} s`}
                  value={((LEARN_MS - state.left_ms) / LEARN_MS) * 100}
                />
              ) : s.used ? (
                <div className="flex items-center justify-between gap-3">
                  <div className="min-w-0">
                    <p className="truncate font-display text-[15px] font-bold tracking-wider text-ink">{s.name}</p>
                    <p className="text-xs text-ink-2">{s.on ? 'Listening for it' : 'Not listening'}</p>
                  </div>
                  <div className="flex shrink-0 items-center gap-2">
                    <Button variant="ghost" onClick={() => act(i, 'forget=1', () => act(i, 'forget=1', () => {}))} disabled={busy !== null || learning >= 0}>
                      Forget
                    </Button>
                    <Switch
                      checked={s.on}
                      ariaLabel={`Listen for ${s.name}`}
                      disabled={busy !== null}
                      onChange={v => act(i, `on=${v ? 1 : 0}`, () => act(i, `on=${v ? 1 : 0}`, () => {}))}
                    />
                  </div>
                </div>
              ) : (
                <div className="flex items-end gap-2">
                  <div className="min-w-0 flex-1">
                    <Input
                      label={`Sound ${i + 1}`}
                      placeholder="DOORBELL"
                      maxLength={NAME_MAX}
                      value={names[i]}
                      onChange={e => { const v = e.target.value; setNames(n => n.map((x, k) => (k === i ? v : x))) }}
                      onKeyDown={e => { if (e.key === 'Enter') learn(i) }}
                      className="uppercase"
                    />
                  </div>
                  <Button variant="secondary" onClick={() => learn(i)} loading={busy === i} disabled={learning >= 0}>Learn</Button>
                </div>
              )}
            </li>
          ))}
        </ul>
      )}
      <p className="text-[13px] leading-snug text-ink-2">
        Learning listens for 4 seconds: press Learn, then make the sound near the device. Steady sounds work best.
      </p>
      <NoticeLine notice={notice} />
    </Card>
  )
}

// -- The clock image --------------------------------------------------------------------------------

/** A picture cropped to the middle square, at the device's size, over black (the screen behind). */
function toEmblem(img: CanvasImageSource, w: number, h: number): ImageData {
  const c = document.createElement('canvas')
  c.width = c.height = EMBLEM_PX
  const ctx = c.getContext('2d')!
  ctx.fillStyle = '#000'
  ctx.fillRect(0, 0, EMBLEM_PX, EMBLEM_PX)
  const side = Math.min(w, h)
  ctx.imageSmoothingQuality = 'high'
  ctx.drawImage(img, (w - side) / 2, (h - side) / 2, side, side, 0, 0, EMBLEM_PX, EMBLEM_PX)
  return ctx.getImageData(0, 0, EMBLEM_PX, EMBLEM_PX)
}

/** RGBA to big-endian RGB565, as the screens take it. */
function toRgb565(d: ImageData): Uint8Array<ArrayBuffer> {
  const out = new Uint8Array(EMBLEM_PX * EMBLEM_PX * 2)
  for (let i = 0, o = 0; i < d.data.length; i += 4, o += 2) {
    const v = ((d.data[i] >> 3) << 11) | ((d.data[i + 1] >> 2) << 5) | (d.data[i + 2] >> 3)
    out[o] = v >> 8
    out[o + 1] = v & 0xff
  }
  return out
}

function fromRgb565(px: Uint8Array): ImageData {
  const d = new ImageData(EMBLEM_PX, EMBLEM_PX)
  for (let i = 0, o = 0; i < px.length; i += 2, o += 4) {
    const v = (px[i] << 8) | px[i + 1]
    const r = v >> 11, g = (v >> 5) & 63, b = v & 31
    d.data[o] = (r << 3) | (r >> 2)
    d.data[o + 1] = (g << 2) | (g >> 4)
    d.data[o + 2] = (b << 3) | (b >> 2)
    d.data[o + 3] = 255
  }
  return d
}

export function ClockImageCard({ send }: { send: Send }) {
  const preview = useRef<HTMLCanvasElement>(null)
  const [missing, setMissing] = useState(false)
  const [current, setCurrent] = useState<'logo' | 'image' | null>(null)
  const [picked, setPicked] = useState<{ name: string; data: ImageData } | null>(null)
  const [busy, setBusy] = useState<'put' | 'clear' | null>(null)
  const [notice, setNotice] = useState<Note | null>(null)
  const [shown, setShown] = useState<ImageData | null>(null)

  useEffect(() => {
    let alive = true
    fetch('/api/emblem', { cache: 'no-store' }).then(async r => {
      if (!alive) return
      if (r.status === 404) {
        // The firmware's "the logo shows" - or firmware without the endpoint at all.
        const text = await r.text()
        if (/logo/.test(text)) setCurrent('logo'); else setMissing(true)
        return
      }
      if (!r.ok) { setMissing(true); return }
      const px = new Uint8Array(await r.arrayBuffer())
      if (px.length === EMBLEM_PX * EMBLEM_PX * 2) { setShown(fromRgb565(px)); setCurrent('image') }
    }, () => { if (alive) setMissing(true) })
    return () => { alive = false }
  }, [])

  const data = picked?.data ?? shown
  useEffect(() => {
    const ctx = preview.current?.getContext('2d')
    if (!ctx) return
    ctx.fillStyle = '#000'
    ctx.fillRect(0, 0, EMBLEM_PX, EMBLEM_PX)
    if (data) ctx.putImageData(data, 0, 0)
  }, [data])

  const pick = async (file: File) => {
    setNotice(null)
    try {
      const bmp = await createImageBitmap(file)
      setPicked({ name: file.name, data: toEmblem(bmp, bmp.width, bmp.height) })
      bmp.close()
    } catch {
      setNotice({ tone: 'danger', text: 'That file is not a picture this browser can open.' })
    }
  }
  const put = async () => {
    if (!picked) return
    setBusy('put')
    setNotice(null)
    const r = await send(t => postBinary('/api/emblem', t, toRgb565(picked.data)), { label: 'Put it on the clock', run: put })
    setBusy(null)
    if (r.ok) {
      setShown(picked.data)
      setPicked(null)
      setCurrent('image')
      setNotice({ tone: 'success', text: 'On the clock screen now.' })
    } else if (r.text || r.status) setNotice({ tone: 'danger', text: refusal(r) })
  }
  const clear = async () => {
    setBusy('clear')
    setNotice(null)
    const r = await send(t => post('/api/emblem?clear=1', t), { label: 'Bring back the logo', run: clear })
    setBusy(null)
    if (r.ok) {
      setShown(null)
      setPicked(null)
      setCurrent('logo')
      setNotice({ tone: 'success', text: 'The logo is back.' })
    } else if (r.text || r.status) setNotice({ tone: 'danger', text: refusal(r) })
  }

  if (missing) return null
  return (
    <Card title="Clock image" caption="Your own picture on the clock screen, in place of the spinning logo. It is cropped to a square, 36 by 36 pixels. This takes effect at once.">
      <div className="flex items-center gap-4">
        <div className="shrink-0 rounded-lg border border-line bg-black p-2">
          <canvas
            ref={preview}
            width={EMBLEM_PX}
            height={EMBLEM_PX}
            className="block size-[108px] [image-rendering:pixelated]"
            aria-label={picked ? 'The picture as the screen will show it' : current === 'image' ? 'The picture on the clock now' : 'The logo shows now'}
            role="img"
          />
        </div>
        <div className="min-w-0 flex-1">
          <FileDrop
            accept="image/*"
            onFile={pick}
            busy={busy !== null}
            icon={<ImageIcon size={20} />}
            label={picked ? picked.name : 'Choose a picture'}
            hint={picked ? 'Shown as it will look. Tap to choose another.' : current === 'image' ? 'Your picture shows now' : 'The logo shows now'}
          />
        </div>
      </div>
      <div className="space-y-3">
        <ButtonRow>
          <Button onClick={put} disabled={!picked || busy !== null} loading={busy === 'put'}>Put it on the clock</Button>
          {current === 'image' && (
            <Button variant="secondary" onClick={clear} disabled={busy !== null} loading={busy === 'clear'}>Bring back the logo</Button>
          )}
        </ButtonRow>
        <NoticeLine notice={notice} />
      </div>
    </Card>
  )
}
