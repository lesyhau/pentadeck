/**
 * The device's HTTP API: what the firmware answers, and the few words this page needs about it.
 * Every write needs the device's token in `X-Token`. This browser gets it by pairing (the owner
 * presses power on the device) and keeps it in localStorage only.
 */

export interface SourceStatus {
  ok: boolean
  name: string
  host: string
  /** How old the last report is; negative when there has never been one. */
  age_ms: number
  cpu_load: number | null
  cpu_temp: number | null
  ram_pct: number | null
  gpu_load?: number | null
  gpu_temp?: number | null
  vram_temp?: number | null
  battery?: number | null
  services_up?: string
}

export interface DeviceStatus {
  fw: string
  running: string
  uptime_s: number
  ip: string
  rssi: number
  free_heap: number
  free_psram: number
  time: string
  weather: { ok: boolean; temp_c: number | null; humidity: number | null; code: number; age_s: number }
  sources: SourceStatus[]
  mic: { mode: string; dbfs: number | null; dba?: number | null }
  /** What the screens show: "DASH", "TUNER", "TEMPO", "WORLD"... Older firmware leaves it out. */
  mode?: string
  /** Low power when nobody is about (firmware 0.16+): the state, what last showed someone, how long ago. */
  power?: { state: 'full' | 'idle'; last_sign: string; quiet_s: number }
  /** The day's sound dose (NIOSH: 85 dBA for 8 hours is 100 %) and its Leq (firmware 0.21+). */
  hearing?: { dose_pct: number | null; leq_dba: number | null }
}

export interface DeviceSettings {
  sources: string[]
  poll_ms: number
  tz: string
  lat: number
  lon: number
  hour24: boolean
  brightness: number
  night_start: string
  night_end: string
  night_brightness: number
  layout: string[]
  buzzer: boolean
  buzzer_volume: number
  quiet_night: boolean
  alert_cpu_temp: number
  alert_gpu_temp: number
  alert_vram_temp: number
  /** The old single LED mode, now a preset over the zones; read back as the preset, or 'custom'. */
  led_mode?: string
  /** Each LED zone's effect, one of led_effects (firmware 0.19+). */
  led_left?: string
  led_back?: string
  led_right?: string
  /** Each LED zone's colour, '#rrggbb' (firmware 0.20+). */
  led_left_color?: string
  led_back_color?: string
  led_right_color?: string
  led_brightness: number
  key_beep?: boolean
  chime?: boolean
  alarm_on?: boolean
  alarm_time?: string
  alarm_days?: number
  world_names?: string[]
  world_tz?: string[]
  /** Minutes without a sign of anyone before low power, 0 = never (firmware 0.16+). */
  idle_min?: number
  idle_brightness?: number
  /** PCs may stream the mic over Wi-Fi (firmware 0.17+). */
  mic_stream?: boolean
  /** The AUDIO screen: 'eq' (16-band equaliser), 'vu' (a needle VU meter) (firmware 0.18+), or one of the
   *  visual effects (firmware 0.21+): see audio_views. */
  audio_view?: string
  /** The screens' and this page's accent, '#rrggbb' (firmware 0.21+). */
  accent?: string
  /** The visual effects' colours: 'accent', 'rainbow' or 'fire' (firmware 0.21+). */
  viz_palette?: string
  /** What a double clap does: one of clap_actions, 'off' by default (firmware 0.21+). */
  clap?: string
  /** A warning on the screens when the day's sound dose passes 100 % (firmware 0.21+). */
  noise_alert?: boolean
  /** The World mode's clocks on screens 2, 3, 4: 0 = home, 1 and 2 = world_names[0] and [1] (firmware 0.19+). */
  world_order?: number[]
  led_modes?: string[]
  led_effects?: string[]
  panels?: string[]
  audio_views?: string[]
  viz_palettes?: string[]
  clap_actions?: string[]
}

/** The lists GET /api/settings sends along, which POST does not take back. */
export const SETTINGS_LISTS = ['led_modes', 'led_effects', 'panels', 'audio_views', 'viz_palettes', 'clap_actions'] as const

export const PANEL_NAMES: Record<string, string> = { lab: 'Home lab', cpu: 'CPU', nav: 'Clock', gpu: 'GPU', audio: 'Audio', off: 'Off' }
export const SLOT_NAMES = ['Left', 'Second', 'Middle', 'Fourth', 'Right']
export const AUDIO_VIEW_NAMES: Record<string, string> = {
  eq: 'Equaliser', vu: 'VU meter', mirror: 'Mirror', scope: 'Oscilloscope', waterfall: 'Waterfall', ripple: 'Ripple', radial: 'Radial',
}
export const AUDIO_VIEW_HINTS: Record<string, string> = {
  eq: '16 bars, from bass to treble.',
  vu: 'A needle meter, like on a stereo.',
  mirror: 'The spectrum mirrored up and down, bass in the middle.',
  scope: 'The sound wave itself, like an oscilloscope.',
  waterfall: 'The spectrum over the last few seconds, bass at the bottom.',
  ripple: 'Rings sent out by the beat.',
  radial: 'The spectrum around a circle.',
}
export const LED_EFFECT_NAMES: Record<string, string> = {
  off: 'Off', glow: 'Glow', breathe: 'Breathe', wave: 'Wave', comet: 'Comet', sparkle: 'Sparkle',
  pulse: 'Pulse', fire: 'Fire', rainbow: 'Rainbow', spectrum: 'Spectrum', cpu: 'CPU load', gpu: 'GPU load', lab: 'Home lab', music: 'Music',
}
export const LED_EFFECT_HINTS: Record<string, string> = {
  off: 'Dark.',
  glow: 'Steady.',
  breathe: 'Fades in and out.',
  wave: 'A band of light running round.',
  comet: 'A bright dot with a fading tail.',
  sparkle: 'Random twinkles.',
  pulse: 'Brighter as the room gets louder.',
  fire: 'Flickering embers.',
  rainbow: 'Cycling colours.',
  spectrum: 'The music along the strip: bass at one end, treble at the other.',
  cpu: 'Fills with CPU load. Orange, then red, when busy or hot.',
  gpu: 'Fills with GPU load. Orange, then red, when busy or hot.',
  lab: 'Your colour when all is well, red when something is down.',
  music: 'A loudness meter. Orange, then red, when loud.',
}
/** Effects that bring their own colours, so the colour picker has nothing to do. */
export const LED_OWN_COLOURS = ['off', 'fire', 'rainbow', 'spectrum']
export const CLAP_NAMES: Record<string, string> = {
  off: 'Nothing', leds: 'LEDs on or off', light: 'Screens on or off', mode: 'Next mode', dash: 'Back to the dashboard',
}
export const LED_DEFAULT_COLOUR = '#2a8cb2'
/** The old single modes, as effects for the left strip, the back light and the right strip. */
export const LED_PRESETS: Record<string, [string, string, string]> = {
  off: ['off', 'off', 'off'], glow: ['glow', 'glow', 'glow'], status: ['cpu', 'lab', 'gpu'], music: ['music', 'music', 'music'],
}

/** GET /status: the firmware's own record of itself (no token needed). */
export interface DeviceInfo {
  fw: string
  idf?: string
  built?: string
  running: string
  other_slot?: string
  uptime_s: number
  rssi: number
  ip: string
  free_heap: number
  /** Why it last restarted: "power-on", "software", "panic", "brownout", a watchdog... */
  reset?: string
}

export const TOKEN_KEY = 'pd_token'
/** Where earlier versions of this page kept the token; moved to TOKEN_KEY on first read. */
const OLD_TOKEN_KEY = 'tg_token'

export function readToken(): string {
  try {
    const token = localStorage.getItem(TOKEN_KEY)
    if (token !== null) return token
    const old = localStorage.getItem(OLD_TOKEN_KEY)
    if (old === null) return ''
    localStorage.setItem(TOKEN_KEY, old)
    localStorage.removeItem(OLD_TOKEN_KEY)
    return old
  } catch {
    return ''
  }
}
export const writeToken = (t: string) => { try { localStorage.setItem(TOKEN_KEY, t) } catch { /* private mode */ } }
/** Forget this browser: it needs pairing again before it can change anything. */
export const clearToken = () => {
  try { localStorage.removeItem(TOKEN_KEY); localStorage.removeItem(OLD_TOKEN_KEY) } catch { /* private mode */ }
}

/**
 * PAIRING: the device asks on its own screens ("PAIR THIS BROWSER? 4821"); pressing power there hands
 * this browser the token. Start returns the request and the code both sides show; poll until the
 * owner answers (60 s on the device).
 */
export type PairStart = { ok: true; id: string; code: string } | { ok: false; busy: boolean }
export async function pairStart(): Promise<PairStart> {
  try {
    const r = await fetch('/api/pair', { method: 'POST' })
    if (!r.ok) return { ok: false, busy: r.status === 429 }
    const j = await r.json() as { id: number; code: string }
    return { ok: true, id: String(j.id), code: j.code }
  } catch {
    return { ok: false, busy: false }
  }
}

export type PairState = { state: 'waiting' } | { state: 'allowed'; token: string } | { state: 'refused' } | { state: 'offline' }
export async function pairPoll(id: string): Promise<PairState> {
  try {
    const r = await fetch(`/api/pair?id=${encodeURIComponent(id)}`, { cache: 'no-store' })
    if (!r.ok) return { state: 'offline' }
    return await r.json() as PairState
  } catch {
    return { state: 'offline' }
  }
}

/** A write's outcome: the HTTP status (0 when the device did not answer) and its words. */
export interface WriteResult {
  ok: boolean
  status: number
  text: string
}

export async function getJson<T>(url: string, timeoutMs = 5000): Promise<T> {
  const ctl = new AbortController()
  const timer = setTimeout(() => ctl.abort(), timeoutMs)
  try {
    const r = await fetch(url, { cache: 'no-store', signal: ctl.signal })
    if (!r.ok) throw new Error(`${r.status}`)
    return await r.json() as T
  } finally {
    clearTimeout(timer)
  }
}

export async function post(url: string, token: string, body?: unknown): Promise<WriteResult> {
  try {
    const headers: Record<string, string> = { 'X-Token': token }
    if (body !== undefined) headers['Content-Type'] = 'application/json'
    const r = await fetch(url, { method: 'POST', headers, body: body === undefined ? undefined : JSON.stringify(body) })
    return { ok: r.ok, status: r.status, text: (await r.text()).trim() }
  } catch {
    return { ok: false, status: 0, text: 'The device is not answering.' }
  }
}

export const needsToken = (status: number) => status === 401 || status === 403

/** What a write that was not sent says: this browser has no token, so it would be refused. */
export const NOT_PAIRED: WriteResult = { ok: false, status: 401, text: '' }

/** A failed write in a few words, under the button that sent it. */
export const refusal = (r: WriteResult) =>
  needsToken(r.status) ? 'Pair this browser with the device first.' : r.text || `The device answered ${r.status}.`

/** The device's buttons, as /api/button names them, and the ways one can be pressed. */
export type ButtonKey = 'light' | 'left' | 'power' | 'right' | 'mode'
export type ButtonEvent = 'short' | 'long' | 'down'

export const pressButton = (k: ButtonKey, e: ButtonEvent, token: string) => post(`/api/button?k=${k}&e=${e}`, token)

/** A screen is 128 x 128 pixels; /api/screen?s=0..4 sends one as big-endian RGB565. */
export const SCREEN_PX = 128

/** One screen's current frame (32768 bytes), no token needed. Throws when it is not that. */
export async function getScreen(s: number, signal: AbortSignal): Promise<Uint8Array> {
  const r = await fetch(`/api/screen?s=${s}`, { cache: 'no-store', signal })
  if (!r.ok) throw new Error(`${r.status}`)
  const frame = new Uint8Array(await r.arrayBuffer())
  if (frame.length !== SCREEN_PX * SCREEN_PX * 2) throw new Error('short frame')
  return frame
}

/** The OTA upload, with its progress - fetch cannot report an upload's progress, XMLHttpRequest can. */
export function uploadFirmware(file: File, token: string, onProgress: (sent: number, total: number) => void): Promise<WriteResult> {
  return new Promise(resolve => {
    const x = new XMLHttpRequest()
    x.open('POST', '/ota')
    x.setRequestHeader('X-Token', token)
    x.setRequestHeader('Content-Type', 'application/octet-stream')
    x.upload.onprogress = e => { if (e.lengthComputable) onProgress(e.loaded, e.total) }
    x.onload = () => resolve({ ok: x.status === 200, status: x.status, text: x.responseText.trim() })
    x.onerror = () => resolve({ ok: false, status: 0, text: 'The upload failed.' })
    x.send(file)
  })
}

// -- Formatting -------------------------------------------------------------------------------------

export const round = (v: number | null | undefined) => (v === null || v === undefined ? null : Math.round(v))

export function uptime(s: number): string {
  const d = Math.floor(s / 86400)
  const h = Math.floor((s % 86400) / 3600)
  const m = Math.floor((s % 3600) / 60)
  return d ? `${d}d ${h}h` : h ? `${h}h ${m}m` : `${m}m`
}

/** The microphone: A-weighted dB SPL when the firmware reports it, else the old dBFS offset. */
export function micLevel(mic: DeviceStatus['mic']): { text: string | null; pct: number } {
  if (typeof mic.dba === 'number') {
    return { text: `${Math.round(mic.dba)} dBA`, pct: clamp01((mic.dba - 30) / 70) * 100 }
  }
  if (typeof mic.dbfs === 'number') {
    return { text: `${Math.round(mic.dbfs + 120)} dB`, pct: clamp01((mic.dbfs + 80) / 75) * 100 }
  }
  return { text: null, pct: 0 }
}

const clamp01 = (v: number) => Math.max(0, Math.min(1, v))

/** Wi-Fi signal in words, for the line under the dBm. */
export function signalWords(rssi: number): string {
  if (rssi >= -50) return 'Excellent'
  if (rssi >= -60) return 'Good'
  if (rssi >= -70) return 'Fair'
  return 'Weak'
}

export function bytes(n: number): string {
  if (n >= 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`
  return `${Math.round(n / 1024)} KB`
}

// -- Sound: the live spectrum, the mic stream, learned sounds -----------------------------------------

/** GET /api/spectrum: the screens' 16 bands, bass first, 0..100, and the level. */
export interface Spectrum { bands: number[]; dba: number | null }

/** The mic stream (port 8081): an endless 16-bit mono WAV at 32 kHz. The token goes in the URL. */
export const streamUrl = (token: string) => `http://${location.hostname}:8081/?token=${encodeURIComponent(token)}`

export interface SoundSlot { used: boolean; on: boolean; name: string }
export interface SoundsState {
  slots: SoundSlot[]
  /** The slot being learned, or -1. */
  learning: number
  left_ms: number
  /** How the last learning ended. */
  result: 'none' | 'learned' | 'nothing'
}
export const soundsPost = (token: string, slot: number, query: string) => post(`/api/sounds?slot=${slot}&${query}`, token)

// -- The clock image ---------------------------------------------------------------------------------

/** The image in place of the logo on the clock screen: EMBLEM_PX square, big-endian RGB565. */
export const EMBLEM_PX = 36

export async function postBinary(url: string, token: string, body: Uint8Array<ArrayBuffer>): Promise<WriteResult> {
  try {
    const r = await fetch(url, { method: 'POST', headers: { 'X-Token': token, 'Content-Type': 'application/octet-stream' }, body })
    return { ok: r.ok, status: r.status, text: (await r.text()).trim() }
  } catch {
    return { ok: false, status: 0, text: 'The device is not answering.' }
  }
}

// -- History -----------------------------------------------------------------------------------------

export type HistoryKey = 'pc_cpu' | 'pc_cpu_temp' | 'pc_gpu' | 'pc_gpu_temp' | 'lab_cpu' | 'lab_cpu_temp' | 'dba'
/** GET /api/history: a value a minute for up to 24 hours, oldest first; null where nothing was known.
 *  `last` is when the newest minute ended (Unix seconds, 0 before the clock was set). */
export interface History { step_s: number; count: number; last: number; series: Record<HistoryKey, (number | null)[]> }

// -- Notifications -----------------------------------------------------------------------------------

export interface Notification { title: string; text: string; seconds: number; sound: string; color?: string }
export const NOTIFY_SOUNDS = ['chime', 'none', 'up', 'down', 'warn', 'test', 'alarm']
