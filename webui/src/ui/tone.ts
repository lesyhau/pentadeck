import { AlertIcon, CheckCircleIcon, InfoIcon } from './icons'

/** What a message is: news, a done thing, something to look at, or something that went wrong. */
export type Tone = 'info' | 'success' | 'warning' | 'danger'

/** Done things are teal, like everything that is fine on the device; there is no green. */
export const TONE_TEXT: Record<Tone, string> = {
  info: 'text-ink-2',
  success: 'text-accent-ink',
  warning: 'text-warn-ink',
  danger: 'text-danger-ink',
}

export const TONE_ICON = { info: InfoIcon, success: CheckCircleIcon, warning: AlertIcon, danger: AlertIcon } as const
