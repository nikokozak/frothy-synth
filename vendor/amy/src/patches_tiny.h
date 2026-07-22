/*
 * frothy-synth minimal AMY build
 *
 * The first Frothy surface cannot load patches, so do not ship AMY's 391
 * built-in DX7/Juno patch strings (~137 KiB of flash rodata) in the firmware
 * image. Keep one inert NULL entry so the upstream lookup code remains
 * compilable; the NULL guard rejects every built-in patch number and the
 * request fails clean. _PATCHES_NUM_BUILTIN stays 1 (not 0) because the
 * bounds compare on a uint16_t patch number would otherwise be always-true
 * and trip -Werror=type-limits.
 */
#ifndef __PATCHESH
#define __PATCHESH
#define _PATCHES_NUM_BUILTIN 1
static const char * const patch_commands[1] PROGMEM = {NULL};
const uint16_t patch_oscs[1] PROGMEM = {0};
#endif
