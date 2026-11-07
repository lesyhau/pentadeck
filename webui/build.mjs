#!/usr/bin/env node
/**
 * Builds the device page and hands it to the firmware:
 *
 *   node build.mjs            -> dist/index.html (one self-contained file: script, styles and fonts
 *                                inlined, no request to anywhere), and its gzip (level 9) at
 *                                ../firmware/main/web/index.html.gz
 *   node build.mjs --no-gz    -> dist/index.html only
 *
 * No npm step: Vite is called through its JS API from the node_modules beside this file.
 */
import { readFile, writeFile } from 'node:fs/promises'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { gzipSync, constants } from 'node:zlib'
import { build } from 'vite'

const HERE = dirname(fileURLToPath(import.meta.url))
const OUT = resolve(HERE, '../firmware/main/web/index.html.gz')

await build({ root: HERE, logLevel: 'warn', configFile: join(HERE, 'vite.config.ts') })

const html = await readFile(join(HERE, 'dist/index.html'))
// Nothing may be fetched: the device is often the only thing on a network with no internet.
const external = String(html).match(/(?:src|href)=["'](?!data:|#)[^"']+["']/g)
if (external) {
  console.error('The page still references outside files:', external)
  process.exit(1)
}

const kb = n => `${(n / 1024).toFixed(1)} KB`
console.log(`dist/index.html  ${kb(html.length)}`)
if (!process.argv.includes('--no-gz')) {
  const gz = gzipSync(html, { level: constants.Z_BEST_COMPRESSION })
  await writeFile(OUT, gz)
  console.log(`${OUT}  ${kb(gz.length)} (gzip -9)`)
}
