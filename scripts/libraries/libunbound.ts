import { join } from 'path'

import { defineLib } from '../utils/lib'

export const libunbound = defineLib({
  name: 'libunbound',
  libDeps: ['libexpat', 'openssl'],
  cacheTag: '1',

  // 1.24.1 (upstream wants 1.4.16)
  url: 'https://github.com/NLnetLabs/unbound.git',
  hash: 'a33f0638e1dacf2633cf2292078a674576bca852',

  build: async (build, platform, prefixPath) => {
    build.log(JSON.stringify(platform.tools, null, 1))
    build.exportEnv({
      ...platform.tools,
      PKG_CONFIG_PATH: join(prefixPath, 'lib/pkgconfig')
    })
    if (platform.type === 'ios') build.exportEnv({ ...platform.sdkFlags })

    // Without an explicit CFLAGS, autoconf defaults to "-g -O2". DNS
    // resolution (OpenAlias) is a cold path, so optimize for size, and
    // add section flags so the final --gc-sections link can drop the
    // resolver features the wallet never uses:
    const sizeFlags = '-Oz -g -ffunction-sections -fdata-sections'
    build.exportEnv({
      CFLAGS:
        platform.type === 'ios'
          ? `${platform.sdkFlags.CFLAGS} ${sizeFlags}`
          : sizeFlags
    })

    await build.exec('./configure', [
      '--enable-static',
      '--disable-shared',
      // GOST verification needs the OpenSSL ENGINE API, which our trimmed
      // OpenSSL build removes (no-engine no-gost). No resolver we query
      // serves GOST-signed records anyway:
      '--disable-gost',
      `--host=${platform.triple}`,
      `--prefix=${prefixPath}`,
      `--with-ssl=${prefixPath}`,
      `--with-libexpat=${prefixPath}`
    ])
    await build.exec('make', [])
    await build.exec('make', ['install'])
  }
})
