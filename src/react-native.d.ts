declare module 'react-native' {
  import type { NativeMoneroLwsfModule } from 'react-native-monero-lwsf'
  declare const NativeModules: {
    MoneroLwsfModule: NativeMoneroLwsfModule
  }
}
