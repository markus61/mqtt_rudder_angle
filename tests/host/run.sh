#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

# LeakSanitizer suspends threads with ptrace at process exit. Sandboxed runners
# commonly deny ptrace, which would otherwise make a healthy ASan/UBSan test
# fail after it has completed. Keep leak checks available on supported hosts.
if [[ "${MANT1S_ENABLE_LEAK_CHECKS:-0}" != "1" ]]; then
  export ASAN_OPTIONS="${ASAN_OPTIONS:+${ASAN_OPTIONS}:}detect_leaks=0"
fi

test_build=$(mktemp -d /tmp/mant1s-registry-tests.XXXXXX)
cc -std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -Wno-unused-parameter \
  -Itests/host/include -Imain -Imain/adc -Imain/dummy \
  -Imanaged_components/espressif__cjson/cJSON \
  -Imanaged_components/espressif__json_generator/include \
  tests/host/registry_test.c main/registry_read_write.c main/json_utils.c \
  managed_components/espressif__cjson/cJSON/cJSON.c \
  managed_components/espressif__json_generator/src/json_generator.c \
  -lm -o "$test_build/registry_test"
"$test_build/registry_test"

cc -std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -Wno-unused-parameter \
  -Itests/host/include -Imain \
  -Imanaged_components/espressif__cjson/cJSON \
  tests/host/device_config_test.c \
  managed_components/espressif__cjson/cJSON/cJSON.c \
  -lm -o "$test_build/device_config_test"
"$test_build/device_config_test"
