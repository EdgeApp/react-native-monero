import { existsSync, mkdirSync } from 'fs'
import { homedir } from 'os'
import { join, resolve } from 'path'

import { CppBridge, type NativeMoneroLwsfModule } from './CppBridge'

const defaultDocumentDirectory = join(homedir(), '.monero-lwsf')

/**
 * The minimal Node binding shape needed to drive the shared C++ bridge.
 */
export interface NodeMoneroLwsfBinding {
  readonly callMonero: NativeMoneroLwsfModule['callMonero']
  readonly methodNames?: readonly string[]
}

interface SyncNodeMoneroLwsfBinding {
  readonly callMoneroSync: (name: string, jsonArguments: string[]) => string
  readonly methodNames?: readonly string[]
}

export interface MakeMoneroOptions {
  /**
   * Persistent directory for wallet files and signed transaction temp files.
   * Defaults to `~/.monero-lwsf`.
   */
  readonly documentDirectory?: string
}

function isAsyncBinding(value: unknown): value is NodeMoneroLwsfBinding {
  return (
    typeof value === 'object' &&
    value != null &&
    typeof (value as NodeMoneroLwsfBinding).callMonero === 'function'
  )
}

function isSyncBinding(value: unknown): value is SyncNodeMoneroLwsfBinding {
  return (
    typeof value === 'object' &&
    value != null &&
    typeof (value as SyncNodeMoneroLwsfBinding).callMoneroSync === 'function'
  )
}

function loadNodeModule(modulePath: string): unknown {
  const loaded = require(modulePath) as { default?: unknown }
  return loaded.default ?? loaded
}

function wrapSyncBinding(
  binding: SyncNodeMoneroLwsfBinding
): NodeMoneroLwsfBinding {
  let queue = Promise.resolve()

  return {
    methodNames: binding.methodNames,
    async callMonero(name, jsonArguments) {
      const next = queue.then(async () =>
        binding.callMoneroSync(name, jsonArguments)
      )

      queue = next.then(
        async () => undefined,
        async () => undefined
      )

      return await next
    }
  }
}

export function loadNodeBinding(modulePath?: string): NodeMoneroLwsfBinding {
  if (modulePath != null) {
    const loaded = loadNodeModule(resolve(modulePath))
    if (isAsyncBinding(loaded)) return loaded
    if (isSyncBinding(loaded)) return wrapSyncBinding(loaded)
    throw new Error(
      'Node binding must export callMonero(name, args) or callMoneroSync(name, args)'
    )
  }

  const defaultCandidates = [
    resolve(__dirname, '../build/Release/monero_lwsf_node.node'),
    resolve(__dirname, '../../build/Release/monero_lwsf_node.node')
  ]

  for (const candidate of defaultCandidates) {
    if (!existsSync(candidate)) continue
    const loaded = loadNodeModule(candidate)
    if (isSyncBinding(loaded)) return wrapSyncBinding(loaded)
    if (isAsyncBinding(loaded)) return loaded
  }

  throw new Error(
    'Node addon not found. Run `npm run build-node` or provide MONERO_LWSF_NODE_BINDING.'
  )
}

export function makeMoneroModule(
  binding: NodeMoneroLwsfBinding,
  options: MakeMoneroOptions = {}
): NativeMoneroLwsfModule {
  const documentDirectory = resolve(
    options.documentDirectory ?? defaultDocumentDirectory
  )
  mkdirSync(documentDirectory, { recursive: true })

  return {
    callMonero: binding.callMonero.bind(binding),
    documentDirectory,
    methodNames: binding.methodNames == null ? [] : [...binding.methodNames]
  }
}

/**
 * Assemble the Node binding into the same high-level API as the React Native entrypoint.
 */
export function makeMonero(
  binding?: NodeMoneroLwsfBinding,
  options: MakeMoneroOptions = {}
): CppBridge {
  return new CppBridge(makeMoneroModule(binding ?? loadNodeBinding(), options))
}

export type { CppBridge, NativeMoneroLwsfModule }
export * from './types'
