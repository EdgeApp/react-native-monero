import { NativeModules } from 'react-native'

import { CppBridge, type NativeMoneroLwsfModule } from './CppBridge'

export function makeMonero(): CppBridge {
  const { MoneroLwsfModule } = NativeModules
  if (MoneroLwsfModule == null) {
    throw new Error('react-native-monero-lwsf native module not linked')
  }
  return new CppBridge(MoneroLwsfModule)
}

export type { CppBridge, NativeMoneroLwsfModule }
export * from './types'
