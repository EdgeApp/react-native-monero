// Run this script as `node -r sucrase/register ./scripts/build-native.ts`
//
// It will:
// - Download third-party source code.
// - Assemble Android shared libraries for each platform.
// - Assemble an iOS universal static xcframework.
//
// Here is where each puzzle piece comes from:
//
// | lib        | android           | ios                   |
// |------------|-------------------|-----------------------|
// | Boost      | custom            | custom                |
// | libexpat   | autotools         | autotools             |
// | libsodium  | autotools         | autotools             |
// | libunbound | autotools         | autotools             |
// | libzmq     | CMake             | CMake                 |
// | monero     | CMake             | CMake                 |
// | OpenSSL    | custom            | custom                |
//

import { copyFile, mkdir, rm, writeFile } from 'fs/promises'
import { basename, join } from 'path'

import { boost } from './libraries/boost'
import { libexpat } from './libraries/libexpat'
import { libsodium } from './libraries/libsodium'
import { libunbound } from './libraries/libunbound'
import { libzmq } from './libraries/libzmq'
import { lwsf } from './libraries/lwsf'
import { openssl } from './libraries/openssl'
import { lsr, tmpPath } from './utils/common'
import { defineLib } from './utils/lib'
import {
  makeHostPlatforms,
  makeIosPlatforms,
  makePlatforms
} from './utils/platforms'
import { addTask, startBuild } from './utils/tasks'

const ffi = defineLib({
  name: 'ffi',
  hash: '1',
  cacheTag: undefined, // Run every time
  libDeps: ['lwsf'],

  async build(build, platform, prefixPath) {
    // Source list (from src/):
    const srcPath = join(__dirname, '../src')
    const sources: string[] = [
      'monero-wrapper/monero-methods.cpp',
      'monero-wrapper/nym-fetch.cpp'
    ]

    // LWSF builds a *ton* of libraries,
    // but doesn't install them in the normal place:
    const lwsLibs = []
    const lwsfPath = join(build.basePath, 'build', `lwsf-${platform.name}`)
    for await (const path of lsr(join(lwsfPath, 'cmake'))) {
      if (path.endsWith('.a')) lwsLibs.push(path)
    }

    // Compile flags:
    const includePaths = [
      join(prefixPath, 'include'),
      join(lwsfPath, 'include'),
      join(build.basePath, 'monero/src'),
      join(build.basePath, 'monero/contrib/epee/include'),
      join(build.basePath, 'monero/external/easylogging++')
    ]
    const libPaths = [join(prefixPath, 'lib')]
    const libs = [
      'boost_chrono',
      'boost_filesystem',
      'boost_program_options',
      'boost_serialization',
      'boost_thread',
      'crypto',
      'sodium',
      'ssl',
      'unbound'
    ]

    if (platform.type === 'android') {
      sources.push('jni/jni.cpp')
    }
    if (platform.type === 'ios') {
      sources.push('clear_cache_stub.c')
    }

    // Compile our sources:
    const objects: string[] = []
    for (const source of sources) {
      // Figure out the object file name:
      const object = join(
        build.cwd,
        basename(source).replace(/\.c$|\.cc$|\.cpp$/, '.o')
      )
      objects.push(object)

      const useCxx = /\.cpp$|\.cc$/.test(source)
      const sdkFlags: string =
        platform.type === 'ios'
          ? useCxx
            ? platform.sdkFlags.CXXFLAGS
            : platform.sdkFlags.CFLAGS
          : platform.type === 'host'
            ? `${useCxx ? platform.sdkFlags.CXXFLAGS : platform.sdkFlags.CFLAGS} -fPIC`
            : ''
      await build.exec(useCxx ? platform.tools.CXX : platform.tools.CC, [
        '-c',
        ...(useCxx ? ['-std=c++17'] : []),
        ...sdkFlags.split(' ').filter(flag => flag !== ''),
        ...includePaths.map(path => `-I${path}`),
        `-o${object}`,
        join(srcPath, source)
      ])
    }

    if (platform.type === 'ios' || platform.type === 'host') {
      // Link everything together into a single giant .o file:
      const objectPath = join(build.cwd, 'monero-module.o')
      const linkFlags =
        platform.type === 'host'
          ? [
              ...platform.sdkFlags.LDFLAGS.split(' ').filter(
                flag => flag !== ''
              ),
              // Relocatable link: do not pull the C++ runtime into the .o.
              // node-gyp links -lc++ when producing monero.node.
              '-nostdlib'
            ]
          : []
      await build.exec(platform.tools.LD, [
        '-r',
        ...linkFlags,
        '-o',
        objectPath,
        ...libPaths.map(path => `-L${path}`),
        ...libs.map(lib => `-l${lib}`),
        ...lwsLibs,
        ...objects
      ])

      // Localize all symbols except the ones we really want,
      // hiding them from future linking steps:
      if (platform.type === 'ios') {
        await build.exec(platform.tools.OBJCOPY, [
          objectPath,
          '-w',
          '-L*',
          '-L!_moneroMethods',
          '-L!_moneroMethodCount',
          '-L!*moneroSetEventCallback*'
        ])
      }

      // Generate a static library:
      const library = join(build.cwd, `monero-module.a`)
      await rm(library, { force: true })
      await build.exec(platform.tools.AR, ['rcs', library, objectPath])
    } else {
      // Link everything together into a shared library:
      const outPath = join(
        srcPath,
        '../android/src/main/jniLibs/',
        platform.arch
      )
      await mkdir(outPath, { recursive: true })
      await build.exec(platform.tools.LD, [
        '-shared',
        `-o${join(outPath, 'librnmonero.so')}`,
        ...libPaths.map(path => `-L${path}`),
        ...libs.map(lib => `-l${lib}`),
        ...objects,

        '-Wl,--start-group',
        ...lwsLibs,
        '-Wl,--end-group',

        '-llog',
        `-Wl,--version-script=${join(srcPath, 'jni/exports.map')}`,
        '-Wl,--no-undefined',
        '-Wl,-z,max-page-size=16384'
      ])
      build.log('done')
    }
  }
})

/**
 * Creates a unified xcframework file out of the per-platform
 * static libraries that `buildIosLwsf` creates.
 */
addTask({
  name: 'xcframework',
  deps: [
    'ffi.build.iphoneos-arm64',
    'ffi.build.iphonesimulator-arm64',
    'ffi.build.iphonesimulator-x86_64'
  ],
  async run(build) {
    const platforms = await makeIosPlatforms()
    const sdks = new Set(platforms.map(row => row.sdk))

    // Merge the platforms into a fat library:
    const merged: string[] = []
    for (const sdk of sdks) {
      build.log(`Merging libraries for ${sdk}...`)
      const sdkDir = join(build.cwd, sdk)
      await mkdir(sdkDir, { recursive: true })
      const output = join(sdkDir, 'libmonero-module.a')

      await build.exec('lipo', [
        '-create',
        '-output',
        output,
        ...platforms
          .filter(platform => platform.sdk === sdk)
          .map(({ sdk, arch }) =>
            join(build.basePath, `build/ffi-${sdk}-${arch}`, `monero-module.a`)
          )
      ])
      merged.push('-library', output)
    }

    // Bundle those into an XCFramework:
    build.log('Creating XCFramework...')
    await rm('ios/MoneroModule.xcframework', { recursive: true, force: true })
    await build.exec('xcodebuild', [
      '-create-xcframework',
      ...merged,
      '-output',
      join(__dirname, '../ios/MoneroModule.xcframework')
    ])
  }
})

addTask({
  name: 'default',
  cacheTag: 'default',
  deps: [
    'ffi.build.android-arm64-v8a',
    'ffi.build.android-armeabi-v7a',
    'xcframework'
  ],
  async run() {}
})

async function main(): Promise<void> {
  await mkdir(tmpPath, { recursive: true })

  const target = process.argv[2] ?? 'default'
  const hostOnly = target === 'host' || target === 'nodeaddon'

  const platforms = hostOnly ? await makeHostPlatforms() : await makePlatforms()

  boost(platforms)
  ffi(platforms)
  libexpat(platforms)
  libsodium(platforms)
  libunbound(platforms)
  libzmq(platforms)
  lwsf(platforms)
  openssl(platforms)

  if (hostOnly) {
    const hostName = platforms[0].name
    addTask({
      name: 'nodeaddon',
      cacheTag: undefined,
      deps: [`ffi.build.${hostName}`],
      async run(build) {
        const ffiPath = join(build.basePath, 'build', `ffi-${hostName}`)
        const staticLib = join(ffiPath, 'monero-module.a')
        const prebuildDir = join(
          __dirname,
          '../prebuilds',
          `${process.platform}-${process.arch}`
        )
        await mkdir(prebuildDir, { recursive: true })

        const napiInclude = String(
          require(join(__dirname, '../node_modules/node-addon-api')).include
        ).replace(/"/g, '')

        const gypDir = join(build.basePath, 'nodeaddon')
        await mkdir(gypDir, { recursive: true })
        // node-gyp/make cannot compile sources given as absolute paths.
        const napiSrc = join(__dirname, '../src/node/monero-napi.cpp')
        await copyFile(napiSrc, join(gypDir, 'monero-napi.cpp'))
        const gypPath = join(gypDir, 'binding.gyp')
        const gyp = {
          targets: [
            {
              target_name: 'monero',
              sources: ['monero-napi.cpp'],
              include_dirs: [
                napiInclude,
                join(__dirname, '../src/monero-wrapper')
              ],
              defines: ['NAPI_CPP_EXCEPTIONS'],
              'cflags!': ['-fno-exceptions'],
              'cflags_cc!': ['-fno-exceptions'],
              cflags_cc: ['-std=c++17', '-fPIC'],
              libraries: [staticLib],
              xcode_settings: {
                GCC_ENABLE_CPP_EXCEPTIONS: 'YES',
                CLANG_CXX_LANGUAGE_STANDARD: 'c++17',
                MACOSX_DEPLOYMENT_TARGET: '11.0',
                OTHER_LDFLAGS: ['-lc++', '-lz']
              }
            }
          ]
        }
        await writeFile(gypPath, JSON.stringify(gyp, null, 2))

        const nodeGyp = join(__dirname, '../node_modules/.bin/node-gyp')
        await build.exec(nodeGyp, ['rebuild'], { cwd: gypDir })

        const built = join(gypDir, 'build/Release/monero.node')
        await build.exec('cp', [built, join(prebuildDir, 'monero.node')])
        build.log(`Wrote ${join(prebuildDir, 'monero.node')}`)
      }
    })
  }

  await startBuild(hostOnly ? 'nodeaddon' : target, { basePath: tmpPath })
}

main().catch((error: unknown) => {
  console.log(String(error))
  process.exit(1)
})
