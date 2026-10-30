#include "scms.h"
#include <stdio.h>

/* A is bit 0, B is bit 1. Only a complete ordered sequence counts. */
int beam_step(beam_t *beam, unsigned int value, int64_t now) {
  if (beam->state != 0 && now - beam->since > 2500) {
    beam->state = value ? 5 : 0;
    beam->since = now;
    return 2;
  }
  if (beam->state == 5) {
    if (value == 0) {
      beam->state = 0;
    }
    return 0;
  }
  if (beam->state == 0) {
    if (value == 0) {
      return 0;
    }
    beam->since = now;
    if (value == 3) {
      beam->state = 5;
      return 2;
    }
    beam->direction = value == 1 ? 1 : -1;
    beam->state = 1;
    return 0;
  }
  unsigned int first = beam->direction == 1 ? 1 : 2;
  unsigned int second = beam->direction == 1 ? 2 : 1;
  if (beam->state == 1 && value == first) {
    return 0;
  }
  if (beam->state == 1 && value == 3) {
    beam->state = 2;
    return 0;
  }
  if (beam->state == 2 && value == 3) {
    return 0;
  }
  if (beam->state == 2 && value == second) {
    beam->state = 3;
    return 0;
  }
  if (beam->state == 3 && value == second) {
    return 0;
  }
  if (beam->state == 3 && value == 0) {
    beam->state = 0;
    return beam->direction;
  }
  beam->state = value ? 5 : 0;
  return 2;
}

bool occupancy_correct(app_t *app, int scope, int count) {
  if (!access_admin(app) || scope < 0 || scope > 2 ||
      count < 0 || count > 1000 || app->saved.emergency ||
      (scope == 0 && count != 0) ||
      (scope > 0 && count > app->saved.building)) {
    record_event(app, "CORRECTION_REJECTED",
                 "Check admin, scope, physical count, and emergency state");
    return false;
  }
  if (scope == 0) {
    if (hw_pir(0) || hw_pir(1)) {
      record_event(app, "CORRECTION_REJECTED", "Presence still detected");
      return false;
    }
    app->saved.building = 0;
    app->saved.inside = 0;
    app->saved.building_uncertain = false;
    for (int room = 0; room < 2; room++) {
      app->saved.rooms[room] = 0;
      app->saved.room_uncertain[room] = false;
    }
    access_cancel(app);
  } else {
    int room = scope - 1;
    if (count == 0 && hw_pir(room)) {
      record_event(app, "CORRECTION_REJECTED", "Room presence still detected");
      return false;
    }
    app->saved.rooms[room] = count;
    app->saved.room_uncertain[room] = false;
  }
  char detail[100];
  snprintf(detail, sizeof(detail), "Admin verified scope%d count=%d", scope, count);
  app->dirty = true;
  bool saved = storage_save(app);
  record_event(app, "OCCUPANCY_CORRECTION", detail);
  return saved;
}

void occupancy_tick(app_t *app, uint16_t inputs) {
  for (int zone = 0; zone < 5; zone++) {
    int event = beam_step(&app->beams[zone], (inputs >> (zone * 2)) & 3,
                          app->now);
    if (event == 0) {
      continue;
    }
    if (event == 2) {
      if (zone < 3) {
        app->saved.building_uncertain = true;
        access_cancel(app);
      } else {
        app->saved.room_uncertain[zone - 3] = true;
      }
      app->dirty = true;
      record_event(app, "SENSOR_CONFLICT", "Incomplete, ambiguous or timed-out beam sequence");
    } else if (zone < 3) {
      access_passage(app, zone, event);
    } else {
      int room = zone - 3;
      if (event > 0) {
        if (app->saved.rooms[room] < 1000) {
          app->saved.rooms[room]++;
        } else {
          app->saved.room_uncertain[room] = true;
        }
      } else if (app->saved.rooms[room] > 0) {
        app->saved.rooms[room]--;
      } else {
        app->saved.room_uncertain[room] = true;
      }
      if (app->saved.rooms[0] + app->saved.rooms[1] > app->saved.building) {
        app->saved.room_uncertain[room] = true;
      }
      app->dirty = true;
      char detail[80];
      snprintf(detail, sizeof(detail), "Room%d count=%d direction=%d",
               room + 1, app->saved.rooms[room], event);
      record_event(app, "ROOM_PASSAGE", detail);
    }
  }
  for (int room = 0; room < 2; room++) {
    bool presence = hw_pir(room);
    if (presence && app->saved.rooms[room] == 0 &&
        !app->saved.room_uncertain[room]) {
      app->saved.room_uncertain[room] = true;
      app->dirty = true;
      record_event(app, "PRESENCE_CONFLICT", room ? "Room2" : "Room1");
    }
    bool required = app->saved.emergency || app->saved.rooms[room] > 0 ||
                    presence || app->saved.room_uncertain[room];
    bool on = app->lights[room];
    if (required) {
      on = true;
      app->vacancy_since[room] = -1;
    } else {
      if (app->vacancy_since[room] < 0) {
        app->vacancy_since[room] = app->now;
      }
      if (app->now - app->vacancy_since[room] >= SCMS_VACANCY_MS) {
        on = false;
      }
    }
    if (on != app->lights[room]) {
      hw_light(room, on);
      app->lights[room] = on;
      record_event(app, "LIGHT_COMMAND",
                   room ? (on ? "Room2 ON" : "Room2 OFF") :
                          (on ? "Room1 ON" : "Room1 OFF"));
    }
  }
}
