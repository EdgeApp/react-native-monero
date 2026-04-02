import { mkdir } from 'fs/promises'
import { join } from 'path'

import { boost } from './libraries/boost'
import { libexpat } from './libraries/libexpat'
import { libsodium } from './libraries/libsodium'
import { libunbound } from './libraries/libunbound'
import { libzmq } from './libraries/libzmq'
import { lwsf } from './libraries/lwsf'
import { openssl } from './libraries/openssl'
import { tmpPath } from './utils/common'
import type { MacosPlatform } from './utils/platforms'
import { makeMacosPlatforms } from './utils/platforms'
import { addTask, startBuild } from './utils/tasks'

function addNodeAddonTask(platform: MacosPlatform): void {
  addTask({
    name: 'node-addon',
    deps: [`lwsf.build.${platform.name}`],
    async run(build) {
      const repoPath = join(__dirname, '..')

      await build.exec('npx', ['node-gyp', 'rebuild'], {
        cwd: repoPath,
        env: {
          ...build.env,
          MONERO_LWSF_PREFIX_PATH: join(
            build.basePath,
            'prefix',
            platform.name
          ),
          MONERO_LWSF_LWSF_CMAKE_PATH: join(
            build.basePath,
            'build',
            `lwsf-${platform.name}`,
            'cmake'
          ),
          MONERO_LWSF_LWSF_BUILD_PATH: join(
            build.basePath,
            'build',
            `lwsf-${platform.name}`
          ),
          MONERO_LWSF_MONERO_PATH: join(build.basePath, 'monero')
        }
      })
    }
  })
}

async function main(): Promise<void> {
  await mkdir(tmpPath, { recursive: true })

  const platforms = await makeMacosPlatforms()
  boost(platforms)
  libexpat(platforms)
  libsodium(platforms)
  libunbound(platforms)
  libzmq(platforms)
  lwsf(platforms)
  openssl(platforms)
  addNodeAddonTask(platforms[0])

  await startBuild(process.argv[2] ?? 'node-addon', { basePath: tmpPath })
}

main().catch((error: unknown) => {
  console.log(String(error))
  process.exit(1)
})
