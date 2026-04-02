import { readFile, writeFile } from 'fs/promises'
import { join } from 'path'

import { getRepo } from '../utils/common'
import { defineLib } from '../utils/lib'
import { addTask } from '../utils/tasks'

const moneroHash = '38bc62741b82cca179fb8e3437a388b0e0f67842' // Nov 7, 2025
const moneroCacheTag = `${moneroHash}-macos-node-1`
// const moneroHash = '1c9686cb45bec8cd1ca5142426b9ea9458ac4384' // Last compatible version?

addTask({
  name: 'monero.clone',
  cacheTag: moneroCacheTag,
  async run(build) {
    await getRepo(
      'monero',
      'https://github.com/monero-project/monero.git',
      moneroHash
    )

    // Hack the build:
    const cmakePath = join(build.basePath, 'monero', 'CMakeLists.txt')
    const cmakeList = await readFile(cmakePath, 'utf8')
    await writeFile(
      cmakePath,
      cmakeList
        .replace(
          '  forbid_undefined_symbols()',
          '# $& # Disabled by react-native build'
        )
        .replace(
          'INCLUDE(CmakeLists_IOS.txt)',
          '# $& # Disabled by react-native build'
        ),
      'utf8'
    )

    const minerPath = join(
      build.basePath,
      'monero/src/cryptonote_basic/miner.cpp'
    )
    const minerCpp = await readFile(minerPath, 'utf8')
    await writeFile(
      minerPath,
      minerCpp
        .replace(
          '#include <IOKit/IOKitLib.h>',
          '// $& # Disabled by react-native build'
        )
        .replace(
          '#include <IOKit/ps/IOPSKeys.h>',
          '// $& # Disabled by react-native build'
        )
        .replace(
          '#include <IOKit/ps/IOPowerSources.h>',
          '// $& # Disabled by react-native build'
        )
        .replace(
          '        return boost::logic::tribool(IOPSGetTimeRemainingEstimate() != kIOPSTimeRemainingUnlimited);',
          '        return boost::logic::tribool(boost::logic::indeterminate); // Disabled by react-native build'
        ),
      'utf8'
    )

    return moneroHash
  }
})

export const lwsf = defineLib({
  name: 'lwsf',
  cacheTag: '0',
  libDeps: ['boost', 'libsodium', 'libunbound', 'libzmq', 'openssl'],
  deps: ['monero.clone'],

  url: 'https://github.com/vtnerd/lwsf.git',
  hash: 'cedb2164f9ccd418b91a4e54ee8479c8d5c3cad0', // Nov 7, 2025

  async build(build, platform, prefixPath) {
    // Patch rpc.cpp to support api_key injection in HTTP requests
    const rpcPath = join(build.cwd, 'src/rpc.cpp')
    const rpcCpp = await readFile(rpcPath, 'utf8')
    await writeFile(
      rpcPath,
      rpcCpp
        // Add api_key storage after includes
        .replace(
          '#include "wire/wrappers_impl.h"',
          `#include "wire/wrappers_impl.h"

// API key storage for request injection (added by react-native build)
namespace lwsf { namespace config {
  static std::string g_api_key;
  const std::string& api_key() { return g_api_key; }
  void set_api_key(const std::string& k) { g_api_key = k; }
}}`
        )
        // Modify invoke_payload to inject api_key into JSON body
        .replace(
          `expect<std::string> invoke_payload(http_client& client, const boost::string_ref endpoint, const epee::byte_slice payload)
  {
    static const epee::net_utils::http::fields_list headers{
      {"Content-Type", "application/json; charset=utf-8"}
    };

    const epee::net_utils::http::http_response_info* response = nullptr;
    if (!client.invoke(endpoint, "POST", {reinterpret_cast<const char*>(payload.data()), payload.size()}, config::rpc_timeout, std::addressof(response), headers))
      return {error::no_response};`,
          `expect<std::string> invoke_payload(http_client& client, const boost::string_ref endpoint, epee::byte_slice payload)
  {
    static const epee::net_utils::http::fields_list headers{
      {"Content-Type", "application/json; charset=utf-8"}
    };

    // Inject api_key if set (added by react-native build)
    std::string body_str;
    const std::string& key = config::api_key();
    if (!key.empty()) {
      std::string original(reinterpret_cast<const char*>(payload.data()), payload.size());
      size_t pos = original.rfind('}');
      if (pos != std::string::npos && pos > 0) {
        body_str = original.substr(0, pos);
        if (original[pos-1] != '{') body_str += ",";
        body_str += "\\"api_key\\":\\"" + key + "\\"}";
      } else {
        body_str = original;
      }
    } else {
      body_str.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    const epee::net_utils::http::http_response_info* response = nullptr;
    if (!client.invoke(endpoint, "POST", body_str, config::rpc_timeout, std::addressof(response), headers))
      return {error::no_response};`
        ),
      'utf8'
    )
    build.log('Patched rpc.cpp for api_key support')

    build.exportEnv({
      PKG_CONFIG_PATH: join(prefixPath, '/lib/pkgconfig')
    })

    // Works for Android:
    await build.exec('cmake', [
      // Source directory:
      `-S${build.cwd}`,
      // Build directory:
      `-B${join(build.cwd, 'cmake')}`,
      // Build options:
      `-DCMAKE_BUILD_TYPE=Release`,
      `-DCMAKE_CXX_FLAGS=-DLWSF_MASTER_ENABLE`,
      `-DCMAKE_C_FLAGS=-D_DARWIN_C_SOURCE`,
      `-DCMAKE_FIND_ROOT_PATH=${prefixPath};${platform.sysroot}`,
      `-DCMAKE_INSTALL_PREFIX=${prefixPath}`,
      `-DCMAKE_PREFIX_PATH=${prefixPath}`,
      `-DMONERO_SOURCE_DIR=${join(build.basePath, 'monero')}`,
      `-DSTATIC=true`,
      `-DUSE_DEVICE_TREZOR=OFF`,
      ...platform.cmakeFlags
    ])
    await build.exec('cmake', [
      '--build',
      join(build.cwd, 'cmake'),
      '--config',
      'Release',
      '--target',
      'lwsf-api'
    ])

    build.log('done')
  }
})
