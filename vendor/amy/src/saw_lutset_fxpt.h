/*
 * frothy-synth minimal AMY build
 *
 * Saw and pulse events are not exposed by the first Frothy surface. Retain an
 * inert, valid LUT so upstream renderer references link without shipping the
 * 16 KiB band-limited saw table set.
 */
#ifndef LUTSET_SAW_FXPT_DEFINED
#define LUTSET_SAW_FXPT_DEFINED

#ifndef LUTENTRY_FXPT_DEFINED
#define LUTENTRY_FXPT_DEFINED
typedef struct {
  const int16_t *table;
  int table_size;
  int log_2_table_size;
  int highest_harmonic;
  float scale_factor;
} lut_entry_fxpt;
#endif

static const int16_t fr_saw_silent_lut[8] PROGMEM = {0};

lut_entry_fxpt saw_fxpt_lutset[2] = {
    {fr_saw_silent_lut, 8, 3, 1, 1.0f},
    {NULL, 0, 0, 0, 0.0f},
};

#endif /* LUTSET_SAW_FXPT_DEFINED */
