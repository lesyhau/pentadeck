import { useCallback, useEffect, useState } from 'react'
import type { SaveStatus } from './ui'
import { getJson, needsToken, post, type DeviceSettings } from './api'
import { sameDraft, toDraft, toPayload, validate, type Draft } from './form'
import type { Send } from './Pairing'

/**
 * THE SETTINGS, as the device holds them (base) and as they are being edited (draft). Held by the
 * App, above the pages, so leaving Settings and coming back keeps what was typed.
 */
export function useSettings(send: Send) {
  const [meta, setMeta] = useState<Pick<DeviceSettings, 'led_modes' | 'panels'>>({})
  const [base, setBase] = useState<Draft | null>(null)
  const [draft, setDraft] = useState<Draft | null>(null)
  const [loadFailed, setLoadFailed] = useState(false)
  const [saving, setSaving] = useState(false)
  const [saveStatus, setSaveStatus] = useState<SaveStatus | null>(null)
  const [showErrors, setShowErrors] = useState(false)

  const fill = useCallback((s: DeviceSettings) => {
    const d = toDraft(s)
    setMeta({ led_modes: s.led_modes, panels: s.panels })
    setBase(d)
    setDraft(d)
    setShowErrors(false)
  }, [])

  const load = useCallback(async () => {
    setLoadFailed(false)
    try { fill(await getJson<DeviceSettings>('/api/settings')) } catch { setLoadFailed(true) }
  }, [fill])
  useEffect(() => { load() }, [load])

  const set = useCallback(<K extends keyof Draft>(key: K, value: Draft[K]) => {
    setDraft(d => (d ? { ...d, [key]: value } : d))
    setSaveStatus(s => (s?.tone === 'danger' ? null : s))
  }, [])

  const dirty = !!draft && !!base && !sameDraft(draft, base)
  const errors = draft && showErrors ? validate(draft) : {}

  const save = async () => {
    if (!draft) return
    if (Object.keys(validate(draft)).length > 0) {
      setShowErrors(true)
      setSaveStatus({ tone: 'danger', text: 'Check the fields marked in red.' })
      return
    }
    setSaving(true)
    setSaveStatus(null)
    const r = await send(t => post('/api/settings', t, toPayload(draft)), { label: 'Save now', run: save })
    setSaving(false)
    if (r.ok) {
      try { fill(JSON.parse(r.text) as DeviceSettings) } catch { load() }
      setSaveStatus({ tone: 'success', text: 'Saved' })
      setTimeout(() => setSaveStatus(s => (s?.text === 'Saved' ? null : s)), 1600)
    } else {
      setSaveStatus({ tone: 'danger', text: needsToken(r.status) ? 'Pair this browser, then Save.' : r.text || `The device answered ${r.status}.` })
    }
  }
  const undo = () => {
    if (base) setDraft(base)
    setShowErrors(false)
    setSaveStatus(null)
  }

  return { meta, draft, errors, dirty, set, load, loadFailed, save, saving, saveStatus, undo }
}

export type Settings = ReturnType<typeof useSettings>
