export enum NetworkType {
  MAINNET = 0,
  TESTNET = 1,
  STAGENET = 2
}

export type WalletBackend = 'lws' | 'monerod'

export interface GeneratedWallet {
  mnemonic: string
  secretSpendKey: string
  publicSpendKey: string
}

// Return type for seedAndKeysFromMnemonic
export interface DerivedKeys {
  address: string
  secretViewKey: string
  publicViewKey: string
  secretSpendKey: string
  publicSpendKey: string
}

// Return type for openWallet and getWalletStatus
export interface WalletStatus {
  syncedHeight: number
  networkHeight: number
  balance: number
  unlockedBalance: number
}
