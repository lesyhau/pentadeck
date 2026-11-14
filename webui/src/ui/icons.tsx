import type { ReactNode } from 'react'

export interface IconProps {
  size?: number
  className?: string
}

/** A 24-unit line icon in currentColor, hidden from assistive tech (the control names itself). */
function icon(paths: ReactNode) {
  return function Icon({ size = 18, className }: IconProps) {
    return (
      <svg
        width={size}
        height={size}
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth={1.75}
        strokeLinecap="round"
        strokeLinejoin="round"
        aria-hidden="true"
        focusable="false"
        className={className}
      >
        {paths}
      </svg>
    )
  }
}

export const SunIcon = icon(<><circle cx="12" cy="12" r="4" /><path d="M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4" /></>)
export const MoonIcon = icon(<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z" />)
export const MonitorIcon = icon(<><rect x="3" y="4" width="18" height="12" rx="1.5" /><path d="M8.5 20h7M12 16v4" /></>)
export const CheckIcon = icon(<path d="M5 12.5l4.5 4.5L19 7.5" />)
export const CloseIcon = icon(<path d="M6.5 6.5l11 11M17.5 6.5l-11 11" />)
export const ChevronDownIcon = icon(<path d="M6.5 9.5L12 15l5.5-5.5" />)
export const UploadIcon = icon(<path d="M12 15V4.5M7.5 9L12 4.5 16.5 9M4.5 14.5v4a1.5 1.5 0 0 0 1.5 1.5h12a1.5 1.5 0 0 0 1.5-1.5v-4" />)
export const RestartIcon = icon(<><path d="M19.5 12a7.5 7.5 0 1 1-2.4-5.5" /><path d="M19.5 4v4h-4" /></>)
export const AlertIcon = icon(<><path d="M10.6 4.3L2.9 17.8A1.6 1.6 0 0 0 4.3 20h15.4a1.6 1.6 0 0 0 1.4-2.2L13.4 4.3a1.6 1.6 0 0 0-2.8 0z" /><path d="M12 9.5v4M12 16.8v.01" /></>)
export const InfoIcon = icon(<><circle cx="12" cy="12" r="8.5" /><path d="M12 11v5M12 7.8v.01" /></>)
export const LinkIcon = icon(<><path d="M10.5 13.5a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1.2 1.2" /><path d="M13.5 10.5a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1.2-1.2" /></>)
export const CheckCircleIcon = icon(<><circle cx="12" cy="12" r="8.5" /><path d="M8.3 12.3l2.6 2.6 4.9-5.4" /></>)

// The device's five buttons, for the remote.
export const BulbIcon = icon(<><path d="M10 20.5h4" /><path d="M8.6 14.5a6 6 0 1 1 6.8 0c-.6.4-.9 1-.9 1.7v1.3h-5v-1.3c0-.7-.3-1.3-.9-1.7z" /></>)
export const PowerIcon = icon(<><path d="M12 3.5v8" /><path d="M7.2 6.6a7.5 7.5 0 1 0 9.6 0" /></>)
export const TriangleLeftIcon = icon(<path d="M15.5 6v12L7 12z" fill="currentColor" />)
export const TriangleRightIcon = icon(<path d="M8.5 6v12L17 12z" fill="currentColor" />)
export const ModeIcon = icon(<><path d="M12 3.5l8.5 4.5L12 12.5 3.5 8z" /><path d="M3.5 12l8.5 4.5 8.5-4.5" /><path d="M3.5 16l8.5 4.5 8.5-4.5" /></>)

// The four pages, for the tabs.
export const GaugeIcon = icon(<><path d="M3.8 17.5a8.5 8.5 0 1 1 16.4 0" /><path d="M12 15l3.6-5" /><circle cx="12" cy="15.5" r="1.4" /></>)
export const ScreensIcon = icon(<><rect x="2.5" y="6" width="5" height="5" rx="1" /><rect x="9.5" y="6" width="5" height="5" rx="1" /><rect x="16.5" y="6" width="5" height="5" rx="1" /><path d="M3.5 16h3M10.5 16h3M17.5 16h3" /></>)
export const SlidersIcon = icon(<><path d="M4 7h8.5M17.5 7H20M4 17h2.5M11.5 17H20" /><circle cx="15" cy="7" r="2.5" /><circle cx="9" cy="17" r="2.5" /></>)
export const ChipIcon = icon(<><rect x="6.5" y="6.5" width="11" height="11" rx="1.5" /><path d="M9.5 3.5v3M14.5 3.5v3M9.5 17.5v3M14.5 17.5v3M3.5 9.5h3M3.5 14.5h3M17.5 9.5h3M17.5 14.5h3" /></>)

// Sound, the clock image, backups, notifications.
export const SpeakerIcon = icon(<><path d="M4 9.5h3.5L12 5.5v13l-4.5-4H4z" /><path d="M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11" /></>)
export const StopIcon = icon(<rect x="7" y="7" width="10" height="10" rx="1.5" />)
export const ImageIcon = icon(<><rect x="3.5" y="4.5" width="17" height="15" rx="1.5" /><circle cx="9" cy="10" r="1.8" /><path d="M20.5 16l-5-5-8.5 8.5" /></>)
export const DownloadIcon = icon(<path d="M12 4.5V15M7.5 10.5L12 15l4.5-4.5M4.5 14.5v4a1.5 1.5 0 0 0 1.5 1.5h12a1.5 1.5 0 0 0 1.5-1.5v-4" />)
export const BellIcon = icon(<><path d="M6.5 16.5V11a5.5 5.5 0 0 1 11 0v5.5l1.5 2h-14z" /><path d="M10 20.5h4" /></>)
