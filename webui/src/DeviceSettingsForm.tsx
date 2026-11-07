import { useId, useState } from 'react'
import {
  Button, ButtonRow, Card, Field, Input, NoticeLine, Reveal, Segmented, Select, Slider, SwitchRow, cn, type Note,
} from './ui'
import { LED_HINTS, LED_NAMES, PANEL_NAMES, SLOT_NAMES } from './api'
import type { Draft, DraftErrors } from './form'

/**
 * THE SETTINGS CARDS: Display, LEDs, Sounds & alarm, World clock, Data & location. Each edits the
 * draft; nothing reaches the device until Save.
 */

const pct = (v: number) => `${v}%`

export interface FormProps {
  draft: Draft
  errors: DraftErrors
  set: <K extends keyof Draft>(key: K, value: Draft[K]) => void
  panels: string[]
  ledModes: string[]
}

// -- Display --------------------------------------------------------------------------------------

/** What each panel is called on the little picture of the five screens. */
const PANEL_SHORT: Record<string, string> = { lab: 'Lab', cpu: 'CPU', nav: 'Clock', gpu: 'GPU', audio: 'Audio', off: 'Off' }

/** The five screens as they stand on the desk, each with what it shows. Always black, like the LCDs. */
function DeckPreview({ layout }: { layout: string[] }) {
  return (
    <div aria-hidden="true" className="mb-3 grid grid-cols-5 gap-1.5 sm:gap-2">
      {layout.map((panel, i) => {
        const off = panel === 'off'
        return (
          <div
            key={i}
            className={cn(
              'grid aspect-square place-items-center rounded-[3px] border bg-black text-[10px] font-medium uppercase tracking-[0.1em] sm:text-[11px]',
              off ? 'border-[#222426] text-[#5A5D62]' : 'border-[#1B5E78] text-[#78BED9]',
            )}
          >
            {PANEL_SHORT[panel] ?? panel}
          </div>
        )
      })}
    </div>
  )
}

export function DisplaySection({ draft, set, errors, panels }: FormProps) {
  const options = panels.map(p => ({ value: p, label: PANEL_NAMES[p] ?? p }))
  const screensId = useId()
  const idleId = useId()
  const setNight = (on: boolean) => {
    // Turned on with no window yet: the usual night, ready to adjust.
    if (on && !draft.night_start && !draft.night_end) {
      set('night_start', '23:00')
      set('night_end', '07:00')
    }
    set('night', on)
  }
  return (
    <Card title="Display" caption="How bright the five screens are, when they dim, and what each one shows.">
      <Slider label="Brightness" value={draft.brightness} onChange={v => set('brightness', v)} format={pct} />

      <div>
        <SwitchRow title="Dim at night" caption="Screens and LEDs dim; alerts can go quiet." checked={draft.night} onChange={setNight} />
        <Reveal show={draft.night}>
          <div className="space-y-6 pt-5">
            <div className="grid grid-cols-2 gap-3 sm:gap-4">
              <Input label="From" type="time" value={draft.night_start} onChange={e => set('night_start', e.target.value)} error={errors.night_start} />
              <Input label="Until" type="time" value={draft.night_end} onChange={e => set('night_end', e.target.value)} error={errors.night_end} />
            </div>
            <Slider label="Night brightness" value={draft.night_brightness} onChange={v => set('night_brightness', v)} format={pct} />
          </div>
        </Reveal>
      </div>

      <div>
        <SwitchRow
          title="Low power when nobody's at the desk"
          caption="After a while with no sound, no buttons and no keyboard or mouse on the PC, the screens dim, the LEDs go off and the device slows down. Any sound wakes it."
          checked={draft.idle}
          onChange={v => set('idle', v)}
        />
        <Reveal show={draft.idle}>
          <div className="space-y-6 pt-5">
            <Field id={idleId} label="After">
              <Select
                id={idleId}
                value={String(draft.idle_min)}
                options={IDLE_AFTER.some(o => o.value === String(draft.idle_min)) ? IDLE_AFTER : [...IDLE_AFTER, { value: String(draft.idle_min), label: `${draft.idle_min} minutes` }]}
                onChange={v => set('idle_min', Number(v))}
                ariaLabel="Low power after"
              />
            </Field>
            <Slider label="Low-power brightness" value={draft.idle_brightness} onChange={v => set('idle_brightness', v)} format={pct} />
          </div>
        </Reveal>
      </div>

      <SwitchRow title="24-hour clock" checked={draft.hour24} onChange={v => set('hour24', v)} />

      <Field id={screensId} group label="Screens, left to right" hint="What each screen shows, in the order they stand on the desk.">
        <DeckPreview layout={draft.layout} />
        <ul className="divide-y divide-line rounded-lg border border-line">
          {draft.layout.map((panel, i) => (
            <li key={i} className="flex items-center justify-between gap-3 py-2 pl-3 pr-2">
              <div className="min-w-0">
                <div className="truncate text-[15px] font-medium text-ink">{SLOT_NAMES[i] ?? `Screen ${i + 1}`}</div>
                <div className="text-xs text-ink-2">Screen {i + 1}</div>
              </div>
              <Select
                className="w-40 shrink-0"
                ariaLabel={`Screen ${i + 1} (${(SLOT_NAMES[i] ?? '').toLowerCase()})`}
                value={panel}
                options={options}
                onChange={v => set('layout', draft.layout.map((p, j) => (j === i ? v : p)))}
              />
            </li>
          ))}
        </ul>
      </Field>
    </Card>
  )
}

// -- LEDs -----------------------------------------------------------------------------------------

export function LedSection({ draft, set, ledModes, onTest, onResume, busy, notice }: FormProps & {
  onTest: () => void
  onResume: () => void
  busy: 'test' | 'resume' | null
  notice: Note | null
}) {
  const modeId = useId()
  return (
    <Card title="LEDs" caption="The side strips, the back and the buttons board.">
      <Field id={modeId} group label="Mode">
        <Segmented
          labelledBy={`${modeId}-label`}
          value={draft.led_mode}
          onChange={v => set('led_mode', v)}
          options={ledModes.map(m => ({ value: m, label: LED_NAMES[m] ?? m }))}
        />
        <p key={draft.led_mode} className="pd-fade-in mt-2 min-h-[2lh] max-w-[60ch] text-[13px] leading-snug text-ink-2">
          {LED_HINTS[draft.led_mode] ?? ''}
        </p>
      </Field>

      <Slider label="LED brightness" value={draft.led_brightness} onChange={v => set('led_brightness', v)} format={pct} />

      <div className="space-y-3">
        <ButtonRow>
          <Button variant="secondary" onClick={onTest} loading={busy === 'test'} disabled={!!busy}>Test pattern</Button>
          <Button variant="secondary" onClick={onResume} loading={busy === 'resume'} disabled={!!busy}>Resume effects</Button>
        </ButtonRow>
        <NoticeLine notice={notice} />
      </div>
    </Card>
  )
}

// -- Alerts ---------------------------------------------------------------------------------------

/** The alarm's day sets: bit 0 = Monday .. bit 6 = Sunday (the device cycles through the same three). */
const IDLE_AFTER = [2, 5, 10, 15, 20, 30, 45, 60, 90, 120].map(m => ({ value: String(m), label: `${m} minutes` }))

const DAY_SETS = [
  { value: String(0x1f), label: 'Monday to Friday' },
  { value: String(0x60), label: 'Weekends' },
  { value: String(0x7f), label: 'Every day' },
]

/** The device's sounds (buzzer.h patterns), to hear before relying on them. */
const SOUNDS = [
  { value: '0', label: 'Test chime' },
  { value: '1', label: 'Something is hot (klaxon)' },
  { value: '2', label: 'Link lost (power-down)' },
  { value: '3', label: 'Link restored (power-up)' },
  { value: '4', label: 'Mains lost (red alert)' },
  { value: '10', label: 'Battery low' },
  { value: '8', label: 'Timer done' },
  { value: '11', label: 'Start-up' },
  { value: '12', label: 'Hourly chime' },
]

export function SoundsSection({ draft, set, errors, onBeep, busy, notice }: FormProps & {
  onBeep: (pattern: number) => void
  busy: boolean
  notice: Note | null
}) {
  const temps = [['alert_cpu_temp', 'CPU'], ['alert_gpu_temp', 'GPU'], ['alert_vram_temp', 'VRAM']] as const
  const [sound, setSound] = useState('0')
  const daysId = useId()
  return (
    <Card title="Sounds & alarm" caption="The buzzer: alerts, key clicks, the hourly chime and a daily alarm.">
      <Slider label="Volume" value={draft.buzzer_volume} min={1} max={100} onChange={v => set('buzzer_volume', v)} format={pct} />

      <div>
        <SwitchRow
          title="Alerts"
          caption="Heat, the home lab going down or coming back, services, mains power."
          checked={draft.buzzer}
          onChange={v => set('buzzer', v)}
        />
        {/* Folded away with the alerts off, unless a temperature needs fixing before a save. */}
        <Reveal show={draft.buzzer || temps.some(([k]) => !!errors[k])}>
          <div className="pt-5">
            <div>
              <p className="text-sm font-medium text-ink">Alert temperatures</p>
              <p className="mb-3 mt-0.5 text-[13px] leading-snug text-ink-2">Beep when a part reaches this, from 50 to 120 °C.</p>
              <div className="grid grid-cols-3 gap-3 sm:gap-4">
                {temps.map(([key, label]) => (
                  <Input
                    key={key}
                    label={label}
                    type="number"
                    inputMode="numeric"
                    min={50}
                    max={120}
                    suffix="°C"
                    value={draft[key]}
                    onChange={e => set(key, e.target.value)}
                    error={errors[key]}
                  />
                ))}
              </div>
            </div>
          </div>
        </Reveal>
      </div>

      <SwitchRow
        title="Quiet at night"
        caption={draft.night ? 'No alerts inside the night window.' : 'Turn on “Dim at night” to set the window.'}
        checked={draft.quiet_night}
        onChange={v => set('quiet_night', v)}
        disabled={!draft.night}
      />

      <SwitchRow
        title="Key beep"
        caption="A short click when you press a button on the device."
        checked={draft.key_beep}
        onChange={v => set('key_beep', v)}
      />

      <SwitchRow
        title="Hourly chime"
        caption="Two soft notes on the hour, like a watch. Silent in a quiet night."
        checked={draft.chime}
        onChange={v => set('chime', v)}
      />

      <div>
        <SwitchRow
          title="Daily alarm"
          caption="Plays a melody for a minute. On the device: LIGHT snoozes 5 minutes, any other button stops it."
          checked={draft.alarm_on}
          onChange={v => set('alarm_on', v)}
        />
        <Reveal show={draft.alarm_on}>
          <div className="grid grid-cols-2 gap-3 pt-5 sm:gap-4">
            <Input label="Time" type="time" value={draft.alarm_time} onChange={e => set('alarm_time', e.target.value)} error={errors.alarm_time} />
            <Field id={daysId} label="Days">
              <Select
                id={daysId}
                value={String(draft.alarm_days)}
                options={DAY_SETS.some(o => o.value === String(draft.alarm_days)) ? DAY_SETS : [...DAY_SETS, { value: String(draft.alarm_days), label: 'Custom' }]}
                onChange={v => set('alarm_days', Number(v))}
                ariaLabel="Alarm days"
              />
            </Field>
          </div>
        </Reveal>
      </div>

      <div className="space-y-3">
        <Field id={`${daysId}-try`} group label="Try a sound">
          <div className="flex gap-3">
            <Select value={sound} options={SOUNDS} onChange={setSound} ariaLabel="Sound to play" className="min-w-0 flex-1" />
            <Button variant="secondary" onClick={() => onBeep(Number(sound))} loading={busy} disabled={busy}>Play</Button>
          </div>
        </Field>
        <NoticeLine notice={notice} />
      </div>
    </Card>
  )
}

// -- Data & location ------------------------------------------------------------------------------

const URL_INPUT = { type: 'url', inputMode: 'url', autoCapitalize: 'off', autoCorrect: 'off', spellCheck: false } as const

export function DataSection({ draft, set, errors }: FormProps) {
  return (
    <Card title="Data & location" caption="Where the stats come from, and where the weather on the clock screen is for.">
      <Input label="PC stats agent" {...URL_INPUT} placeholder="http://pc.local:8765/stats" value={draft.src0} onChange={e => set('src0', e.target.value)} error={errors.src0} />
      <Input label="Home lab stats agent" {...URL_INPUT} placeholder="http://homelab.local:8765/stats" value={draft.src1} onChange={e => set('src1', e.target.value)} error={errors.src1} />
      <div className="grid grid-cols-2 gap-3 sm:gap-4">
        <Input label="Poll every" type="number" inputMode="numeric" min={500} max={10000} step={100} suffix="ms" value={draft.poll_ms} onChange={e => set('poll_ms', e.target.value)} error={errors.poll_ms} />
        <Input label="Time zone (POSIX)" autoCapitalize="characters" autoCorrect="off" spellCheck={false} placeholder="UTC0" value={draft.tz} onChange={e => set('tz', e.target.value)} error={errors.tz} />
      </div>
      {/* No inputMode: the iPhone's decimal keypad has no minus sign, and half the world is negative. */}
      <div className="grid grid-cols-2 gap-3 sm:gap-4">
        <Input label="Latitude" type="number" step="any" min={-90} max={90} suffix="°" value={draft.lat} onChange={e => set('lat', e.target.value)} error={errors.lat} />
        <Input label="Longitude" type="number" step="any" min={-180} max={180} suffix="°" value={draft.lon} onChange={e => set('lon', e.target.value)} error={errors.lon} />
      </div>
    </Card>
  )
}

// -- World clock -----------------------------------------------------------------------------------

/** Common places and their POSIX time zones (daylight saving rules included), for the World mode. */
const PLACES: { name: string; tz: string }[] = [
  { name: 'STUTTGART', tz: 'CET-1CEST,M3.5.0,M10.5.0/3' },
  { name: 'BERLIN', tz: 'CET-1CEST,M3.5.0,M10.5.0/3' },
  { name: 'PARIS', tz: 'CET-1CEST,M3.5.0,M10.5.0/3' },
  { name: 'LONDON', tz: 'GMT0BST,M3.5.0/1,M10.5.0' },
  { name: 'NEW YORK', tz: 'EST5EDT,M3.2.0,M11.1.0' },
  { name: 'CHICAGO', tz: 'CST6CDT,M3.2.0,M11.1.0' },
  { name: 'LOS ANGELES', tz: 'PST8PDT,M3.2.0,M11.1.0' },
  { name: 'SAO PAULO', tz: '<-03>3' },
  { name: 'DUBAI', tz: '<+04>-4' },
  { name: 'NEW DELHI', tz: 'IST-5:30' },
  { name: 'HO CHI MINH', tz: '<+07>-7' },
  { name: 'SINGAPORE', tz: '<+08>-8' },
  { name: 'TOKYO', tz: 'JST-9' },
  { name: 'SYDNEY', tz: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
]

function PlaceFields({ which, draft, set, errors }: FormProps & { which: 0 | 1 }) {
  const nameKey = which ? 'w1name' : 'w0name', tzKey = which ? 'w1tz' : 'w0tz'
  const known = PLACES.find(p => p.name === draft[nameKey].trim().toUpperCase() && p.tz === draft[tzKey].trim())
  const id = useId()
  return (
    <div className="space-y-3">
      <Field id={id} label={which ? 'Right of home' : 'Left of home'}>
        <Select
          id={id}
          value={known ? known.name : 'custom'}
          options={[...PLACES.map(p => ({ value: p.name, label: p.name.charAt(0) + p.name.slice(1).toLowerCase().replace(/ (\w)/g, (_, c: string) => ' ' + c.toUpperCase()) })), { value: 'custom', label: 'Somewhere else…' }]}
          onChange={v => {
            const p = PLACES.find(q => q.name === v)
            if (p) { set(nameKey, p.name); set(tzKey, p.tz) }
          }}
        />
      </Field>
      <Reveal show={!known}>
        <div className="grid grid-cols-2 gap-3 sm:gap-4">
          <Input label="Name" autoCapitalize="characters" maxLength={15} value={draft[nameKey]} onChange={e => set(nameKey, e.target.value)} error={errors[nameKey]} />
          <Input label="Time zone (POSIX)" autoCapitalize="characters" autoCorrect="off" spellCheck={false} value={draft[tzKey]} onChange={e => set(tzKey, e.target.value)} error={errors[tzKey]} />
        </div>
      </Reveal>
    </div>
  )
}

export function WorldSection(props: FormProps) {
  return (
    <Card title="World clock" caption="The two places shown either side of home in the World mode.">
      <PlaceFields which={0} {...props} />
      <PlaceFields which={1} {...props} />
    </Card>
  )
}
