#pragma once
#include <stdio.h>

static int g_checks = 0, g_failed = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    g_checks++;                                                         \
    if (!(cond)) {                                                      \
      g_failed++;                                                       \
      fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                   \
  } while (0)

static int test_report(const char *name) {
  printf("%s: %d/%d kiểm tra đạt\n", name, g_checks - g_failed, g_checks);
  return g_failed ? 1 : 0;
}
