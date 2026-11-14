import { useId, useState } from 'react'
import {
  Button, ButtonRow, Card, Field, Input, NoticeLine, Reveal, Segmented, Select, Slider, SwitchRow, cn, type Note,
} from '../ui'
import { AUDIO_VIEW_HINTS, AUDIO_VIEW_NAMES, CLAP_NAMES, LED_EFFECT_HINTS, LED_EFFECT_NAMES, LED_OWN_COLOURS, PANEL_NAMES, SLOT_NAMES } from '../lib/api'
import { ACCENTS } from '../lib/accent'
import type { Draft, DraftErrors } from '../lib/form'

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
  ledEffects: string[]
  /** What the AUDIO screen can show (the firmware's list). */
  audioViews: string[]
  /** The visual effects' colour sets; empty with firmware before the accent and the effects. */
  vizPalettes: string[]
  /** What a double clap can do; empty with firmware before claps. */
  clapActions: string[]
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
              off ? 'border-[#222426] text-[#5A5D62]' : 'border-[rgb(var(--pd-a800))] text-[rgb(var(--pd-a400))]',
            )}
          >
            {PANEL_SHORT[panel] ?? panel}
          </div>
        )
      })}
    </div>
  )
}

export function DisplaySection({ draft, set, errors, panels, audioViews, vizPalettes }: FormProps) {
  const options = panels.map(p => ({ value: p, label: PANEL_NAMES[p] ?? p }))
  const screensId = useId()
  const audioId = useId()
  const accentId = useId()
  const paletteId = useId()
  const idleId = useId()
  const effects = vizPalettes.length > 0
  const setNight = (on: boolean) => {
    // Turned on with no window yet: the usual night, ready to adjust.
    if (on && !draft.night_start && !draft.night_end) {
      set('night_start', '23:00')
      set('night_end', '07:00')
    }
    set('night', on)
  }
  return (
    <Card title="Display" caption="Brightness, dimming and what each screen shows.">
      <Slider label="Brightness" value={draft.brightness} onChange={v => set('brightness', v)} format={pct} />

      <div>
        <SwitchRow title="Dim at night" caption="Screens and LEDs dim." checked={draft.night} onChange={setNight} />
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
          title="Low power when you're away"
          caption="Dims the screens and turns the LEDs off when nothing happens for a while. Any sound wakes it."
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

      {effects && (
        <Field id={accentId} group label="Accent colour" hint="The screens' frames, headings and bars, the logo, and this page." error={errors.accent}>
          <ColourPicker label="Accent colour" value={draft.accent} onChange={v => set('accent', v)} colours={ACCENTS} />
        </Field>
      )}

      <Field id={screensId} group label="Screens, left to right">
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

      <Field id={audioId} label="Audio screen">
        <Select
          id={audioId}
          value={draft.audio_view}
          options={audioViews.map(v => ({ value: v, label: AUDIO_VIEW_NAMES[v] ?? v }))}
          onChange={v => set('audio_view', v)}
          ariaLabel="Audio screen"
        />
        <p key={draft.audio_view} className="pd-fade-in mt-2 max-w-[60ch] text-[13px] leading-snug text-ink-2">
          {AUDIO_VIEW_HINTS[draft.audio_view] ?? ''}
        </p>
      </Field>

      {effects && (
        <Field id={paletteId} group label="Effect colours" hint="For the effects on the audio screen and in the VISUAL mode (where power changes them too).">
          <Segmented
            labelledBy={`${paletteId}-label`}
            value={draft.viz_palette}
            onChange={v => set('viz_palette', v)}
            options={vizPalettes.map(p => ({ value: p, label: PALETTE_NAMES[p] ?? p }))}
          />
        </Field>
      )}
    </Card>
  )
}

const PALETTE_NAMES: Record<string, string> = { accent: 'Accent', rainbow: 'Rainbow', fire: 'Fire' }

// -- LEDs -----------------------------------------------------------------------------------------

const LED_ZONES = [
  { key: 'led_left', color: 'led_left_color', label: 'Left strip' },
  { key: 'led_back', color: 'led_back_color', label: 'Back light' },
  { key: 'led_right', color: 'led_right_color', label: 'Right strip' },
] as const

/** Colours that look right on the LEDs (fully saturated; teal is the screens' own). */
const LED_COLOURS = [
  { value: '#2a8cb2', label: 'Teal' },
  { value: '#00c0ff', label: 'Cyan' },
  { value: '#0040ff', label: 'Blue' },
  { value: '#8000ff', label: 'Purple' },
  { value: '#ff0080', label: 'Pink' },
  { value: '#ff0000', label: 'Red' },
  { value: '#ff4000', label: 'Orange' },
  { value: '#ffa000', label: 'Amber' },
  { value: '#00ff40', label: 'Green' },
  { value: '#ffffff', label: 'White' },
]

const SWATCH = 'relative h-9 w-9 shrink-0 rounded-full border border-line-strong transition-shadow duration-150 focus-visible:outline-none focus-visible:shadow-[0_0_0_3px_rgb(var(--pd-accent)/0.35)]'
const PICKED = 'shadow-[0_0_0_2px_rgb(var(--pd-surface)),0_0_0_4px_rgb(var(--pd-accent))]'

/** The presets, and "Custom" for any other colour (the system's colour picker). */
function ColourPicker({ value, onChange, label, colours = LED_COLOURS }: {
  value: string
  onChange: (v: string) => void
  label: string
  colours?: { value: string; label: string }[]
}) {
  const custom = !colours.some(c => c.value === value.toLowerCase())
  return (
    <div role="radiogroup" aria-label={label} className="flex flex-wrap gap-2.5">
      {colours.map(c => (
        <button
          key={c.value}
          type="button"
          role="radio"
          aria-checked={c.value === value.toLowerCase()}
          title={c.label}
          aria-label={c.label}
          className={cn(SWATCH, c.value === value.toLowerCase() && PICKED)}
          style={{ background: c.value }}
          onClick={() => onChange(c.value)}
        />
      ))}
      <label
        title="Custom colour"
        className={cn(SWATCH, 'cursor-pointer overflow-hidden focus-within:shadow-[0_0_0_3px_rgb(var(--pd-accent)/0.35)]', custom && PICKED)}
        style={{ background: custom ? value : 'conic-gradient(red, yellow, lime, cyan, blue, magenta, red)' }}
      >
        <input type="color" aria-label="Custom colour" value={value} onChange={e => onChange(e.target.value)} className="absolute inset-0 h-full w-full cursor-pointer opacity-0" />
      </label>
    </div>
  )
}

export function LedSection({ draft, set, ledEffects, onTest, onResume, busy, notice }: FormProps & {
  onTest: () => void
  onResume: () => void
  busy: 'test' | 'resume' | null
  notice: Note | null
}) {
  const zonesId = useId()
  const [picking, setPicking] = useState<string | null>(null)
  const options = ledEffects.map(e => ({ value: e, label: LED_EFFECT_NAMES[e] ?? e }))
  return (
    <Card title="LEDs" caption="An effect and a colour for each strip. The buttons stay lit unless all three are off.">
      <Field id={zonesId} group label="Effects">
        <ul className="divide-y divide-line rounded-lg border border-line">
          {LED_ZONES.map(({ key, color, label }) => {
            const ownColours = LED_OWN_COLOURS.includes(draft[key])
            const open = picking === key && !ownColours
            return (
              <li key={key} className="py-2 pl-3 pr-2">
                <div className="flex items-center justify-between gap-2 sm:gap-3">
                  <div className="min-w-0">
                    <div className="truncate text-[15px] font-medium text-ink">{label}</div>
                    <div key={draft[key]} className="pd-fade-in text-xs leading-snug text-ink-2">{LED_EFFECT_HINTS[draft[key]] ?? ''}</div>
                  </div>
                  <div className="flex shrink-0 items-center gap-2">
                    <Select className="w-32 sm:w-36" ariaLabel={`${label} effect`} value={draft[key]} options={options} onChange={v => set(key, v)} />
                    <button
                      type="button"
                      aria-label={`${label} colour`}
                      aria-expanded={open}
                      disabled={ownColours}
                      title={ownColours ? 'This effect has its own colours' : 'Colour'}
                      onClick={() => setPicking(open ? null : key)}
                      className={cn(
                        'h-11 w-11 shrink-0 rounded-lg border border-line-strong transition-shadow duration-150',
                        'focus-visible:outline-none focus-visible:shadow-[0_0_0_3px_rgb(var(--pd-accent)/0.35)]',
                        open && PICKED,
                        ownColours && 'cursor-not-allowed opacity-30',
                      )}
                      style={{ background: draft[color] }}
                    />
                  </div>
                </div>
                <Reveal show={open}>
                  <div className="pb-1 pt-3">
                    <ColourPicker label={`${label} colour`} value={draft[color]} onChange={v => set(color, v)} />
                  </div>
                </Reveal>
              </li>
            )
          })}
        </ul>
      </Field>

      <Slider label="Brightness" value={draft.led_brightness} onChange={v => set('led_brightness', v)} format={pct} />

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

export function MicSection({ draft, set, clapActions }: FormProps) {
  const clapId = useId()
  return (
    <Card title="Microphone" caption="Claps, your hearing, and the mic on your PC.">
      {clapActions.length > 0 && (
        <Field id={clapId} label="Double clap" hint="Two sharp claps, a beat apart. The device's own beeps are ignored.">
          <Select
            id={clapId}
            value={draft.clap}
            onChange={v => set('clap', v)}
            options={clapActions.map(a => ({ value: a, label: CLAP_NAMES[a] ?? a }))}
          />
        </Field>
      )}
      {clapActions.length > 0 && (
        <SwitchRow
          title="Warn about loud days"
          caption="A card on the screens when the day's sound dose passes 100 %: 8 hours at 85 dBA, or the same in louder, shorter bursts."
          checked={draft.noise_alert}
          onChange={v => set('noise_alert', v)}
        />
      )}
      <SwitchRow
        title="Let PCs use the mic"
        caption="Also lets this page listen in. The AUDIO screen shows MIC LIVE while anyone listens. The device's own features use the mic either way."
        checked={draft.mic_stream}
        onChange={v => set('mic_stream', v)}
      />
    </Card>
  )
}

export function SoundsSection({ draft, set, errors, onBeep, busy, notice }: FormProps & {
  onBeep: (pattern: number) => void
  busy: boolean
  notice: Note | null
}) {
  const temps = [['alert_cpu_temp', 'CPU'], ['alert_gpu_temp', 'GPU'], ['alert_vram_temp', 'VRAM']] as const
  const [sound, setSound] = useState('0')
  const daysId = useId()
  return (
    <Card title="Sounds & alarm" caption="Alerts, key clicks, the chime and the alarm.">
      <Slider label="Volume" value={draft.buzzer_volume} min={1} max={100} onChange={v => set('buzzer_volume', v)} format={pct} />

      <div>
        <SwitchRow
          title="Alerts"
          caption="Heat, the home lab going down or back up, mains power."
          checked={draft.buzzer}
          onChange={v => set('buzzer', v)}
        />
        {/* Folded away with the alerts off, unless a temperature needs fixing before a save. */}
        <Reveal show={draft.buzzer || temps.some(([k]) => !!errors[k])}>
          <div className="pt-5">
            <div>
              <p className="text-sm font-medium text-ink">Alert temperatures</p>
              <p className="mb-3 mt-0.5 text-[13px] leading-snug text-ink-2">Beep at this temperature (50–120 °C).</p>
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
        caption={draft.night ? 'No alerts during the night hours.' : 'Needs “Dim at night”.'}
        checked={draft.quiet_night}
        onChange={v => set('quiet_night', v)}
        disabled={!draft.night}
      />

      <SwitchRow
        title="Key beep"
        caption="A click on every button press."
        checked={draft.key_beep}
        onChange={v => set('key_beep', v)}
      />

      <SwitchRow
        title="Hourly chime"
        caption="Two soft notes on the hour."
        checked={draft.chime}
        onChange={v => set('chime', v)}
      />

      <div>
        <SwitchRow
          title="Daily alarm"
          caption="LIGHT snoozes it for 5 minutes; any other button stops it."
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
    <Card title="Data & location" caption="Where the stats come from, and your location for the weather.">
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

const titleCase = (name: string) => name.toLowerCase().replace(/(^|\s)(\w)/g, (_, s: string, c: string) => s + c.toUpperCase())

function PlaceFields({ which, draft, set, errors }: FormProps & { which: 0 | 1 }) {
  const nameKey = which ? 'w1name' : 'w0name', tzKey = which ? 'w1tz' : 'w0tz'
  const known = PLACES.find(p => p.name === draft[nameKey].trim().toUpperCase() && p.tz === draft[tzKey].trim())
  const id = useId()
  return (
    <div className="space-y-3">
      <Field id={id} label={which ? 'Second place' : 'First place'}>
        <Select
          id={id}
          value={known ? known.name : 'custom'}
          options={[...PLACES.map(p => ({ value: p.name, label: titleCase(p.name) })), { value: 'custom', label: 'Somewhere else…' }]}
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

/** Screens 2, 3 and 4 of the World mode: which clock each shows. Picking one already shown swaps the two. */
function ClockOrder({ draft, set }: FormProps) {
  const id = useId()
  const names = ['Home', titleCase(draft.w0name.trim()) || 'First place', titleCase(draft.w1name.trim()) || 'Second place']
  const options = names.map((label, place) => ({ value: String(place), label }))
  const pick = (slot: number, place: number) => {
    const order = [...draft.world_order]
    const from = order.indexOf(place)
    order[from] = order[slot]
    order[slot] = place
    set('world_order', order)
  }
  return (
    <Field id={id} group label="Order" hint="The AWAKE screen lists them in the same order.">
      <ul className="divide-y divide-line rounded-lg border border-line">
        {draft.world_order.map((place, slot) => (
          <li key={slot} className="flex items-center justify-between gap-3 py-2 pl-3 pr-2">
            <div className="min-w-0">
              <div className="truncate text-[15px] font-medium text-ink">Screen {slot + 2}</div>
              {slot === 1 && <div className="text-xs text-ink-2">Middle</div>}
            </div>
            <Select className="w-40 shrink-0" ariaLabel={`Screen ${slot + 2}`} value={String(place)} options={options} onChange={v => pick(slot, Number(v))} />
          </li>
        ))}
      </ul>
    </Field>
  )
}

export function WorldSection(props: FormProps) {
  return (
    <Card title="World clock" caption="Two more places beside home, in the order you choose.">
      <PlaceFields which={0} {...props} />
      <PlaceFields which={1} {...props} />
      <ClockOrder {...props} />
    </Card>
  )
}
