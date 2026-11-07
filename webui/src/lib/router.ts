import { useEffect, useState } from 'react'

/**
 * THE PAGES, one per hash: #/, #/remote, #/settings, #/device. The hash keeps every page an
 * entry of the browser's history (back and forward work) and survives a reload, and the device
 * only ever serves the one file at /.
 */
export type Page = 'overview' | 'remote' | 'settings' | 'device'

export const PAGES: { page: Page; hash: string; label: string }[] = [
  { page: 'overview', hash: '#/', label: 'Overview' },
  { page: 'remote', hash: '#/remote', label: 'Remote' },
  { page: 'settings', hash: '#/settings', label: 'Settings' },
  { page: 'device', hash: '#/device', label: 'Device' },
]

const known = (hash: string) => PAGES.find(p => p.hash === hash)
const current = (): Page => known(location.hash)?.page ?? 'overview'

/** The page the address names. Anything else is the overview. */
export function usePage(): Page {
  const [page, setPage] = useState(current)

  useEffect(() => {
    // An address that names no page (an old bookmark, say): show it as the overview it is.
    const tidy = () => { if (location.hash && location.hash !== '#' && !known(location.hash)) history.replaceState(null, '', '#/') }
    const onChange = () => {
      tidy()
      setPage(current())
      // A new page starts at its top, as a page load would.
      window.scrollTo(0, 0)
    }
    tidy()
    window.addEventListener('hashchange', onChange)
    return () => window.removeEventListener('hashchange', onChange)
  }, [])

  useEffect(() => {
    document.title = `${PAGES.find(p => p.page === page)!.label} · Pentadeck`
  }, [page])

  return page
}
