import { useCallback, useEffect, useRef, useState } from 'react'
import { AppBar, AppearanceMenu, PageTitle, SaveBar, StatusDot, cn, setAppearance, useAppearance, type Note } from './ui'
import {
  NOT_PAIRED, TOKEN_KEY, clearToken, needsToken, post, pressButton, readToken, refusal, uploadFirmware, writeToken,
  type ButtonEvent, type ButtonKey,
} from './api'
import { usePage } from './router'
import { TabBar, TopTabs } from './Nav'
import { useStatus } from './useStatus'
import { useSettings } from './useSettings'
import { PairDialog, type Retry, type Send } from './Pairing'
import { Overview } from './Overview'
import { Remote } from './Remote'
import { SettingsPage } from './Settings'
import { DevicePage } from './Device'

/**
 * THE SHELL: the header, the four pages and their tabs, and everything the pages share - the live
 * status, the settings draft (so it outlives a visit to another page), this browser's token, the
 * pairing dialog, and every write to the device.
 */
export function App() {
  const page = usePage()
  const { status, online } = useStatus()
  const appearance = useAppearance()

  // -- Pairing: the token this browser holds, and the dialog that gets one when a write needs it ----
  const [token, setTokenState] = useState(readToken)
  const tokenRef = useRef(token)
  const keep = useCallback((t: string) => { tokenRef.current = t; setTokenState(t) }, [])
  // Paired or forgotten in another tab of this browser: this one follows.
  useEffect(() => {
    const onStorage = (e: StorageEvent) => { if (e.key === null || e.key === TOKEN_KEY) keep(readToken()) }
    window.addEventListener('storage', onStorage)
    return () => window.removeEventListener('storage', onStorage)
  }, [keep])

  const [notices, setNotices] = useState<Record<string, Note | null>>({})
  const paired = useCallback((t: string) => {
    writeToken(t)
    keep(t)
    // What was refused for want of pairing no longer holds.
    setNotices({})
  }, [keep])
  const forget = useCallback(() => { clearToken(); keep('') }, [keep])

  const [asking, setAsking] = useState<{ refused: boolean; retry?: Retry } | null>(null)
  const send = useCallback<Send>(async (go, retry) => {
    const t = tokenRef.current
    // No token: the device would refuse it, so ask for pairing instead of sending.
    if (!t) { setAsking({ refused: false, retry }); return NOT_PAIRED }
    const r = await go(t)
    if (needsToken(r.status)) setAsking({ refused: true, retry })
    return r
  }, [])

  const settings = useSettings(send)

  // -- One-off actions, each with its result under its buttons ----------------------------------------
  const [ledBusy, setLedBusy] = useState<'test' | 'resume' | null>(null)
  const [beeping, setBeeping] = useState(false)
  const [rebooting, setRebooting] = useState(false)
  const note = useCallback((where: string, n: Note | null) => {
    setNotices(all => ({ ...all, [where]: n }))
    if (n?.tone === 'success') setTimeout(() => setNotices(all => (all[where] === n ? { ...all, [where]: null } : all)), 6000)
  }, [])
  const act = async (where: string, url: string, done: string, retry: Retry) => {
    note(where, null)
    const r = await send(t => post(url, t), retry)
    note(where, r.ok ? { tone: 'success', text: done } : { tone: 'danger', text: refusal(r) })
  }
  const testLeds = async () => {
    setLedBusy('test')
    await act('leds', '/api/leds?mode=groups&b=60', 'Test pattern on for two minutes.', { label: 'Test pattern', run: testLeds })
    setLedBusy(null)
  }
  const resumeLeds = async () => {
    setLedBusy('resume')
    await act('leds', '/api/leds?mode=auto', 'Effects resumed.', { label: 'Resume effects', run: resumeLeds })
    setLedBusy(null)
  }
  const beep = async (p: number) => {
    setBeeping(true)
    await act('sounds', `/api/beep?p=${p}&v=${settings.draft?.buzzer_volume ?? 40}`, 'Playing on the device.', { label: 'Play', run: () => beep(p) })
    setBeeping(false)
  }
  const reboot = async () => {
    setRebooting(true)
    await act('reboot', '/reboot', 'Rebooting… the page reconnects by itself.', { label: 'Reboot now', run: reboot })
    setRebooting(false)
  }

  // The remote's presses: no word when they land (the screens show it), a line when they do not.
  const pressKey = useCallback(async (k: ButtonKey, e: ButtonEvent) => {
    const r = await send(t => pressButton(k, e, t))
    note('remote', r.ok ? null : { tone: 'danger', text: refusal(r) })
    return r.ok
  }, [send, note])

  const [progress, setProgress] = useState<{ sent: number; total: number } | null>(null)
  const install = async (file: File) => {
    note('firmware', null)
    const r = await send(async t => {
      setProgress({ sent: 0, total: file.size })
      const r = await uploadFirmware(file, t, (sent, total) => setProgress({ sent, total }))
      setProgress(null)
      return r
    }, { label: 'Install now', run: () => install(file) })
    note('firmware', r.ok
      ? { tone: 'success', text: 'Installed. The device is restarting…' }
      : { tone: 'danger', text: refusal(r) })
  }

  // -- Layout -----------------------------------------------------------------------------------------
  const clock = status?.time ? status.time.slice(11, 16) : '--:--'
  const live = online !== false
  const onSettings = page === 'settings'
  const barShown = onSettings && (settings.dirty || !!settings.saveStatus)

  return (
    <>
      <AppBar nav={<TopTabs page={page} dirty={settings.dirty} />}>
        <div className="flex h-9 items-center gap-2 rounded-full border border-line px-3" title="Device time">
          <StatusDot tone={online === null ? 'idle' : live ? 'live' : 'down'} />
          <span className="font-display text-[13px] font-bold tabular-nums tracking-wider text-ink">{clock}</span>
          <span role="status" className="sr-only text-xs text-ink-2 sm:not-sr-only">
            {online === null ? 'Connecting' : live ? 'Connected' : 'Not answering'}
          </span>
        </div>
        <AppearanceMenu value={appearance} onChange={setAppearance} />
      </AppBar>

      {/* The bottom padding keeps the end of every page clear of the tab bar (on a phone) and the save bar. */}
      <main
        key={page}
        className={cn(
          'pd-wrap pd-rise pt-6 transition-[padding] duration-300 sm:pt-8',
          barShown
            ? 'pb-[calc(var(--pd-tabbar)+env(safe-area-inset-bottom)+8rem)] lg:pb-32'
            : 'pb-[calc(var(--pd-tabbar)+env(safe-area-inset-bottom)+3rem)] lg:pb-12',
        )}
      >
        {page === 'overview' && <Overview status={status} online={online} />}

        {page === 'remote' && (
          <>
            <PageTitle
              title="Remote"
              meta="The five screens as they are now. Tap a button for a short press, hold it for a long one."
              className="mb-6"
            />
            {/* Edge to edge on a phone, so the screens get every pixel of the width. */}
            <Remote
              mode={status?.mode}
              onPress={pressKey}
              notice={notices.remote ?? null}
              className="max-sm:-mx-4 max-sm:rounded-none max-sm:border-x-0 max-sm:px-3"
            />
          </>
        )}

        {onSettings && (
          <SettingsPage
            settings={settings}
            onTestLeds={testLeds}
            onResumeLeds={resumeLeds}
            ledBusy={ledBusy}
            onBeep={beep}
            beeping={beeping}
            notices={{ leds: notices.leds ?? null, sounds: notices.sounds ?? null }}
          />
        )}

        {page === 'device' && (
          <DevicePage
            status={status}
            paired={!!token}
            onToken={paired}
            onForget={forget}
            onInstall={install}
            progress={progress}
            onReboot={reboot}
            rebooting={rebooting}
            notices={{ firmware: notices.firmware ?? null, reboot: notices.reboot ?? null }}
          />
        )}

        <p className="mt-12 text-center text-xs text-ink-3">
          Pentadeck{status?.fw ? ` ${status.fw}` : ''} · open source under the MIT license
        </p>
      </main>

      {/* Only on its page; elsewhere the dot on the Settings tab is the reminder. */}
      {onSettings && (
        <SaveBar dirty={settings.dirty} saving={settings.saving} status={settings.saveStatus} onUndo={settings.undo} onSave={settings.save} />
      )}
      <TabBar page={page} dirty={settings.dirty} />

      {asking && (
        <PairDialog
          refused={asking.refused}
          retry={asking.retry}
          onToken={paired}
          onClose={() => setAsking(null)}
          onRetry={() => { const r = asking.retry; setAsking(null); r?.run() }}
        />
      )}
    </>
  )
}
