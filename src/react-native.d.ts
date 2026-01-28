declare module 'react-native' {
  import type { NativeMoneroModule } from 'react-native-monero-lwsf'
  declare const NativeModules: {
    MoneroLwsfModule: NativeMoneroModule
    readonly callMonero: (
      method: string,
      arguments: string[]
    ) => Promise<string>
    readonly methodNames: string[]
    readonly documentDirectory: string
  }
}
