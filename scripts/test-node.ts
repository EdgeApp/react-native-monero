import { resolve } from 'path'

import { loadNodeBinding, makeMonero } from '../src/node'

async function main(): Promise<void> {
  const bindingPath = process.argv[2] ?? process.env.MONERO_LWSF_NODE_BINDING

  const monero =
    bindingPath == null
      ? makeMonero(undefined, {
          documentDirectory: process.env.MONERO_LWSF_DOCUMENT_DIRECTORY
        })
      : makeMonero(loadNodeBinding(resolve(bindingPath)), {
          documentDirectory: process.env.MONERO_LWSF_DOCUMENT_DIRECTORY
        })

  const wallet = await monero.generateWallet('MAINNET')
  console.log(JSON.stringify(wallet, null, 2))
}

main().catch((error: unknown) => {
  console.error(String(error))
  process.exit(1)
})
