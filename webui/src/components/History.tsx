import { useEffect, useId, useLayoutEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react'
import { HudHeading, Skeleton, cn } from '../ui'
import { getJson, type History, type HistoryKey } from '../lib/api'

/**
 * THE LAST 24 HOURS: a reading a minute, kept by the device (core/history.c). Three small charts on
 * one time axis - load, temperature, sound - never two scales on one chart. The PC's CPU, its GPU
 * and the home lab keep their colours from chart to chart. Hovering one chart (or the arrow keys on
 * it) moves the crosshair on all three; the table under them has every hour's averages.
 */

const REFRESH_MS = 60_000

interface Series { key: HistoryKey; label: string; color: string }
interface ChartDef { title: string; unit: string; series: Series[]; domain?: [number, number] }

const VIZ = (n: 1 | 2 | 3) => `rgb(var(--pd-viz-${n}))`
const CHARTS: ChartDef[] = [
  {
    title: 'Load', unit: '%', domain: [0, 100],
    series: [
      { key: 'pc_cpu', label: 'PC CPU', color: VIZ(1) },
      { key: 'pc_gpu', label: 'PC GPU', color: VIZ(2) },
      { key: 'lab_cpu', label: 'Lab CPU', color: VIZ(3) },
    ],
  },
  {
    title: 'Temperature', unit: '°C',
    series: [
      { key: 'pc_cpu_temp', label: 'PC CPU', color: VIZ(1) },
      { key: 'pc_gpu_temp', label: 'PC GPU', color: VIZ(2) },
      { key: 'lab_cpu_temp', label: 'Lab CPU', color: VIZ(3) },
    ],
  },
  { title: 'Sound level', unit: 'dBA', series: [{ key: 'dba', label: 'Sound', color: 'rgb(var(--pd-accent))' }] },
]

const H = 150
const M = { top: 10, right: 56, bottom: 22, left: 34 }

function useHistory() {
  const [h, setH] = useState<History | null>(null)
  const [failed, setFailed] = useState(false)
  useEffect(() => {
    let stopped = false
    let timer: ReturnType<typeof setTimeout> | undefined
    const tick = async () => {
      if (!document.hidden) {
        try { const v = await getJson<History>('/api/history', 8000); if (!stopped) { setH(v); setFailed(false) } } catch { if (!stopped) setFailed(true) }
      }
      if (!stopped) timer = setTimeout(tick, REFRESH_MS)
    }
    tick()
    return () => { stopped = true; clearTimeout(timer) }
  }, [])
  return { h, failed }
}

function useWidth<T extends HTMLElement>() {
  const ref = useRef<T>(null)
  const [w, setW] = useState(0)
  useLayoutEffect(() => {
    const el = ref.current
    if (!el) return
    setW(el.clientWidth)
    const ro = new ResizeObserver(() => setW(el.clientWidth))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])
  return [ref, w] as const
}

/** When reading i was taken (ms), or null before the device's clock was set. */
const timeOf = (h: History, i: number) => (h.last ? (h.last - (h.count - 1 - i) * h.step_s) * 1000 : null)
const clock = (ms: number) => new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
const ago = (h: History, i: number) => {
  const m = Math.round(((h.count - 1 - i) * h.step_s) / 60)
  return m < 60 ? `${m} min ago` : `${Math.floor(m / 60)} h ${m % 60} min ago`
}
const when = (h: History, i: number) => { const t = timeOf(h, i); return t === null ? ago(h, i) : clock(t) }

/** Round ends and three or four ticks between them. */
function niceDomain(lo: number, hi: number): [number, number] {
  if (!Number.isFinite(lo)) return [0, 100]
  const span = Math.max(hi - lo, 10)
  const step = span > 60 ? 20 : span > 30 ? 10 : 5
  let a = Math.floor(lo / step) * step
  let b = Math.ceil(hi / step) * step
  if (b - a < step * 2) b = a + step * 2
  if (a < 0 && lo >= 0) a = 0
  return [a, b]
}
const ticksOf = ([a, b]: [number, number]) => {
  const span = b - a
  const step = span > 120 ? 40 : span > 60 ? 20 : span > 30 ? 10 : 5
  const out: number[] = []
  for (let v = a; v <= b + 1e-9; v += step) out.push(v)
  return out
}

export function HistorySection() {
  const { h, failed } = useHistory()
  const [hover, setHover] = useState<number | null>(null)
  const span = h && h.count > 1 ? `${Math.round(((h.count - 1) * h.step_s) / 3600)} h` : null

  return (
    <section aria-label="The last 24 hours">
      <HudHeading
        right={span && <span className="text-[11px] font-medium uppercase tracking-[0.12em] text-ink-2">{h!.count >= 1440 ? 'Last 24 h' : `Last ${span}`}</span>}
      >
        History
      </HudHeading>
      <div className="mt-3 space-y-3">
        {!h ? (
          failed ? (
            <p className="text-[13px] text-ink-2">The device did not send its history. This firmware may not keep one yet.</p>
          ) : (
            <Skeleton className="h-40 w-full" />
          )
        ) : h.count < 2 ? (
          <p className="rounded-xl border border-line bg-surface p-4 text-[13px] text-ink-2 shadow-card">
            The device keeps a reading a minute, from when it starts. The charts fill in over the next hours.
          </p>
        ) : (
          <>
            <div className="grid gap-3 lg:grid-cols-3 lg:gap-6">
              {CHARTS.map(c => (
                <Chart key={c.title} def={c} h={h} hover={hover} setHover={setHover} />
              ))}
            </div>
            <HistoryTable h={h} />
          </>
        )}
      </div>
    </section>
  )
}

function Chart({ def, h, hover, setHover }: {
  def: ChartDef
  h: History
  hover: number | null
  setHover: (i: number | null) => void
}) {
  const titleId = useId()
  const [focused, setFocused] = useState(false)
  const [mine, setMine] = useState(false)      // the pointer is over this chart: its tooltip shows
  const n = h.count
  const [box, W] = useWidth<HTMLDivElement>()
  const pw = Math.max(10, W - M.left - M.right)
  const ph = H - M.top - M.bottom

  const values = def.series.map(s => h.series[s.key] ?? [])
  const has = values.some(v => v.some(x => x !== null))
  const domain = useMemo<[number, number]>(() => {
    if (def.domain) return def.domain
    let lo = Infinity, hi = -Infinity
    for (const v of values) for (const x of v) if (x !== null) { lo = Math.min(lo, x); hi = Math.max(hi, x) }
    return niceDomain(lo, hi)
  }, [def.domain, values])
  const x = (i: number) => M.left + (n > 1 ? (i / (n - 1)) * pw : 0)
  const y = (v: number) => M.top + ph - ((Math.min(Math.max(v, domain[0]), domain[1]) - domain[0]) / (domain[1] - domain[0])) * ph

  const paths = values.map(v => {
    let d = ''
    let pen = false
    for (let i = 0; i < n; i++) {
      const val = v[i]
      if (val === null || val === undefined) { pen = false; continue }
      d += `${pen ? 'L' : 'M'}${x(i).toFixed(1)} ${y(val).toFixed(1)}`
      pen = true
    }
    return d
  })

  // The time axis: the clock's 00, 06, 12, 18 when the time is known, else hours ago.
  const xTicks: { i: number; label: string }[] = []
  if (h.last) {
    for (let i = 0; i < n; i++) {
      const d = new Date(timeOf(h, i)!)
      if (d.getMinutes() === 0 && d.getHours() % 6 === 0) xTicks.push({ i, label: d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) })
    }
  } else {
    for (let back = 0; back < n; back += 360) xTicks.push({ i: n - 1 - back, label: back ? `−${back / 60} h` : 'now' })
  }

  // Each line's name at its end, in the right-hand gutter; names too close get a leader line.
  const ends = def.series.length > 1
    ? def.series
      .map((s, k) => {
        let i = n - 1
        while (i >= 0 && values[k][i] == null) i--
        return i < 0 ? null : { s, ex: x(i), ey: y(values[k][i]!), ly: y(values[k][i]!) }
      })
      .filter((e): e is NonNullable<typeof e> => e !== null)
      .sort((a, b) => a.ey - b.ey)
    : []
  for (let k = 1; k < ends.length; k++) if (ends[k].ly - ends[k - 1].ly < 12) ends[k].ly = ends[k - 1].ly + 12
  const over = ends.length ? ends[ends.length - 1].ly - (M.top + ph + 4) : 0
  if (over > 0) for (const e of ends) e.ly -= over

  const pick = (e: PointerEvent<SVGSVGElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    const px = ((e.clientX - r.left) / r.width) * W
    setHover(Math.round(Math.min(Math.max((px - M.left) / pw, 0), 1) * (n - 1)))
  }
  const key = (e: KeyboardEvent) => {
    const stepBy = e.shiftKey ? 60 : 10
    if (e.key === 'ArrowLeft') setHover(Math.max(0, (hover ?? n - 1) - stepBy))
    else if (e.key === 'ArrowRight') setHover(Math.min(n - 1, (hover ?? n - 1) + stepBy))
    else if (e.key === 'Escape') setHover(null)
    else return
    e.preventDefault()
  }
  const showTip = hover !== null && (mine || focused)
  const fmt = (v: number | null | undefined) => (v == null ? '–' : `${Math.round(v)}`)

  return (
    <figure className="min-w-0 rounded-xl border border-line bg-surface p-4 shadow-card" aria-labelledby={titleId}>
      <figcaption className="mb-2 flex flex-wrap items-baseline justify-between gap-x-3 gap-y-1">
        <span id={titleId} className="text-sm font-medium text-ink">{def.title} <span className="font-normal text-ink-3">{def.unit}</span></span>
        {def.series.length > 1 && (
          <span className="flex flex-wrap gap-x-3 gap-y-1 text-[11px] text-ink-2">
            {def.series.map(s => (
              <span key={s.key} className="flex items-center gap-1.5">
                <span className="h-0.5 w-3 rounded-full" style={{ background: s.color }} aria-hidden="true" />
                {s.label}
              </span>
            ))}
          </span>
        )}
      </figcaption>
      <div ref={box} className="relative">
        {W > 0 && (
          <svg
            width={W}
            height={H}
            viewBox={`0 0 ${W} ${H}`}
            className="block touch-pan-y overflow-visible outline-none focus-visible:rounded focus-visible:ring-2 focus-visible:ring-accent"
            tabIndex={0}
            role="img"
            aria-label={`${def.title} over the last ${Math.round(((n - 1) * h.step_s) / 3600)} hours. Arrow keys read the values; the table below has them all.`}
            onPointerMove={e => { setMine(true); pick(e) }}
            onPointerDown={e => { setMine(true); pick(e) }}
            onPointerLeave={() => { setMine(false); setHover(null) }}
            onFocus={() => setFocused(true)}
            onBlur={() => { setFocused(false); setHover(null) }}
            onKeyDown={key}
          >
            {ticksOf(domain).map(v => (
              <g key={v}>
                <line x1={M.left} x2={M.left + pw} y1={y(v)} y2={y(v)} stroke="rgb(var(--pd-line))" strokeWidth={1} shapeRendering="crispEdges" />
                <text x={M.left - 6} y={y(v)} dy="0.32em" textAnchor="end" className="fill-ink-3 text-[10px] tabular-nums">{v}</text>
              </g>
            ))}
            {xTicks.map(t => (
              <text key={t.i} x={x(t.i)} y={H - 6} textAnchor="middle" className="fill-ink-3 text-[10px] tabular-nums">{t.label}</text>
            ))}
            {has ? paths.map((d, k) => (
              <path key={def.series[k].key} d={d} fill="none" stroke={def.series[k].color} strokeWidth={2} strokeLinejoin="round" strokeLinecap="round" />
            )) : (
              <text x={M.left + pw / 2} y={M.top + ph / 2} textAnchor="middle" className="fill-ink-3 text-[12px]">No readings yet</text>
            )}
            {ends.map(e => (
              <g key={e.s.key}>
                {Math.abs(e.ly - e.ey) > 1 && (
                  <line x1={e.ex + 3} y1={e.ey} x2={M.left + pw + 6} y2={e.ly} stroke="rgb(var(--pd-line-strong))" strokeWidth={1} />
                )}
                <text x={M.left + pw + 8} y={e.ly} dy="0.32em" className="fill-ink-2 text-[10px]">{e.s.label}</text>
              </g>
            ))}
            {hover !== null && hover < n && (
              <g pointerEvents="none">
                <line x1={x(hover)} x2={x(hover)} y1={M.top} y2={M.top + ph} stroke="rgb(var(--pd-ink-3))" strokeWidth={1} shapeRendering="crispEdges" />
                {values.map((v, k) => v[hover] != null && (
                  <circle key={k} cx={x(hover)} cy={y(v[hover]!)} r={4} fill={def.series[k].color} stroke="rgb(var(--pd-surface))" strokeWidth={2} />
                ))}
              </g>
            )}
          </svg>
        )}
        {showTip && hover !== null && (
          <div
            role="status"
            className={cn(
              'pointer-events-none absolute top-0 z-10 min-w-[7.5rem] rounded-lg border border-line bg-raised px-3 py-2 text-[12px] shadow-pop',
            )}
            style={x(hover) > W * 0.6 ? { right: W - x(hover) + 10 } : { left: x(hover) + 10 }}
          >
            <div className="mb-1 text-[11px] text-ink-2 tabular-nums">{when(h, hover)}</div>
            {def.series.map((s, k) => (
              <div key={s.key} className="flex items-center gap-2">
                <span className="h-0.5 w-3 shrink-0 rounded-full" style={{ background: s.color }} aria-hidden="true" />
                <span className="font-semibold tabular-nums text-ink">{fmt(values[k][hover])}{values[k][hover] != null && <span className="font-normal text-ink-2"> {def.unit}</span>}</span>
                <span className="text-ink-2">{s.label}</span>
              </div>
            ))}
          </div>
        )}
      </div>
    </figure>
  )
}

/** Every hour's averages, newest first: every value the charts draw, without needing to see them. */
function HistoryTable({ h }: { h: History }) {
  const per = Math.round(3600 / h.step_s)
  const rows: { at: string; v: (number | null)[] }[] = []
  const cols = CHARTS.flatMap(c => c.series.map(s => ({ ...s, unit: c.unit, chart: c.title })))
  for (let end = h.count; end > 0; end -= per) {
    const start = Math.max(0, end - per)
    rows.push({
      at: `${when(h, start)} – ${when(h, end - 1)}`,
      v: cols.map(c => {
        const arr = h.series[c.key] ?? []
        let sum = 0, k = 0
        for (let i = start; i < end; i++) { const x = arr[i]; if (x != null) { sum += x; k++ } }
        return k ? sum / k : null
      }),
    })
  }
  return (
    <details className="group rounded-xl border border-line bg-surface shadow-card">
      <summary className="cursor-pointer select-none list-none px-4 py-3 text-sm font-medium text-ink-2 hover:text-ink [&::-webkit-details-marker]:hidden">
        <span className="mr-2 inline-block transition-transform group-open:rotate-90" aria-hidden="true">›</span>
        Show as a table
      </summary>
      <div className="overflow-x-auto border-t border-line">
        <table className="w-full min-w-[40rem] text-left text-[12px] tabular-nums">
          <thead className="text-ink-2">
            <tr>
              <th scope="col" className="px-4 py-2 font-medium">Hour</th>
              {cols.map(c => (
                <th key={c.key} scope="col" className="px-2 py-2 text-right font-medium">
                  {c.chart === 'Load' ? `${c.label} load` : c.chart === 'Temperature' ? `${c.label} temp` : c.label}
                  <span className="text-ink-3"> {c.unit}</span>
                </th>
              ))}
            </tr>
          </thead>
          <tbody className="divide-y divide-line text-ink">
            {rows.map(r => (
              <tr key={r.at}>
                <th scope="row" className="whitespace-nowrap px-4 py-1.5 font-normal text-ink-2">{r.at}</th>
                {r.v.map((v, k) => <td key={k} className="px-2 py-1.5 text-right">{v == null ? '–' : Math.round(v)}</td>)}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </details>
  )
}
