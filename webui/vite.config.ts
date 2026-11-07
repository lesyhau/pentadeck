import { defineConfig, type Plugin } from 'vite'
import react from '@vitejs/plugin-react'
import { prePaintScript } from './src/lib/appearance'

/** Where the dev server sends /api, /status, /reboot and /ota: your Pentadeck on the LAN. */
const DEVICE = process.env.PENTADECK_URL || 'http://pentadeck.local'

/**
 * The theme is stamped on <html> by a blocking inline script at the end of <head>, before the
 * body is parsed, so the first paint is already in the right theme.
 */
function prePaint(): Plugin {
  return {
    name: 'pentadeck-pre-paint',
    transformIndexHtml: {
      order: 'pre',
      handler: () => [{ tag: 'script', children: prePaintScript(), injectTo: 'head' }],
    },
  }
}

/**
 * One file: the firmware serves a single gzipped index.html, so the built script and stylesheet
 * are written into the page (fonts are already data: URIs, see assetsInlineLimit), the <link>ed
 * icons become data: URIs too, and their files are dropped from the bundle.
 */
function singleFile(): Plugin {
  return {
    name: 'pentadeck-single-file',
    apply: 'build',
    enforce: 'post',
    generateBundle(_options, bundle) {
      const html = Object.values(bundle).find(f => f.type === 'asset' && f.fileName.endsWith('.html'))
      if (!html || html.type !== 'asset') return
      let page = String(html.source)
      for (const [name, file] of Object.entries(bundle)) {
        if (file.type === 'chunk' && file.isEntry) {
          const code = file.code.replace(/<\/script/gi, '<\\/script')
          page = page.replace(new RegExp(`<script[^>]*src="/${name}"[^>]*></script>`), () => `<script type="module">${code}</script>`)
          delete bundle[name]
        } else if (file.type === 'asset' && name.endsWith('.css')) {
          page = page.replace(new RegExp(`<link[^>]*href="/${name}"[^>]*>`), () => `<style>${String(file.source)}</style>`)
          delete bundle[name]
        } else if (file.type === 'asset' && page.includes(`"/${name}"`)) {
          const mime = name.endsWith('.svg') ? 'image/svg+xml' : 'image/png'
          const data = `data:${mime};base64,${Buffer.from(file.source).toString('base64')}`
          page = page.split(`"/${name}"`).join(`"${data}"`)
          delete bundle[name]
        }
      }
      html.source = page
    },
  }
}

export default defineConfig({
  plugins: [react(), prePaint(), singleFile()],
  build: {
    target: 'es2020',
    assetsInlineLimit: 100_000_000,
    cssCodeSplit: false,
    modulePreload: false,
    reportCompressedSize: false,
    rollupOptions: { output: { inlineDynamicImports: true } },
  },
  server: {
    port: 8098,
    proxy: Object.fromEntries(['/api', '/status', '/reboot', '/ota'].map(p => [p, { target: DEVICE, changeOrigin: true }])),
  },
})
