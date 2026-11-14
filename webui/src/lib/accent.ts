/**
 * THE ACCENT: the device's accent colour (setting "accent") as the page's ramp, --pd-a300 .. --pd-a900
 * (styles/theme.css). The teal ramp is turned to the accent's hue, its saturation scaled by the
 * accent's, its lightness kept - the same as hud_set_accent() in the firmware, so the page and the
 * screens match. The saved accent's ramp is remembered, so the next visit paints in it straight away
 * (appearance.ts's pre-paint script).
 *
 * No DOM access at module level: vite.config.ts imports appearance.ts, which imports this, in Node.
 */

export const ACCENT_RAMP_KEY = 'pd_accent_ramp'
export const DEFAULT_ACCENT = '#2a8cb2'

/** The device's presets (settings.c TG_ACCENTS): the teal's saturation and lightness in other hues. */
export const ACCENTS = [
  { value: '#2a8cb2', label: 'Teal' },
  { value: '#2a57b2', label: 'Blue' },
  { value: '#622ab2', label: 'Violet' },
  { value: '#b22a84', label: 'Pink' },
  { value: '#b22a2a', label: 'Red' },
  { value: '#b2622a', label: 'Orange' },
  { value: '#b2892a', label: 'Amber' },
  { value: '#2ab262', label: 'Green' },
  { value: '#8c8c8c', label: 'Mono' },
]

const TEAL: Record<number, string> = {
  300: '#a8d5e6', 400: '#78bed9', 500: '#42a7cd', 600: '#2a8cb2', 700: '#206f8d', 800: '#1b5e78', 900: '#164e63',
}

type Rgb = [number, number, number]
type Hsl = [number, number, number]

const parse = (hex: string): Rgb => {
  const n = parseInt(hex.replace('#', ''), 16)
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255]
}

function toHsl([r, g, b]: Rgb): Hsl {
  const R = r / 255, G = g / 255, B = b / 255
  const mx = Math.max(R, G, B), mn = Math.min(R, G, B), d = mx - mn, l = (mx + mn) / 2
  if (d < 1e-6) return [0, 0, l]
  const s = d / (1 - Math.abs(2 * l - 1))
  const h = mx === R ? ((G - B) / d + 6) % 6 : mx === G ? (B - R) / d + 2 : (R - G) / d + 4
  return [h / 6, s, l]
}

function toRgb([h, s, l]: Hsl): Rgb {
  h -= Math.floor(h)
  s = Math.min(1, Math.max(0, s))
  const c = (1 - Math.abs(2 * l - 1)) * s, hp = h * 6, x = c * (1 - Math.abs((hp % 2) - 1)), m = l - c / 2
  const [r, g, b] = [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][Math.floor(hp) % 6]
  const byte = (v: number) => Math.round(Math.min(1, Math.max(0, v + m)) * 255)
  return [byte(r), byte(g), byte(b)]
}

export const isHex = (v: string) => /^#[0-9a-f]{6}$/i.test(v)

/** Each ramp level as "r g b", for the theme's variables. */
export function accentRamp(accent: string): Record<number, string> {
  const out: Record<number, string> = {}
  const same = !isHex(accent) || accent.toLowerCase() === DEFAULT_ACCENT
  const [ha, sa] = toHsl(parse(same ? DEFAULT_ACCENT : accent))
  const [ht, st] = toHsl(parse(DEFAULT_ACCENT))
  for (const [level, hex] of Object.entries(TEAL)) {
    const rgb = same ? parse(hex) : (([h, s, l]) => toRgb([h + ha - ht, s * (sa / st), l]))(toHsl(parse(hex)))
    out[Number(level)] = rgb.join(' ')
  }
  return out
}

/** The ramp as inline CSS for <html>: what the pre-paint script stamps before the page loads. */
export function rampCss(accent: string): string {
  return Object.entries(accentRamp(accent)).map(([level, rgb]) => `--pd-a${level}:${rgb};`).join('')
}

/** Paint the page in this accent; with remember, the next visit starts in it too. */
export function applyAccent(accent: string, remember = false) {
  const root = document.documentElement
  for (const [level, rgb] of Object.entries(accentRamp(accent))) root.style.setProperty(`--pd-a${level}`, rgb)
  if (!remember) return
  try {
    if (accent.toLowerCase() === DEFAULT_ACCENT) localStorage.removeItem(ACCENT_RAMP_KEY)
    else localStorage.setItem(ACCENT_RAMP_KEY, rampCss(accent))
  } catch { /* private mode: the page still paints, a moment later next time */ }
}
