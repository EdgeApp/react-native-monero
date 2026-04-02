const { readdirSync } = require('fs')
const { join } = require('path')

const prefixPath = process.env.MONERO_LWSF_PREFIX_PATH
const lwsfCmakePath = process.env.MONERO_LWSF_LWSF_CMAKE_PATH

if (prefixPath == null || lwsfCmakePath == null) {
  throw new Error(
    'MONERO_LWSF_PREFIX_PATH and MONERO_LWSF_LWSF_CMAKE_PATH must be set'
  )
}

const prefixLibraries = [
  'libboost_chrono.a',
  'libboost_filesystem.a',
  'libboost_program_options.a',
  'libboost_serialization.a',
  'libboost_thread.a',
  'libcrypto.a',
  'libsodium.a',
  'libssl.a',
  'libunbound.a'
].map(name => join(prefixPath, 'lib', name))

function listStaticLibraries(path) {
  const out = []
  for (const entry of readdirSync(path, { withFileTypes: true })) {
    const entryPath = join(path, entry.name)
    if (entry.isDirectory()) {
      out.push(...listStaticLibraries(entryPath))
      continue
    }
    if (entry.isFile() && entry.name.endsWith('.a')) {
      out.push(entryPath)
    }
  }
  return out
}

const lwsfLibraries = listStaticLibraries(lwsfCmakePath).sort()
const linkerFlags = [
  ...prefixLibraries,
  ...lwsfLibraries,
  '-liconv',
  '-lsqlite3',
  '-lz'
]

process.stdout.write(linkerFlags.join(' '))
