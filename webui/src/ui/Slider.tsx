import { useId, type CSSProperties } from 'react'

/** A number on a range, its value read out at the right of the label. */
export function Slider({ label, value, min = 0, max = 100, step = 1, onChange, format = String }: {
  label: string
  value: number
  min?: number
  max?: number
  step?: number
  onChange: (value: number) => void
  format?: (value: number) => string
}) {
  const id = useId()
  const frac = max > min ? (value - min) / (max - min) : 0
  // The fill ends under the thumb's centre, which travels 22 px less than the track is wide.
  const style = { '--pd-fill': `calc(11px + (100% - 22px) * ${frac})` } as CSSProperties
  return (
    <div>
      <div className="flex items-baseline justify-between gap-3">
        <label htmlFor={id} className="text-sm font-medium text-ink">{label}</label>
        <output htmlFor={id} className="font-display text-[13px] font-bold tabular-nums tracking-wider text-accent-ink">
          {format(value)}
        </output>
      </div>
      <input
        id={id}
        type="range"
        className="pd-range"
        style={style}
        min={min}
        max={max}
        step={step}
        value={value}
        aria-valuetext={format(value)}
        onChange={e => onChange(Number(e.target.value))}
      />
    </div>
  )
}
