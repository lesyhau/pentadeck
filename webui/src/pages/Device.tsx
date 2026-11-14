import { useEffect, useState } from 'react'
import {
  Button, ButtonRow, Card, CheckIcon, ConfirmDialog, DownloadIcon, FileDrop, InlineNotice, LinkIcon, NoticeLine, PageTitle, Progress,
  RestartIcon, Reveal, StatTile, cn, type Note,
} from '../ui'
import {
  SETTINGS_LISTS, bytes, getJson, post, refusal, signalWords, uptime, type DeviceInfo, type DeviceSettings, type DeviceStatus,
} from '../lib/api'
import { PairCode, usePairing, type Send } from '../components/Pairing'

/**
 * THE DEVICE PAGE: whether this browser may change things (pairing), firmware updates, a reboot,
 * and what the device says about itself.
 */

// -- Access ---------------------------------------------------------------------------------------

function AccessCard({ paired, onToken, onForget }: {
  paired: boolean
  onToken: (token: string) => void
  onForget: () => void
}) {
  const { phase, start, reset } = usePairing(onToken)
  const [confirming, setConfirming] = useState(false)
  const asking = phase.k === 'asking'
  return (
    <Card title="Access" caption="A paired browser can save settings, test the LEDs and sounds, use the remote and update the firmware.">
      <div className="flex items-center gap-3">
        <span className={cn('grid size-10 shrink-0 place-items-center rounded-full', paired ? 'bg-accent/15 text-accent-ink' : 'bg-warn/15 text-warn-ink')}>
          {paired ? <CheckIcon size={20} /> : <LinkIcon size={20} />}
        </span>
        <div className="min-w-0" role="status">
          <p className="text-[15px] font-medium text-ink">{paired ? 'This browser is paired' : 'Not paired yet'}</p>
          <p className="mt-0.5 text-[13px] leading-snug text-ink-2">
            {paired ? 'Its key is kept in this browser only.' : 'Pairing takes one press of the power button on the device.'}
          </p>
        </div>
      </div>

      {!paired && (asking || phase.k === 'no') && (
        <div>
          {asking ? <PairCode code={phase.code} /> : phase.k === 'no' && <InlineNotice tone="warning">{phase.why}</InlineNotice>}
        </div>
      )}

      <ButtonRow>
        {paired ? (
          <Button variant="secondary" onClick={() => setConfirming(true)}>Forget this browser</Button>
        ) : asking ? (
          <Button variant="secondary" onClick={reset}>Cancel</Button>
        ) : (
          <Button onClick={start} loading={phase.k === 'starting'} icon={<LinkIcon size={16} />}>Pair with the device</Button>
        )}
      </ButtonRow>

      {confirming && (
        <ConfirmDialog
          title="Forget this browser?"
          description="It will need pairing again before it can change anything on the device."
          confirmLabel="Forget"
          onCancel={() => setConfirming(false)}
          onConfirm={() => { setConfirming(false); reset(); onForget() }}
        />
      )}
    </Card>
  )
}

// -- Firmware -------------------------------------------------------------------------------------

function FirmwareCard({ fw, running, onInstall, progress, notice }: {
  fw?: string
  running?: string
  onInstall: (file: File) => void
  /** Bytes sent and the total while an upload runs; null otherwise. */
  progress: { sent: number; total: number } | null
  notice: Note | null
}) {
  const [file, setFile] = useState<File | null>(null)
  const uploading = progress !== null
  const kb = (n: number) => Math.round(n / 1024).toLocaleString()
  return (
    <Card
      title="Firmware"
      caption={fw ? `Running ${fw} from ${running}. Installing restarts the device.` : 'Installing restarts the device.'}
    >
      <FileDrop
        accept=".bin,application/octet-stream"
        onFile={setFile}
        busy={uploading}
        label={file ? file.name : 'Choose an app image (.bin)'}
        hint={file ? `${kb(file.size)} KB · tap to choose another` : 'Or drop it here'}
      />
      <Reveal show={uploading}>
        {progress && (
          <Progress
            label="Uploading"
            figures={`${kb(progress.sent)} of ${kb(progress.total)} KB`}
            value={progress.total ? (progress.sent / progress.total) * 100 : undefined}
          />
        )}
      </Reveal>
      <div className="space-y-3">
        <ButtonRow>
          <Button onClick={() => file && onInstall(file)} disabled={!file || uploading} loading={uploading}>Install update</Button>
        </ButtonRow>
        <NoticeLine notice={notice} />
      </div>
    </Card>
  )
}

// -- Reboot ---------------------------------------------------------------------------------------

function RebootCard({ onReboot, rebooting, notice }: { onReboot: () => void; rebooting: boolean; notice: Note | null }) {
  const [confirming, setConfirming] = useState(false)
  return (
    <Card title="Reboot" caption="Restart the device. Saved settings stay as they are.">
      <div className="space-y-3">
        <ButtonRow>
          <Button variant="secondary" onClick={() => setConfirming(true)} loading={rebooting} icon={<RestartIcon size={16} />}>
            Reboot device
          </Button>
        </ButtonRow>
        <NoticeLine notice={notice} />
      </div>
      {confirming && (
        <ConfirmDialog
          icon={<RestartIcon size={20} />}
          title="Reboot the device?"
          description="The screens go dark for a few seconds while it starts again. Changes not yet saved stay on this page."
          confirmLabel="Reboot"
          onCancel={() => setConfirming(false)}
          onConfirm={() => { setConfirming(false); onReboot() }}
        />
      )}
    </Card>
  )
}

// -- Backup ---------------------------------------------------------------------------------------

/** What a backup leaves out: the lists the device sends along, and the old LED mode (the zones say it). */
const NOT_KEPT: readonly string[] = [...SETTINGS_LISTS, 'led_mode']

function keepable(s: Record<string, unknown>): Record<string, unknown> {
  return Object.fromEntries(Object.entries(s).filter(([k]) => !NOT_KEPT.includes(k)))
}

function BackupCard({ send, fw, onRestored }: { send: Send; fw?: string; onRestored: () => void }) {
  const [busy, setBusy] = useState<'save' | 'restore' | null>(null)
  const [notice, setNotice] = useState<Note | null>(null)
  const [pending, setPending] = useState<{ name: string; settings: Record<string, unknown> } | null>(null)

  const download = async () => {
    setBusy('save')
    setNotice(null)
    try {
      const s = await getJson<DeviceSettings>('/api/settings')
      const day = new Date().toISOString().slice(0, 10)
      const blob = new Blob([JSON.stringify({ pentadeck: fw ?? '', saved: new Date().toISOString(), settings: keepable(s as unknown as Record<string, unknown>) }, null, 2)], { type: 'application/json' })
      const a = document.createElement('a')
      a.href = URL.createObjectURL(blob)
      a.download = `pentadeck-settings-${day}.json`
      a.click()
      setTimeout(() => URL.revokeObjectURL(a.href), 10_000)
      setNotice({ tone: 'success', text: 'Saved to your downloads.' })
    } catch {
      setNotice({ tone: 'danger', text: 'The device did not send its settings.' })
    }
    setBusy(null)
  }

  const pick = async (file: File) => {
    setNotice(null)
    try {
      const j = JSON.parse(await file.text()) as Record<string, unknown>
      // A backup from this page wraps the settings; a bare GET /api/settings works too.
      const s = (typeof j.settings === 'object' && j.settings ? j.settings : j) as Record<string, unknown>
      if (!Array.isArray(s.sources) || typeof s.tz !== 'string') throw new Error('not settings')
      setPending({ name: file.name, settings: keepable(s) })
    } catch {
      setNotice({ tone: 'danger', text: 'That file is not a Pentadeck settings backup.' })
    }
  }

  const restore = async (p: { name: string; settings: Record<string, unknown> }) => {
    setBusy('restore')
    const r = await send(t => post('/api/settings', t, p.settings), { label: 'Restore now', run: () => restore(p) })
    setBusy(null)
    if (r.ok) {
      onRestored()
      setNotice({ tone: 'success', text: 'Restored. The device uses them now.' })
    } else if (r.text || r.status) {
      setNotice({ tone: 'danger', text: refusal(r) })
    }
  }

  return (
    <Card title="Backup" caption="All the settings in one file, to keep or to move to another device. Learned sounds and the clock image stay on the device.">
      <div className="space-y-3">
        <ButtonRow>
          <Button variant="secondary" onClick={download} loading={busy === 'save'} icon={<DownloadIcon size={16} />}>Download settings</Button>
        </ButtonRow>
        <FileDrop accept=".json,application/json" onFile={pick} busy={busy !== null} label="Restore from a backup (.json)" hint="Or drop it here" />
        <NoticeLine notice={notice} />
      </div>
      {pending && (
        <ConfirmDialog
          title="Replace the device's settings?"
          description={`Everything in ${pending.name} takes the place of what the device has now. Unsaved changes on the Settings page are lost.`}
          confirmLabel="Restore"
          onCancel={() => setPending(null)}
          onConfirm={() => { const p = pending; setPending(null); restore(p) }}
        />
      )}
    </Card>
  )
}

// -- Device info ----------------------------------------------------------------------------------

const RESETS: Record<string, string> = {
  'power-on': 'Power on',
  software: 'Restart',
  panic: 'Crash',
  'interrupt watchdog': 'Watchdog',
  'task watchdog': 'Watchdog',
  watchdog: 'Watchdog',
  brownout: 'Brownout',
  other: 'Other',
}

/** "Nov  7 2026 17:01:36" (the compiler's __DATE__ __TIME__) as "Nov 7 2026". */
const builtOn = (s?: string) => s?.replace(/\s+/g, ' ').split(' ').slice(0, 3).join(' ')

/**
 * The live figures come from /api/status (every 2 s, as on the overview); what does not change
 * while it runs (the build, the other slot, why it last restarted) from /status, once.
 */
function InfoCard({ status }: { status: DeviceStatus | null }) {
  const [info, setInfo] = useState<DeviceInfo | null>(null)
  const [failed, setFailed] = useState(false)
  useEffect(() => {
    let alive = true
    getJson<DeviceInfo>('/status').then(i => { if (alive) setInfo(i) }, () => { if (alive) setFailed(true) })
    return () => { alive = false }
  }, [])

  const now = status ?? info
  const loading = !now
  const [memory, memoryUnit] = now ? bytes(now.free_heap).split(' ') : []
  const reset = info?.reset
  const reason = reset ? RESETS[reset] ?? reset : undefined

  return (
    <Card title="Device info" caption={info?.idf ? `Built on ESP-IDF ${info.idf}.` : undefined}>
      <div className="grid grid-cols-2 gap-2 sm:grid-cols-3 sm:gap-3 lg:grid-cols-2">
        <StatTile label="Firmware" value={now?.fw} sub={builtOn(info?.built) ?? ' '} loading={loading} />
        <StatTile label="Running slot" value={now?.running} text sub={info?.other_slot ? `Other: ${info.other_slot}` : ' '} loading={loading} />
        <StatTile label="IP address" value={now?.ip} text sub="Wi-Fi" loading={loading} />
        <StatTile
          label="Wi-Fi signal"
          value={now ? String(now.rssi).replace('-', '−') : undefined}
          unit="dBm"
          sub={now ? signalWords(now.rssi) : undefined}
          loading={loading}
        />
        <StatTile label="Uptime" value={now ? uptime(now.uptime_s) : undefined} text sub="Since the last start" loading={loading} />
        <StatTile
          label="Free memory"
          value={memory}
          unit={memoryUnit}
          sub={status ? `${bytes(status.free_psram)} of it PSRAM` : ' '}
          loading={loading}
        />
        <StatTile
          label="Last reset"
          value={reason}
          text
          sub={reset && reason !== reset ? reset : ' '}
          loading={!info && !failed}
        />
      </div>
    </Card>
  )
}

// -- The page -------------------------------------------------------------------------------------

export function DevicePage({ status, paired, send, onRestored, onToken, onForget, onInstall, progress, onReboot, rebooting, notices }: {
  status: DeviceStatus | null
  send: Send
  onRestored: () => void
  paired: boolean
  onToken: (token: string) => void
  onForget: () => void
  onInstall: (file: File) => void
  progress: { sent: number; total: number } | null
  onReboot: () => void
  rebooting: boolean
  notices: { firmware: Note | null; reboot: Note | null }
}) {
  return (
    <>
      <PageTitle title="Device" meta="Access, updates and what the device says about itself." className="mb-6" />
      <div className="grid items-start gap-4 sm:gap-6 lg:grid-cols-2">
        <div className="space-y-4 sm:space-y-6">
          <AccessCard paired={paired} onToken={onToken} onForget={onForget} />
          <FirmwareCard fw={status?.fw} running={status?.running} onInstall={onInstall} progress={progress} notice={notices.firmware} />
          <RebootCard onReboot={onReboot} rebooting={rebooting} notice={notices.reboot} />
        </div>
        <div className="space-y-4 sm:space-y-6">
          <InfoCard status={status} />
          <BackupCard send={send} fw={status?.fw} onRestored={onRestored} />
        </div>
      </div>
    </>
  )
}
