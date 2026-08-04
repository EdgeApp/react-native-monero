import { dirname } from 'path'

import { defineLib } from '../utils/lib'
import type { Platform } from '../utils/platforms'

// Features a TLS client wallet never touches, but which the default build
// links into the shipped binary (QUIC stack, SM2 precompute tables, legacy
// ciphers, CMS/OCSP/TS PKI tooling). Names validated against 3.6.0's
// Configure disablables list.
const sizeConfig = [
  'no-apps',
  'no-aria',
  'no-blake2',
  'no-camellia',
  'no-cast',
  'no-cmp',
  'no-cms',
  'no-comp',
  'no-ct',
  'no-docs',
  'no-dso',
  'no-dtls',
  'no-ec2m',
  'no-engine',
  'no-gost',
  'no-http',
  'no-idea',
  'no-legacy',
  'no-md4',
  'no-mdc2',
  'no-ocsp',
  'no-psk',
  'no-quic',
  'no-rc2',
  'no-rc4',
  'no-rc5',
  'no-rmd160',
  'no-seed',
  'no-sm2',
  'no-sm3',
  'no-sm4',
  'no-srp',
  'no-srtp',
  'no-tests',
  'no-ts',
  'no-whirlpool'
]

export const openssl = defineLib({
  name: 'openssl',
  cacheTag: '1',

  // 3.6.0
  url: 'https://github.com/openssl/openssl.git',
  hash: '7b371d80d959ec9ab4139d09d78e83c090de9779',

  build: async (build, platform, prefixPath) => {
    const extraConfig = []
    if (platform.type === 'android') {
      build.exportEnv({
        ANDROID_NDK_ROOT: platform.ndkPath,
        PATH: `${dirname(platform.tools.CC)}:${build.env.PATH}`
      })
      extraConfig.push(`-D__ANDROID_API__=${platform.api}`)
      if (platform.arch === 'x86_64') extraConfig.push('no-asm')
    }

    // if (platform.type === 'ios') {
    //   build.exportEnv({
    //     CROSS_COMPILE: dirname(platform.tools.CC) + '/',
    //     CROSS_TOP: dirname(dirname(platform.sysroot)),
    //     CROSS_SDK: basename(platform.sysroot)
    //   })
    // }

    if (platform.type === 'ios') {
      // OpenSSL's ios64-xcrun target stopped passing any deployment-target
      // flag, so clang defaults to the SDK version and raises the CPU
      // baseline to apple-a12. That emits ARMv8.3 `ldapr` (and v8.1 LSE)
      // instructions, which crash with SIGILL on A11 and older devices
      // (iPhone X, 8, 7, 6s). Configure forwards `-` args to the compiler:
      extraConfig.push(
        platform.sdk === 'iphoneos'
          ? `-miphoneos-version-min=${platform.version}`
          : `-mios-simulator-version-min=${platform.version}`
      )
    }

    await build.exec('./Configure', [
      getTarget(platform),
      `--prefix=${prefixPath}`,
      'no-async',
      'no-shared',
      ...sizeConfig,
      // Configure forwards dash-prefixed args to the compiler. These come
      // after its own -O3, so -Oz wins. The hot AES/SHA/EC paths use
      // hand-written asm and are unaffected by the C optimization level:
      '-Oz',
      '-ffunction-sections',
      '-fdata-sections',
      ...extraConfig
    ])
    await build.exec('make', [])
    await build.exec('make', ['install'])
  }
})

function getTarget(platform: Platform): string {
  if (platform.type === 'android') {
    switch (platform.arch) {
      case 'arm64-v8a':
        return 'android-arm64'
      case 'armeabi-v7a':
        return 'android-arm'
      case 'x86':
        return 'android-x86'
      case 'x86_64':
        return 'android-x86_64'
    }
  }

  if (platform.type === 'ios') {
    switch (platform.arch) {
      case 'x86_64':
        return 'iossimulator-x86_64-xcrun'
      case 'arm64':
        return platform.sdk === 'iphoneos'
          ? 'ios64-xcrun'
          : 'iossimulator-arm64-xcrun'
    }
  }

  return ''
}
