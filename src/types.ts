export enum NetworkType {
  MAINNET = 0,
  TESTNET = 1,
  STAGENET = 2
}

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
