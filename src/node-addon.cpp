#include <node_api.h>

#include <string>
#include <vector>

#include "monero-wrapper/monero-methods.hpp"

namespace {

void throwTypeError(napi_env env, const char* message) {
  napi_throw_type_error(env, nullptr, message);
}

bool getString(napi_env env, napi_value value, std::string& out) {
  size_t length = 0;
  napi_status status = napi_get_value_string_utf8(env, value, nullptr, 0, &length);
  if (status != napi_ok) return false;

  std::string buffer(length, '\0');
  status = napi_get_value_string_utf8(
    env,
    value,
    buffer.data(),
    buffer.size() + 1,
    &length
  );
  if (status != napi_ok) return false;

  out.assign(buffer.data(), length);
  return true;
}

napi_value callMoneroSync(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_status status = napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (status != napi_ok || argc != 2) {
    throwTypeError(env, "callMoneroSync expects (name, args)");
    return nullptr;
  }

  std::string methodName;
  if (!getString(env, args[0], methodName)) {
    throwTypeError(env, "callMoneroSync name must be a string");
    return nullptr;
  }

  bool isArray = false;
  status = napi_is_array(env, args[1], &isArray);
  if (status != napi_ok || !isArray) {
    throwTypeError(env, "callMoneroSync args must be a string array");
    return nullptr;
  }

  uint32_t length = 0;
  status = napi_get_array_length(env, args[1], &length);
  if (status != napi_ok) {
    napi_throw_error(env, nullptr, "Failed to read argument array length");
    return nullptr;
  }

  std::vector<const std::string> methodArgs;
  methodArgs.reserve(length);
  for (uint32_t i = 0; i < length; ++i) {
    napi_value item;
    status = napi_get_element(env, args[1], i, &item);
    if (status != napi_ok) {
      napi_throw_error(env, nullptr, "Failed to read argument array item");
      return nullptr;
    }

    std::string value;
    if (!getString(env, item, value)) {
      throwTypeError(env, "callMoneroSync args must contain only strings");
      return nullptr;
    }
    methodArgs.push_back(value);
  }

  for (unsigned i = 0; i < moneroMethodCount; ++i) {
    if (moneroMethods[i].name != methodName) continue;

    if (
      moneroMethods[i].argc != -1 &&
      methodArgs.size() != static_cast<size_t>(moneroMethods[i].argc)
    ) {
      napi_throw_error(env, nullptr, "lwsf incorrect C++ argument count");
      return nullptr;
    }

    try {
      const std::string output = moneroMethods[i].method(methodArgs);
      napi_value out;
      status = napi_create_string_utf8(
        env,
        output.c_str(),
        output.size(),
        &out
      );
      if (status != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to create result string");
        return nullptr;
      }
      return out;
    } catch (const std::exception& error) {
      napi_throw_error(env, nullptr, error.what());
      return nullptr;
    } catch (...) {
      napi_throw_error(env, nullptr, "lwsf threw a C++ exception");
      return nullptr;
    }
  }

  napi_throw_error(
    env,
    nullptr,
    ("No lwsf C++ method " + methodName).c_str()
  );
  return nullptr;
}

napi_value initialize(napi_env env, napi_value exports) {
  napi_value callMonero;
  napi_status status = napi_create_function(
    env,
    "callMoneroSync",
    NAPI_AUTO_LENGTH,
    callMoneroSync,
    nullptr,
    &callMonero
  );
  if (status != napi_ok) return nullptr;

  status = napi_set_named_property(env, exports, "callMoneroSync", callMonero);
  if (status != napi_ok) return nullptr;

  napi_value methodNames;
  status = napi_create_array_with_length(env, moneroMethodCount, &methodNames);
  if (status != napi_ok) return nullptr;

  for (unsigned i = 0; i < moneroMethodCount; ++i) {
    napi_value name;
    status = napi_create_string_utf8(
      env,
      moneroMethods[i].name,
      NAPI_AUTO_LENGTH,
      &name
    );
    if (status != napi_ok) return nullptr;

    status = napi_set_element(env, methodNames, i, name);
    if (status != napi_ok) return nullptr;
  }

  status = napi_set_named_property(env, exports, "methodNames", methodNames);
  if (status != napi_ok) return nullptr;

  return exports;
}

} // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, initialize)
