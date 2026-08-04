import { defineLib } from '../utils/lib'

export const libsodium = defineLib({
  name: 'libsodium',
  cacheTag: '1',

  // v1.0.20:
  url: 'https://github.com/jedisct1/libsodium.git',
  hash: '9511c982fb1d046470a8b42aa36556cdb7da15de',

  build: async (build, platform, prefixPath) => {
    build.exportEnv({ ...platform.tools })
    if (platform.type === 'ios') build.exportEnv({ ...platform.sdkFlags })

    // Keep -O2 here (this is the crypto hot path), but add section flags
    // so the final --gc-sections link can drop unused primitives:
    const sizeFlags = '-O2 -g -ffunction-sections -fdata-sections'
    build.exportEnv({
      CFLAGS:
        platform.type === 'ios'
          ? `${platform.sdkFlags.CFLAGS} ${sizeFlags}`
          : sizeFlags
    })

    await build.exec('./configure', [
      '--enable-static',
      '--disable-shared',
      `--host=${platform.triple}`,
      `--prefix=${prefixPath}`
    ])
    await build.exec('make', [])
    await build.exec('make', ['install'])
  }
})
