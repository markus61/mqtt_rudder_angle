#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
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
