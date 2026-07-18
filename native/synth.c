/*
 * frothy-synth experimental AMY bridge
 *
 * AMY renders on one pinned FreeRTOS task. Only copied scalar events cross
 * from Frothy into AMY; the audio task never reads Frothy runtime memory.
 * SPDX-License-Identifier: MIT
 */

#include "object.h"
#include "runtime.h"
#include "tagged.h"
#include "types.h"

#include "../vendor/amy/src/amy.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(ESP_PLATFORM)
#error "frothy-synth currently requires ESP-IDF"
#endif

enum {
  FR_SYNTH_MIN_OSCS = 1,
  FR_SYNTH_MAX_OSCS = 32,
  FR_SYNTH_MAX_HZ = 20000,
  FR_SYNTH_LEVEL_SCALE = 1000,
  FR_SYNTH_TASK_STACK_BYTES = 16 * 1024,
  FR_SYNTH_STOP_TIMEOUT_MS = 1000,
};

static bool fr_synth_started;
static uint16_t fr_synth_max_oscs;
static atomic_bool fr_synth_audio_running;
static atomic_int fr_synth_audio_error;
static i2s_chan_handle_t fr_synth_i2s;
static StaticSemaphore_t fr_synth_stopped_storage;
static SemaphoreHandle_t fr_synth_stopped;

static fr_err_t fr_synth_check_call(fr_runtime_t *runtime,
                                    const fr_tagged_t *args, uint8_t arg_count,
                                    uint8_t expected, fr_tagged_t *out) {
  if (runtime == NULL || out == NULL || arg_count != expected ||
      (args == NULL && arg_count > 0)) {
    return FR_ERR_INVALID;
  }
  return FR_OK;
}

static fr_err_t fr_synth_decode_int(const fr_tagged_t *args, uint8_t index,
                                    fr_int_t *out) {
  if (args == NULL || out == NULL) {
    return FR_ERR_INVALID;
  }
  return fr_tagged_decode_int(args[index], out);
}

static void fr_synth_i2s_close(void) {
  if (fr_synth_i2s == NULL) {
    return;
  }
  (void)i2s_channel_disable(fr_synth_i2s);
  (void)i2s_del_channel(fr_synth_i2s);
  fr_synth_i2s = NULL;
}

static fr_err_t fr_synth_i2s_open(int bclk, int ws, int dout) {
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  i2s_std_config_t standard = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)bclk,
              .ws = (gpio_num_t)ws,
              .dout = (gpio_num_t)dout,
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {0},
          },
  };

  if (i2s_new_channel(&channel, &fr_synth_i2s, NULL) != ESP_OK) {
    fr_synth_i2s = NULL;
    return FR_ERR_IO;
  }
  if (i2s_channel_init_std_mode(fr_synth_i2s, &standard) != ESP_OK ||
      i2s_channel_enable(fr_synth_i2s) != ESP_OK) {
    fr_synth_i2s_close();
    return FR_ERR_IO;
  }
  return FR_OK;
}

static void fr_synth_audio_task(void *context) {
  (void)context;
  while (atomic_load(&fr_synth_audio_running)) {
    int64_t started_us = amy_get_us();
    int16_t *block = amy_simple_fill_buffer();
    size_t written = 0;

    amy_overload_check((uint32_t)(amy_get_us() - started_us));
    esp_err_t err = i2s_channel_write(fr_synth_i2s, block,
                                      AMY_BLOCK_SIZE * AMY_NCHANS *
                                          sizeof(output_sample_type),
                                      &written, pdMS_TO_TICKS(100));

    if (err != ESP_OK ||
        written != AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(output_sample_type)) {
      atomic_store(&fr_synth_audio_error, FR_ERR_IO);
      atomic_store(&fr_synth_audio_running, false);
    }
  }
  xSemaphoreGive(fr_synth_stopped);
  vTaskDelete(NULL);
}

/*
 * AMY platform hooks. Frothy owns the one ESP-IDF audio task and I2S channel.
 */
void amy_platform_init(void) {}
void amy_platform_deinit(void) {}
void amy_update_tasks(void) {}
int16_t *amy_render_audio(void) { return amy_simple_fill_buffer(); }
size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
  (void)buffer;
  (void)nbytes;
  return 0;
}

fr_err_t fr_lib_synth_start(fr_runtime_t *runtime, const fr_tagged_t *args,
                            uint8_t arg_count, fr_tagged_t *out) {
  fr_int_t bclk = 0;
  fr_int_t ws = 0;
  fr_int_t dout = 0;
  fr_int_t max_oscs = 0;
  amy_config_t config;

  FR_TRY(fr_synth_check_call(runtime, args, arg_count, 4, out));
  if (fr_synth_started) {
    return FR_ERR_INVALID;
  }
  FR_TRY(fr_synth_decode_int(args, 0, &bclk));
  FR_TRY(fr_synth_decode_int(args, 1, &ws));
  FR_TRY(fr_synth_decode_int(args, 2, &dout));
  FR_TRY(fr_synth_decode_int(args, 3, &max_oscs));
  if (!GPIO_IS_VALID_OUTPUT_GPIO(bclk) || !GPIO_IS_VALID_OUTPUT_GPIO(ws) ||
      !GPIO_IS_VALID_OUTPUT_GPIO(dout) || bclk == ws || bclk == dout ||
      ws == dout || max_oscs < FR_SYNTH_MIN_OSCS ||
      max_oscs > FR_SYNTH_MAX_OSCS) {
    return FR_ERR_DOMAIN;
  }

  FR_TRY(fr_synth_i2s_open((int)bclk, (int)ws, (int)dout));

  config = amy_default_config();
  config.features.reverb = 0;
  config.features.echo = 0;
  config.features.chorus = 0;
  config.features.partials = 0;
  config.features.custom = 0;
  config.features.default_synths = 0;
  config.features.audio_in = 0;
  config.features.startup_bleep = 0;
  config.platform.multicore = 0;
  config.platform.multithread = 0;
  config.midi = AMY_MIDI_IS_NONE;
  config.audio = AMY_AUDIO_IS_NONE;
  config.max_oscs = (uint16_t)max_oscs;
  config.ks_oscs = 0;
  config.max_sequencer_tags = 1;
  config.max_voices = 1;
  config.max_synths = 1;
  config.max_memory_patches = 0;
  amy_start(config);

  fr_synth_stopped = xSemaphoreCreateBinaryStatic(&fr_synth_stopped_storage);
  atomic_store(&fr_synth_audio_error, FR_OK);
  atomic_store(&fr_synth_audio_running, true);
  if (xTaskCreatePinnedToCore(fr_synth_audio_task, "frothy_synth",
                              FR_SYNTH_TASK_STACK_BYTES, NULL,
                              ESP_TASK_PRIO_MAX - 1, NULL, 1) != pdPASS) {
    atomic_store(&fr_synth_audio_running, false);
    amy_stop();
    fr_synth_i2s_close();
    return FR_ERR_CAPACITY;
  }

  fr_synth_max_oscs = (uint16_t)max_oscs;
  fr_synth_started = true;
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_synth_stop(fr_runtime_t *runtime, const fr_tagged_t *args,
                           uint8_t arg_count, fr_tagged_t *out) {
  fr_err_t audio_error = FR_OK;

  FR_TRY(fr_synth_check_call(runtime, args, arg_count, 0, out));
  if (!fr_synth_started) {
    *out = fr_tagged_nil();
    return FR_OK;
  }

  atomic_store(&fr_synth_audio_running, false);
  if (xSemaphoreTake(fr_synth_stopped,
                     pdMS_TO_TICKS(FR_SYNTH_STOP_TIMEOUT_MS)) != pdTRUE) {
    return FR_ERR_IO;
  }
  audio_error = (fr_err_t)atomic_load(&fr_synth_audio_error);
  fr_synth_i2s_close();
  amy_stop();
  fr_synth_started = false;
  fr_synth_max_oscs = 0;
  *out = fr_tagged_nil();
  return audio_error;
}

fr_err_t fr_lib_synth_running(fr_runtime_t *runtime, const fr_tagged_t *args,
                              uint8_t arg_count, fr_tagged_t *out) {
  FR_TRY(fr_synth_check_call(runtime, args, arg_count, 0, out));
  return fr_tagged_encode_bool(
      fr_synth_started && atomic_load(&fr_synth_audio_running), out);
}

fr_err_t fr_lib_synth_now(fr_runtime_t *runtime, const fr_tagged_t *args,
                          uint8_t arg_count, fr_tagged_t *out) {
  uint32_t now = 0;

  FR_TRY(fr_synth_check_call(runtime, args, arg_count, 0, out));
  if (!fr_synth_started || !atomic_load(&fr_synth_audio_running)) {
    return FR_ERR_INVALID;
  }
  now = amy_sysclock();
  if (now > FR_TAGGED_INT_MAX) {
    return FR_ERR_RANGE;
  }
  return fr_tagged_encode_int((fr_int_t)now, out);
}

fr_err_t fr_lib_synth_sine_at(fr_runtime_t *runtime, const fr_tagged_t *args,
                              uint8_t arg_count, fr_tagged_t *out) {
  fr_int_t time = 0;
  fr_int_t oscillator = 0;
  fr_int_t hz = 0;
  fr_int_t level = 0;
  amy_event event;

  FR_TRY(fr_synth_check_call(runtime, args, arg_count, 4, out));
  if (!fr_synth_started || !atomic_load(&fr_synth_audio_running)) {
    return FR_ERR_INVALID;
  }
  FR_TRY(fr_synth_decode_int(args, 0, &time));
  FR_TRY(fr_synth_decode_int(args, 1, &oscillator));
  FR_TRY(fr_synth_decode_int(args, 2, &hz));
  FR_TRY(fr_synth_decode_int(args, 3, &level));
  if (time < 0 || oscillator < 0 || oscillator >= fr_synth_max_oscs || hz < 1 ||
      hz > FR_SYNTH_MAX_HZ || level < 0 || level > FR_SYNTH_LEVEL_SCALE) {
    return FR_ERR_DOMAIN;
  }

  event = amy_default_event();
  event.time = (uint32_t)time;
  event.osc = (uint16_t)oscillator;
  event.wave = SINE;
  event.freq_coefs[COEF_CONST] = (float)hz;
  event.velocity = (float)level / (float)FR_SYNTH_LEVEL_SCALE;
  amy_add_event(&event);
  *out = fr_tagged_nil();
  return FR_OK;
}
