import { EventEmitter } from 'events'

import type { NativeMoneroLwsfModule } from './CppBridge'
import { loadNativeAddon } from './load-addon'

export interface MakeNodeMoneroModuleOpts {
  documentDirectory: string
}

export type NodeMoneroModule = NativeMoneroLwsfModule & EventEmitter

/**
 * Node N-API implementation of the native Monero module.
 * Same `callMonero` contract as `NativeModules.MoneroLwsfModule`.
 */
export function makeNodeMoneroModule(
  opts: MakeNodeMoneroModuleOpts
): NodeMoneroModule {
  const addon = loadNativeAddon()
  const emitter = new EventEmitter()

  addon.setEventListener((walletId, eventName, data) => {
    emitter.emit('MoneroWalletEvent', { walletId, eventName, data })
  })

  const methodNames = addon.methodNames()

  const module = Object.assign(emitter, {
    callMonero: async (name: string, jsonArguments: string[]) =>
      await addon.callMonero(name, jsonArguments),
    methodNames,
    documentDirectory: opts.documentDirectory
  }) as NodeMoneroModule

  return module
}
