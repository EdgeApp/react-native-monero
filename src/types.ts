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

// Transaction direction
export enum TransactionDirection {
  In = 0,
  Out = 1
}

// Single transaction info
export interface TransactionInfo {
  hash: string
  direction: TransactionDirection
  isPending: boolean
  isFailed: boolean
  isCoinbase: boolean
  amount: number
  fee: number
  blockHeight: number
  confirmations: number
  timestamp: number
  paymentId: string
  description: string
  label: string
  unlockTime: number
  subaddrAccount: number
  txKey?: string // Only available for outgoing transactions we sent
}

// Return type for getAllTransactions
export interface TransactionsPage {
  transactions: TransactionInfo[]
  totalCount: number
  page: number
  pageSize: number
}

// Transaction priority levels
export enum TransactionPriority {
  Default = 0,
  Low = 1,
  Medium = 2,
  High = 3
}

// Recipient for createTransaction
export interface Recipient {
  address: string
  amount: string // atomic units as string to handle uint64
}

// Return type for createTransaction
export interface SignedTransaction {
  txid: string
  signedTxHex: string
  fee: string
}

// Parsed Monero URI (parseUri result)
export interface ParsedUri {
  address: string
  paymentId: string
  amount: string // atomic units as string
  txDescription: string
  recipientName: string
  unknownParameters: string[]
}

// Params for encodeUri (make monero: URI)
export interface EncodeUriParams {
  address: string
  paymentId?: string // empty or omit; use integrated address for payment id
  amount: string // atomic units as string
  txDescription?: string
  recipientName?: string
}
