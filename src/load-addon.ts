import { existsSync } from 'fs'
import { join } from 'path'

export interface NativeMoneroAddon {
  callMonero: (method: string, args: string[]) => Promise<string>
  methodNames: () => string[]
  setEventListener: (
    cb: (walletId: string, eventName: string, data: string) => void
  ) => void
}

function candidatePaths(): string[] {
  const here = __dirname
  const platform = `${process.platform}-${process.arch}`
  const out: string[] = []
  let dir = here
  for (let i = 0; i < 6; i++) {
    out.push(join(dir, 'prebuilds', platform, 'monero.node'))
    out.push(join(dir, 'build', 'Release', 'monero.node'))
    const parent = join(dir, '..')
    if (parent === dir) break
    dir = parent
  }
  return out
}

let cached: NativeMoneroAddon | undefined

export function loadNativeAddon(): NativeMoneroAddon {
  if (cached != null) return cached

  const errors: string[] = []
  const missing: string[] = []
  for (const candidate of candidatePaths()) {
    try {
      if (!existsSync(candidate)) {
        missing.push(candidate)
        continue
      }
      // Native addon loaded at runtime when the .node binary exists.

      const mod = require(candidate) as NativeMoneroAddon
      if (typeof mod.callMonero !== 'function') continue
      cached = mod
      return cached
    } catch (error: unknown) {
      const message = error instanceof Error ? error.message : String(error)
      errors.push(`${candidate}: ${message}`)
    }
  }

  throw new Error(
    'react-native-monero Node addon not found. Run `npm run build-native -- host`. ' +
      (errors.length > 0
        ? errors.join('; ')
        : `Looked in: ${missing.join(', ')}`)
  )
}
