import { LED_DEFAULT_COLOUR, LED_PRESETS, type DeviceSettings, type SETTINGS_LISTS } from './api'
import { DEFAULT_ACCENT, isHex } from './accent'

/**
 * THE FORM, as edited: the device's settings with the typed numbers kept as the text being typed
 * (so "1" on the way to "120" is not refused mid-word), and night dimming as a switch of its own -
 * the device has no such field, an empty window means "off".
 */
export interface Draft {
  src0: string
  src1: string
  poll_ms: string
  tz: string
  lat: string
  lon: string
  hour24: boolean
  brightness: number
  night: boolean
  night_start: string
  night_end: string
  night_brightness: number
  layout: string[]
  buzzer: boolean
  buzzer_volume: number
  quiet_night: boolean
  alert_cpu_temp: string
  alert_gpu_temp: string
  alert_vram_temp: string
  led_left: string
  led_back: string
  led_right: string
  led_left_color: string
  led_back_color: string
  led_right_color: string
  led_brightness: number
  key_beep: boolean
  chime: boolean
  alarm_on: boolean
  alarm_time: string
  alarm_days: number
  w0name: string
  w0tz: string
  w1name: string
  w1tz: string
  idle: boolean
  idle_min: number
  idle_brightness: number
  mic_stream: boolean
  audio_view: string
  /** '#rrggbb': the screens' and this page's accent. */
  accent: string
  /** The visual effects' colours: 'accent', 'rainbow' or 'fire'. */
  viz_palette: string
  /** The World mode's clocks on screens 2, 3, 4: 0 = home, 1 and 2 = the two places. */
  world_order: number[]
  /** What a double clap does. */
  clap: string
  noise_alert: boolean
}

export type DraftErrors = Partial<Record<keyof Draft, string>>

export function toDraft(s: DeviceSettings): Draft {
  // Firmware before the zones has one mode for all three.
  const [left, back, right] = LED_PRESETS[s.led_mode ?? ''] ?? LED_PRESETS.status
  return {
    src0: s.sources[0] ?? '',
    src1: s.sources[1] ?? '',
    poll_ms: String(s.poll_ms),
    tz: s.tz,
    lat: String(s.lat),
    lon: String(s.lon),
    hour24: s.hour24,
    brightness: s.brightness,
    night: !!(s.night_start && s.night_end),
    night_start: s.night_start,
    night_end: s.night_end,
    night_brightness: s.night_brightness,
    layout: [...s.layout],
    buzzer: s.buzzer,
    buzzer_volume: s.buzzer_volume,
    quiet_night: s.quiet_night,
    alert_cpu_temp: String(s.alert_cpu_temp),
    alert_gpu_temp: String(s.alert_gpu_temp),
    alert_vram_temp: String(s.alert_vram_temp),
    led_left: s.led_left ?? left,
    led_back: s.led_back ?? back,
    led_right: s.led_right ?? right,
    led_left_color: s.led_left_color ?? LED_DEFAULT_COLOUR,
    led_back_color: s.led_back_color ?? LED_DEFAULT_COLOUR,
    led_right_color: s.led_right_color ?? LED_DEFAULT_COLOUR,
    led_brightness: s.led_brightness,
    key_beep: s.key_beep ?? true,
    chime: s.chime ?? false,
    alarm_on: s.alarm_on ?? false,
    alarm_time: s.alarm_time ?? '07:00',
    alarm_days: s.alarm_days ?? 0x1f,
    w0name: s.world_names?.[0] ?? 'LONDON',
    w0tz: s.world_tz?.[0] ?? 'GMT0BST,M3.5.0/1,M10.5.0',
    w1name: s.world_names?.[1] ?? 'NEW YORK',
    w1tz: s.world_tz?.[1] ?? 'EST5EDT,M3.2.0,M11.1.0',
    idle: (s.idle_min ?? 10) > 0,
    idle_min: s.idle_min || 10,
    idle_brightness: s.idle_brightness ?? 10,
    mic_stream: s.mic_stream ?? true,
    audio_view: s.audio_view ?? 'eq',
    accent: s.accent ?? DEFAULT_ACCENT,
    viz_palette: s.viz_palette ?? 'accent',
    world_order: s.world_order?.length === 3 ? [...s.world_order] : [1, 0, 2],
    clap: s.clap ?? 'off',
    noise_alert: s.noise_alert ?? true,
  }
}

/** What POST /api/settings is sent. Night off sends an empty window, as the device expects. */
export function toPayload(d: Draft): Omit<DeviceSettings, (typeof SETTINGS_LISTS)[number]> {
  return {
    sources: [d.src0.trim(), d.src1.trim()],
    poll_ms: Number(d.poll_ms),
    tz: d.tz.trim(),
    lat: Number(d.lat),
    lon: Number(d.lon),
    hour24: d.hour24,
    brightness: d.brightness,
    night_start: d.night ? d.night_start : '',
    night_end: d.night ? d.night_end : '',
    night_brightness: d.night_brightness,
    layout: d.layout,
    buzzer: d.buzzer,
    buzzer_volume: d.buzzer_volume,
    quiet_night: d.quiet_night,
    alert_cpu_temp: Number(d.alert_cpu_temp),
    alert_gpu_temp: Number(d.alert_gpu_temp),
    alert_vram_temp: Number(d.alert_vram_temp),
    led_left: d.led_left,
    led_back: d.led_back,
    led_right: d.led_right,
    led_left_color: d.led_left_color,
    led_back_color: d.led_back_color,
    led_right_color: d.led_right_color,
    led_brightness: d.led_brightness,
    key_beep: d.key_beep,
    chime: d.chime,
    alarm_on: d.alarm_on,
    alarm_time: d.alarm_time,
    alarm_days: d.alarm_days,
    world_names: [d.w0name.trim().toUpperCase(), d.w1name.trim().toUpperCase()],
    world_tz: [d.w0tz.trim(), d.w1tz.trim()],
    idle_min: d.idle ? d.idle_min : 0,
    idle_brightness: d.idle_brightness,
    mic_stream: d.mic_stream,
    audio_view: d.audio_view,
    accent: d.accent.toLowerCase(),
    viz_palette: d.viz_palette,
    world_order: d.world_order,
    clap: d.clap,
    noise_alert: d.noise_alert,
  }
}

/**
 * Whether two drafts would save the same thing - so turning night dimming on and off again, which
 * leaves the pre-filled times behind, is not a change. Numbers compare as typed ("1000" and "1000.0"
 * differ, which is honest: the field was edited).
 */
export function sameDraft(a: Draft, b: Draft): boolean {
  const key = (d: Draft) =>
    JSON.stringify({ ...d, night_start: d.night ? d.night_start : '', night_end: d.night ? d.night_end : '', idle_min: d.idle ? d.idle_min : 0 })
  return key(a) === key(b)
}

const isNum = (s: string) => s.trim() !== '' && Number.isFinite(Number(s))
const inRange = (s: string, lo: number, hi: number, int = false) =>
  isNum(s) && Number(s) >= lo && Number(s) <= hi && (!int || Number.isInteger(Number(s)))
const isUrl = (s: string) => s.trim() === '' || /^https?:\/\/\S+$/i.test(s.trim())
const isTime = (s: string) => /^([01]\d|2[0-3]):[0-5]\d$/.test(s)

/** The same limits the device holds; it refuses anything outside them with a 400. */
export function validate(d: Draft): DraftErrors {
  const e: DraftErrors = {}
  if (!isUrl(d.src0)) e.src0 = 'An http:// address, or empty.'
  if (!isUrl(d.src1)) e.src1 = 'An http:// address, or empty.'
  if (!inRange(d.poll_ms, 500, 10000, true)) e.poll_ms = 'From 500 to 10,000.'
  if (!d.tz.trim()) e.tz = 'A POSIX time zone, such as UTC0 or CET-1CEST,M3.5.0,M10.5.0/3.'
  if (!inRange(d.lat, -90, 90)) e.lat = 'From -90 to 90.'
  if (!inRange(d.lon, -180, 180)) e.lon = 'From -180 to 180.'
  if (d.night && !isTime(d.night_start)) e.night_start = 'A time, HH:MM.'
  if (d.night && !isTime(d.night_end)) e.night_end = 'A time, HH:MM.'
  if (!isTime(d.alarm_time)) e.alarm_time = 'A time, HH:MM.'
  for (const k of ['w0name', 'w1name'] as const) {
    if (!d[k].trim() || d[k].trim().length > 15) e[k] = 'A name, up to 15 letters.'
  }
  for (const k of ['w0tz', 'w1tz'] as const) {
    if (!/^(<[^>]+>|[A-Za-z]{3,})[+-]?\d/.test(d[k].trim())) e[k] = 'A POSIX time zone, such as IST-5:30.'
  }
  if (!isHex(d.accent)) e.accent = 'A colour, #rrggbb.'
  for (const k of ['alert_cpu_temp', 'alert_gpu_temp', 'alert_vram_temp'] as const) {
    if (!inRange(d[k], 50, 120, true)) e[k] = '50 to 120.'
  }
  return e
}
