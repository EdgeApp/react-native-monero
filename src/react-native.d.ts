declare module 'react-native' {
  import type { NativeMoneroLwsfModule } from 'react-native-monero'
  declare const NativeModules: {
    MoneroLwsfModule: NativeMoneroLwsfModule
  }
}
