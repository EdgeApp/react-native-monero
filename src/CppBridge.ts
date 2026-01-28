'use strict'

import type {
  DerivedKeys,
  GeneratedWallet,
  NetworkType,
  WalletBackend
} from './types'
import { networkTypeToIntString } from './types'

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

export class CppBridge {
  private readonly module: NativeMoneroLwsfModule

  constructor(moneroLwsfModule: NativeMoneroLwsfModule) {
    this.module = moneroLwsfModule
  }

  /**
   * Generate a new wallet's keys in memory (no disk I/O).
   * @param nettype - Network type (0=mainnet, 1=testnet, 2=stagenet)
   * @param language - Mnemonic language (e.g., "English")
   * @returns Generated wallet with mnemonic and spend keys
   */
  async generateWallet(
    nettype: NetworkType,
    language: string = 'English'
  ): Promise<GeneratedWallet> {
    const response = await this.module.callMonero('generateWallet', [
      networkTypeToIntString(nettype),
      language
    ])
    return JSON.parse(response) as GeneratedWallet
  }

  /**
   * Derive all keys from a mnemonic (no disk I/O).
   * @param mnemonic - The 25-word mnemonic seed
   * @param nettype - Network type (0=mainnet, 1=testnet, 2=stagenet)
   * @returns All four keys (view and spend, public and secret)
   */
  async seedAndKeysFromMnemonic(
    mnemonic: string,
    nettype: NetworkType
  ): Promise<DerivedKeys> {
    const response = await this.module.callMonero('seedAndKeysFromMnemonic', [
      mnemonic,
      networkTypeToIntString(nettype)
    ])
    return JSON.parse(response) as DerivedKeys
  }

  /**
   * Get the current network blockchain height from a daemon.
   * @param backend - Backend type ('lwsf' or 'monerod')
   * @param nettype - Network type (0=mainnet, 1=testnet, 2=stagenet)
   * @param daemonAddress - Daemon address to query
   * @returns Current blockchain height
   */
  async getNetworkBlockHeight(
    backend: WalletBackend,
    nettype: NetworkType,
    daemonAddress: string
  ): Promise<number> {
    const response = await this.module.callMonero('getNetworkBlockHeight', [
      backend,
      networkTypeToIntString(nettype),
      daemonAddress
    ])
    return parseInt(response, 10)
  }
}
