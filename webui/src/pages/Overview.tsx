import type { ReactNode } from 'react'
import { HudHeading, Meter, Notice, PageTitle, Reveal, StatGrid, StatTile, StatusDot, loadTone, type MeterTone } from '../ui'
import { bytes, micLevel, round, signalWords, uptime, type DeviceStatus, type SourceStatus } from '../lib/api'

/**
 * THE LIVE OVERVIEW: the two machines the device watches, and the device itself. Each group is a
 * HUD heading over a grid of stat tiles.
 */

const num = (v: number | null | undefined) => round(v) ?? undefined

/** A figure with a load meter under it. */
function Gauge({ label, value, tone, loading }: {
  label: string
  value: number | null | undefined
  tone?: MeterTone
  loading: boolean
}) {
  const pct = value ?? 0
  return (
    <StatTile
      label={label}
      loading={loading}
      value={num(value)}
      unit={value == null ? undefined : '%'}
      meter={<Meter value={pct} tone={value == null ? 'muted' : tone ?? loadTone(pct)} />}
    />
  )
}

function Temp({ label, value, loading }: { label: string; value: number | null | undefined; loading: boolean }) {
  return <StatTile label={label} loading={loading} value={num(value)} unit={value == null ? undefined : '°C'} />
}

/** Where a machine's reports stand: live, stale, or never heard from. */
function SourceState({ src }: { src?: SourceStatus }) {
  if (!src) return null
  const state = src.ok ? 'Live' : src.age_ms < 0 ? 'No data' : 'Stale'
  return (
    <span className="flex items-center gap-2 text-[11px] font-medium uppercase tracking-[0.12em] text-ink-2">
      <StatusDot tone={src.ok ? 'live' : 'down'} />
      {state}
    </span>
  )
}

function HostGroup({ title, src, loading, children }: { title: string; src?: SourceStatus; loading: boolean; children: ReactNode }) {
  return (
    <section aria-label={title}>
      <HudHeading right={<SourceState src={src} />}>{title}</HudHeading>
      <p className="mb-3 mt-1 truncate text-[13px] text-ink-2">{src?.host || (loading ? ' ' : 'Not configured')}</p>
      <StatGrid>{children}</StatGrid>
    </section>
  )
}

export function Overview({ status, online }: { status: DeviceStatus | null; online: boolean | null }) {
  const loading = !status
  const pc = status?.sources[0]
  const lab = status?.sources[1]
  const mic = status ? micLevel(status.mic) : null
  const w = status?.weather
  const [memory, memoryUnit] = status ? bytes(status.free_heap).split(' ') : []
  const battery = lab?.battery
  const batteryTone: MeterTone = battery == null ? 'muted' : battery <= 20 ? 'danger' : battery <= 40 ? 'warn' : 'accent'

  return (
    <div className="space-y-8">
      <PageTitle title="Overview" meta={status ? `${status.ip} · refreshed every 2 seconds` : 'Waiting for the device…'} />

      <Reveal show={online === false}>
        <Notice tone="warning" title="The device is not answering" role="status">
          {status ? 'Showing the last reading. The page keeps trying every 2 seconds.' : 'The page keeps trying every 2 seconds.'}
        </Notice>
      </Reveal>

      <div className="grid gap-8 lg:grid-cols-2 lg:gap-6">
        <HostGroup title="PC" src={pc} loading={loading}>
          <Gauge label="CPU load" value={pc?.cpu_load} loading={loading} />
          <Temp label="CPU temp" value={pc?.cpu_temp} loading={loading} />
          <Gauge label="RAM" value={pc?.ram_pct} loading={loading} />
          <Gauge label="GPU load" value={pc?.gpu_load} loading={loading} />
          <Temp label="GPU temp" value={pc?.gpu_temp} loading={loading} />
          <Temp label="VRAM temp" value={pc?.vram_temp} loading={loading} />
        </HostGroup>

        <HostGroup title="Home lab" src={lab} loading={loading}>
          <Gauge label="CPU load" value={lab?.cpu_load} loading={loading} />
          <Temp label="CPU temp" value={lab?.cpu_temp} loading={loading} />
          <Gauge label="RAM" value={lab?.ram_pct} loading={loading} />
          {/* A battery fills the other way: low is the warning. */}
          <Gauge label="Battery" value={battery} tone={batteryTone} loading={loading} />
          <StatTile label="Services" value={lab?.services_up} sub="Up" loading={loading} />
          <StatTile label="Reported" value={lab ? reportAge(lab.age_ms) : undefined} text loading={loading} />
        </HostGroup>
      </div>

      <section aria-label="Device">
        <HudHeading
          right={
            status && (
              <span className="text-[11px] font-medium uppercase tracking-[0.12em] text-ink-2">
                Up {uptime(status.uptime_s)}
                {status.power && (status.power.state === 'idle' ? ' · Low power' : ' · Full power')}
              </span>
            )
          }
        >
          Device
        </HudHeading>
        <StatGrid className="mt-3 lg:grid-cols-6">
          <StatTile label="Firmware" value={status?.fw} sub={status?.running} loading={loading} />
          <StatTile
            label="Wi-Fi"
            value={status ? String(status.rssi).replace('-', '−') : undefined}
            unit="dBm"
            sub={status ? signalWords(status.rssi) : undefined}
            loading={loading}
          />
          <StatTile label="Memory" value={memory} unit={memoryUnit} sub="Free" loading={loading} />
          <StatTile
            label="Outside"
            value={w?.ok ? num(w.temp_c) : undefined}
            unit={w?.ok && w.temp_c != null ? '°C' : undefined}
            sub={w ? (w.ok ? 'Weather' : 'Not yet') : undefined}
            loading={loading}
          />
          <StatTile
            label="Humidity"
            value={w?.ok ? num(w.humidity) : undefined}
            unit={w?.ok && w.humidity != null ? '%' : undefined}
            sub="Outside"
            loading={loading}
          />
          <StatTile
            label="Mic"
            value={mic?.text ? mic.text.split(' ')[0] : undefined}
            unit={mic?.text ? mic.text.split(' ')[1] : undefined}
            meter={<Meter value={mic?.pct ?? 0} tone={mic?.text ? 'accent' : 'muted'} />}
            loading={loading}
          />
        </StatGrid>
      </section>
    </div>
  )
}

function reportAge(ms: number): string {
  if (ms < 0) return 'Never'
  if (ms < 1500) return 'Just now'
  if (ms < 60_000) return `${Math.round(ms / 1000)} s ago`
  return `${Math.round(ms / 60_000)} min ago`
}
