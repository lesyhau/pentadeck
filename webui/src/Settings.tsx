import { Button, Card, Notice, PageTitle, Skeleton, type Note } from './ui'
import { DataSection, DisplaySection, LedSection, MicSection, SoundsSection, WorldSection } from './DeviceSettingsForm'
import type { Settings } from './useSettings'

const DEFAULT_LED_MODES = ['off', 'glow', 'status', 'music']
const DEFAULT_PANELS = ['lab', 'cpu', 'nav', 'gpu', 'audio', 'off']

/** THE SETTINGS PAGE: the cards over the draft the App keeps; the save bar is the App's too. */
export function SettingsPage({ settings, onTestLeds, onResumeLeds, ledBusy, onBeep, beeping, notices }: {
  settings: Settings
  onTestLeds: () => void
  onResumeLeds: () => void
  ledBusy: 'test' | 'resume' | null
  onBeep: (pattern: number) => void
  beeping: boolean
  notices: { leds: Note | null; sounds: Note | null }
}) {
  const { draft, errors, set, meta, loadFailed, load } = settings
  const form = draft && { draft, errors, set, panels: meta.panels ?? DEFAULT_PANELS, ledModes: meta.led_modes ?? DEFAULT_LED_MODES }

  return (
    <>
      <PageTitle title="Settings" meta="Nothing reaches the device until you press Save." className="mb-6" />

      {loadFailed ? (
        <Notice tone="danger" title="Could not load the settings" action={<Button variant="secondary" onClick={load}>Try again</Button>}>
          The device did not answer.
        </Notice>
      ) : !form ? (
        <div className="grid items-start gap-4 sm:gap-6 lg:grid-cols-2" aria-busy="true">
          {['Display', 'Sounds & alarm'].map(title => (
            <Card key={title} title={title}>
              <Skeleton className="h-11 w-full" />
              <Skeleton className="h-11 w-3/4" />
              <Skeleton className="h-11 w-full" />
            </Card>
          ))}
        </div>
      ) : (
        <div className="grid items-start gap-4 sm:gap-6 lg:grid-cols-2">
          <div className="space-y-4 sm:space-y-6">
            <DisplaySection {...form} />
            <LedSection {...form} onTest={onTestLeds} onResume={onResumeLeds} busy={ledBusy} notice={notices.leds} />
            <MicSection {...form} />
          </div>
          <div className="space-y-4 sm:space-y-6">
            <SoundsSection {...form} onBeep={onBeep} busy={beeping} notice={notices.sounds} />
            <WorldSection {...form} />
            <DataSection {...form} />
          </div>
        </div>
      )}
    </>
  )
}
