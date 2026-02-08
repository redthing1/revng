// This file is distributed under the MIT License. See LICENSE.md for details.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int32_t helper(int32_t x) {
  // Keep some arithmetic and branching around so the CFG isn't trivial.
  if ((x & 1) != 0)
    return x * 3 + 1;
  return x / 2;
}

int32_t compute(int32_t seed) {
  int32_t acc = seed;
  for (int i = 0; i < 16; ++i)
    acc = helper(acc) ^ (i * 0x1234);
  return acc;
}

int main(int argc, char **argv) {
  int32_t seed = 0x1337;
  if (argc > 1)
    seed = (int32_t)strtol(argv[1], NULL, 0);

  int32_t out = compute(seed);
  // Print something so the binary doesn't get optimized into a no-op.
  printf("%d\n", out);
  return (out & 0xff) == 0 ? 0 : 1;
}

