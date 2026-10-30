#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  pin_t a;
  pin_t b;
  uint32_t direction_attr;
  uint32_t trigger_attr;
  uint32_t last_trigger;
  uint32_t direction;
  unsigned int stage;
  unsigned int ticks;
} passage_t;

static void set_beams(passage_t *chip, unsigned int first,
                      unsigned int second) {
  if (chip->direction == 0) {
    pin_write(chip->a, first);
    pin_write(chip->b, second);
  } else {
    pin_write(chip->a, second);
    pin_write(chip->b, first);
  }
}

static void passage_tick(void *user_data) {
  passage_t *chip = user_data;
  uint32_t trigger = attr_read(chip->trigger_attr);

  if (trigger != chip->last_trigger) {
    chip->last_trigger = trigger;
    if (chip->stage != 0) {
      printf("Passage busy: crossing request rejected; toggle again after completion.\n");
    } else {
      uint32_t direction = attr_read(chip->direction_attr);
      if (direction > 1 || trigger > 1) {
        printf("Invalid passage control: direction and trigger must be 0 or 1.\n");
        return;
      }
      chip->direction = direction;
      chip->stage = 1;
      chip->ticks = 0;
      set_beams(chip, 1, 0);
      return;
    }
  }

  if (chip->stage == 0) {
    return;
  }
  chip->ticks++;
  if (chip->ticks < 10) {
    return;
  }
  chip->ticks = 0;
  chip->stage++;

  if (chip->stage == 2) {
    set_beams(chip, 1, 1);
  } else if (chip->stage == 3) {
    set_beams(chip, 0, 1);
  } else {
    set_beams(chip, 0, 0);
    chip->stage = 0;
  }
}

void chip_init(void) {
  passage_t *chip = calloc(1, sizeof(passage_t));
  if (chip == NULL) {
    printf("Passage simulator initialization failed: out of memory.\n");
    abort();
  }

  chip->a = pin_init("A", OUTPUT_LOW);
  chip->b = pin_init("B", OUTPUT_LOW);
  chip->direction_attr = attr_init("direction", 0);
  chip->trigger_attr = attr_init("trigger", 0);
  chip->last_trigger = attr_read(chip->trigger_attr);

  const timer_config_t config = {
    .callback = passage_tick,
    .user_data = chip
  };
  timer_t timer = timer_init(&config);
  timer_start(timer, 20000, true);
}
