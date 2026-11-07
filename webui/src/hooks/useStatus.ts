import { useEffect, useState } from 'react'
import { getJson, type DeviceStatus } from '../lib/api'

const EVERY_MS = 2000

/**
 * GET /api/status every two seconds. The last good reading is kept while the device does not answer
 * (`online` false), so the page goes on showing what it last knew rather than going blank. Paused
 * while the page is hidden - a phone in a pocket has no one to show it to.
 */
export function useStatus(): { status: DeviceStatus | null; online: boolean | null } {
  const [status, setStatus] = useState<DeviceStatus | null>(null)
  const [online, setOnline] = useState<boolean | null>(null)

  useEffect(() => {
    let stopped = false
    let timer: ReturnType<typeof setTimeout> | undefined
    let inFlight = false
    const tick = async () => {
      clearTimeout(timer)
      if (inFlight) return
      if (document.hidden) { timer = setTimeout(tick, EVERY_MS); return }
      inFlight = true
      try {
        const s = await getJson<DeviceStatus>('/api/status', 4000)
        if (stopped) return
        setStatus(s)
        setOnline(true)
      } catch {
        if (!stopped) setOnline(false)
      }
      inFlight = false
      if (!stopped) timer = setTimeout(tick, EVERY_MS)
    }
    const onVisible = () => { if (!document.hidden) tick() }
    tick()
    document.addEventListener('visibilitychange', onVisible)
    return () => { stopped = true; clearTimeout(timer); document.removeEventListener('visibilitychange', onVisible) }
  }, [])

  return { status, online }
}
