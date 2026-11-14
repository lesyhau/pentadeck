import { useId, useState } from 'react'
import { BellIcon, Button, ButtonRow, Card, Field, Input, NoticeLine, Select, type Note } from '../ui'
import { NOTIFY_SOUNDS, post, refusal, type Notification } from '../lib/api'
import type { Send } from './Pairing'

/**
 * A MESSAGE ON THE SCREENS: a card over the middle screen, the LEDs pulsing, until it times out or a
 * button is pressed. Scripts send the same with POST /api/notify (docs/API.md).
 */

/** The device's limits (modes.c): longer is cut there. */
const TITLE_MAX = 23
const TEXT_MAX = 95

const DURATIONS = [
  { value: '10', label: '10 seconds' },
  { value: '30', label: '30 seconds' },
  { value: '60', label: '1 minute' },
  { value: '0', label: 'Until a button is pressed' },
]
const SOUND_NAMES: Record<string, string> = {
  chime: 'Chime', none: 'No sound', up: 'Rising', down: 'Falling', warn: 'Klaxon', test: 'Test', alarm: 'Alarm',
}

export function NotifyCard({ send }: { send: Send }) {
  const [msg, setMsg] = useState<Notification>({ title: '', text: '', seconds: 10, sound: 'chime' })
  const [busy, setBusy] = useState(false)
  const [notice, setNotice] = useState<Note | null>(null)
  const timeId = useId()
  const soundId = useId()
  const empty = !msg.title.trim() && !msg.text.trim()

  const go = async () => {
    if (empty) return
    setBusy(true)
    setNotice(null)
    const body = { ...msg, title: msg.title.trim().toUpperCase(), text: msg.text.trim() }
    const r = await send(t => post('/api/notify', t, body), { label: 'Send now', run: go })
    setBusy(false)
    if (r.ok) {
      setNotice({ tone: 'success', text: 'On the screens now.' })
      setTimeout(() => setNotice(n => (n?.tone === 'success' ? null : n)), 4000)
    } else if (r.text || r.status) {
      setNotice({ tone: 'danger', text: refusal(r) })
    }
  }

  return (
    <Card title="Message" caption="A card on the middle screen, the LEDs pulsing. Any button on the device puts it away.">
      <div className="grid gap-4 sm:grid-cols-[minmax(0,1fr)_minmax(0,2fr)]">
        <Input
          label="Title"
          placeholder="DINNER"
          maxLength={TITLE_MAX}
          value={msg.title}
          onChange={e => setMsg(m => ({ ...m, title: e.target.value }))}
          className="uppercase"
        />
        <Input
          label="Text"
          placeholder="Ready in five minutes"
          maxLength={TEXT_MAX}
          value={msg.text}
          onChange={e => setMsg(m => ({ ...m, text: e.target.value }))}
          onKeyDown={e => { if (e.key === 'Enter') go() }}
        />
      </div>
      <div className="grid gap-4 sm:grid-cols-2">
        <Field id={timeId} label="Shown for">
          <Select id={timeId} value={String(msg.seconds)} options={DURATIONS} onChange={v => setMsg(m => ({ ...m, seconds: Number(v) }))} />
        </Field>
        <Field id={soundId} label="Sound">
          <Select
            id={soundId}
            value={msg.sound}
            options={NOTIFY_SOUNDS.map(s => ({ value: s, label: SOUND_NAMES[s] ?? s }))}
            onChange={v => setMsg(m => ({ ...m, sound: v }))}
          />
        </Field>
      </div>
      <div className="space-y-3">
        <ButtonRow>
          <Button onClick={go} disabled={empty} loading={busy} icon={<BellIcon size={16} />}>Send to the screens</Button>
        </ButtonRow>
        <NoticeLine notice={notice} />
      </div>
    </Card>
  )
}
