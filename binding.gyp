{
  "targets": [
    {
      "target_name": "monero_lwsf_node",
      "sources": [
        "src/node-addon.cpp",
        "src/monero-wrapper/monero-methods.cpp"
      ],
      "include_dirs": [
        "<(module_root_dir)/src",
        "<!(node -p \"process.env.MONERO_LWSF_PREFIX_PATH\")/include",
        "<!(node -p \"process.env.MONERO_LWSF_LWSF_BUILD_PATH\")/include",
        "<!(node -p \"process.env.MONERO_LWSF_LWSF_CMAKE_PATH\")/include",
        "<!(node -p \"process.env.MONERO_LWSF_MONERO_PATH\")/src",
        "<!(node -p \"process.env.MONERO_LWSF_MONERO_PATH\")/contrib/epee/include",
        "<!(node -p \"process.env.MONERO_LWSF_MONERO_PATH\")/external/easylogging++"
      ],
      "libraries": [
        "<!@(node ./scripts/node-gyp-libraries.js)"
      ],
      "cflags_cc": [
        "-std=c++17",
        "-frtti",
        "-fexceptions"
      ],
      "conditions": [
        [
          "OS==\"mac\"",
          {
            "xcode_settings": {
              "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
              "CLANG_CXX_LIBRARY": "libc++",
              "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
              "GCC_ENABLE_CPP_RTTI": "YES",
              "MACOSX_DEPLOYMENT_TARGET": "13.0",
              "OTHER_LDFLAGS": [
                "-framework AppKit",
                "-framework CoreFoundation",
                "-framework CoreServices",
                "-framework IOKit",
                "-framework Security",
                "-framework SystemConfiguration"
              ]
            }
          }
        ]
      ]
    }
  ]
}
