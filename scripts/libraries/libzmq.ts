import { join } from 'path'

import { defineLib } from '../utils/lib'

export const libzmq = defineLib({
  name: 'libzmq',
  cacheTag: '1',

  // 4.3.5 (upstream wants 4.2.0)
  url: 'https://github.com/zeromq/libzmq.git',
  hash: '622fc6dde99ee172ebaa9c8628d85a7a1995a21d',

  build: async (build, platform, prefixPath) => {
    build.exportEnv({ ...platform.tools })

    build.exportEnv({
      ...platform.tools,
      PKG_CONFIG_PATH: join(prefixPath, 'lib/pkgconfig')
    })
    if (platform.type === 'ios' || platform.type === 'host') {
      build.exportEnv({ ...platform.sdkFlags })
    }
    if (platform.type === 'host' && platform.os === 'darwin') {
      build.exportEnv({ SDKROOT: platform.sysroot })
    }

    await build.exec('./autogen.sh')
    const configureArgs = [
      '--enable-static',
      '--disable-shared',
      `--host=${platform.triple}`,
      `--prefix=${prefixPath}`
    ]
    // Host clang 21 treats -Wmissing-braces as an error; iOS SDK builds
    // already skip -Werror on Darwin.
    if (platform.type === 'host') configureArgs.push('--disable-Werror')
    await build.exec('./configure', configureArgs)
    await build.exec('make', [])
    await build.exec('make', ['install'])
  }
})
