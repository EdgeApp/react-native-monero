// Smoke-test the Node addon: hello + generateWallet + isValidAddress.
// Run after `npm run build-native-host`:
//   node -r sucrase/register ./scripts/smoke-node.ts

import { mkdtempSync } from 'fs'
import { tmpdir } from 'os'
import { join } from 'path'

import { CppBridge } from '../src/CppBridge'
import { makeNodeMoneroModule } from '../src/node'

async function main(): Promise<void> {
  const documentDirectory = mkdtempSync(join(tmpdir(), 'monero-node-'))
  const module = makeNodeMoneroModule({ documentDirectory })
  const bridge = new CppBridge(module)

  const hello = await module.callMonero('hello', [])
  console.log('hello:', hello)

  const generated = await bridge.generateWallet('MAINNET')
  console.log('mnemonic words:', generated.mnemonic.split(' ').length)
  console.log('publicSpendKey:', generated.publicSpendKey)

  const keys = await bridge.seedAndKeysFromMnemonic(
    generated.mnemonic,
    'MAINNET'
  )
  const valid = await bridge.isValidAddress(keys.address, 'MAINNET')
  console.log('address:', keys.address)
  console.log('isValidAddress:', valid)
  process.exit(valid ? 0 : 1)
}

main().catch((error: unknown) => {
  console.error(error)
  process.exit(1)
})
