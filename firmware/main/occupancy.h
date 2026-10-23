#pragma once

void occupancy_init(void);
void occupancy_on_entry(void);
void occupancy_on_exit(void);
void occupancy_set_room1(int count);
void occupancy_set_room2(int count);
int occupancy_building(void);
int occupancy_room1(void);
int occupancy_room2(void);
