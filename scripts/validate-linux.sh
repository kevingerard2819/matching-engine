#!/bin/sh
# Reproduce correctness checks and measurements on a Linux host.
set -eu
cd "$(dirname "$0")/.."
orders=${BENCH_ORDERS:-100000}
runs=${BENCH_RUNS:-3}
jobs=${BUILD_JOBS:-2}
sanitizers=${SANITIZERS:-auto}
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel "$jobs"
ctest --test-dir build-release --output-on-failure
if [ "$sanitizers" = auto ]; then
  if printf 'int main() {}\n' | "${CXX:-c++}" -x c++ - -fsanitize=address,undefined \
      -o build-release/sanitizer-probe && ./build-release/sanitizer-probe; then
    sanitizers=ON
  else
    sanitizers=OFF
    echo 'Address/UB sanitizers unavailable: running checked STL Debug tests instead.'
  fi
fi
printf 'ENABLE_SANITIZERS=%s\n' "$sanitizers" > build-release/sanitizer-status.txt
cmake -S . -B build-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_SANITIZERS="$sanitizers" '-DCMAKE_CXX_FLAGS_DEBUG=-g -D_GLIBCXX_ASSERTIONS -D_GLIBCXX_DEBUG'
cmake --build build-sanitized --parallel "$jobs"
ctest --test-dir build-sanitized --output-on-failure
./build-release/generate_orders build-release/validation.orders "$orders" 42
python3 bench/experiments.py build-release/validation.orders --build build-release \
  --runs "$runs" --reserve "$orders" --output build-release/experiments.csv
