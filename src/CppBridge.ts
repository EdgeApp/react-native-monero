'use strict'

/**
 * The shape of the native C++ module exposed to React Native.
 *
 * You do not normally need this, but it is accessible as
 * `require('react-native').NativeModules.MoneroLwsfModule`.
 *
 * Pass this object to the `CppBridge` constructor to re-assemble the API.
 */
export interface NativeMoneroLwsfModule {
  readonly callMonero: (
    name: string,
    jsonArguments: string[]
  ) => Promise<string>

  readonly methodNames: string[]
  readonly documentDirectory: string
}
