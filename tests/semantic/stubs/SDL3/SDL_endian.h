#ifndef REALMZ_TEST_STUB_SDL_ENDIAN_H
#define REALMZ_TEST_STUB_SDL_ENDIAN_H

#include <stdint.h>

// convert.h defines inline compatibility helpers while compiling the isolated
// fixture. Keep these two unused declarations semantically accurate so the
// test never depends on an initialized SDL checkout.
static inline uint16_t SDL_Swap16BE(uint16_t value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  return value;
#else
  return (uint16_t)((value >> 8U) | (value << 8U));
#endif
}

static inline uint32_t SDL_Swap32BE(uint32_t value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  return value;
#else
  return ((value & UINT32_C(0x000000FF)) << 24U) |
      ((value & UINT32_C(0x0000FF00)) << 8U) |
      ((value & UINT32_C(0x00FF0000)) >> 8U) |
      ((value & UINT32_C(0xFF000000)) >> 24U);
#endif
}

#endif
