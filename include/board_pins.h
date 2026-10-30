#ifndef SCMS_BOARD_PINS_H
#define SCMS_BOARD_PINS_H

#define SCMS_SPI_SCK_GPIO             18
#define SCMS_SPI_MOSI_GPIO            23
#define SCMS_SPI_MISO_GPIO            19
#define SCMS_RFID_ENTRY_CS_GPIO        4
#define SCMS_RFID_CHECKPOINT2_CS_GPIO 13
#define SCMS_RFID_EXIT_CS_GPIO         14
#define SCMS_SD_CS_GPIO                5

#define SCMS_I2C_SDA_GPIO             21
#define SCMS_I2C_SCL_GPIO             22
#define SCMS_LCD_I2C_ADDRESS        0x27
#define SCMS_RTC_I2C_ADDRESS        0x68
#define SCMS_LCD_COLUMNS             20
#define SCMS_LCD_ROWS                 4

#define SCMS_SERVO_ENTRY_GPIO         25
#define SCMS_SERVO_CHECKPOINT2_GPIO   26
#define SCMS_SERVO_EXIT_GPIO          27
#define SCMS_BUZZER_GPIO              32
#define SCMS_ROOM1_PIR_GPIO           34
#define SCMS_ROOM2_PIR_GPIO           35
#define SCMS_EMERGENCY_GPIO           39
#define SCMS_EMERGENCY_ACTIVE_LEVEL    0

#define SCMS_INPUT_CLOCK_GPIO         17
#define SCMS_INPUT_LOAD_GPIO          33
#define SCMS_INPUT_DATA_GPIO          36
#define SCMS_INPUT_REGISTER_COUNT      2
#define SCMS_INPUT_COUNT              16

/* Read U1 D7..D0 into byte 0, then U2 D7..D0 into byte 1. */
#define SCMS_INPUT_ENTRY_A             0
#define SCMS_INPUT_ENTRY_B             1
#define SCMS_INPUT_CHECKPOINT2_A       2
#define SCMS_INPUT_CHECKPOINT2_B       3
#define SCMS_INPUT_EXIT_A              4
#define SCMS_INPUT_EXIT_B              5
#define SCMS_INPUT_ROOM1_A             6
#define SCMS_INPUT_ROOM1_B             7
#define SCMS_INPUT_ROOM2_A             8
#define SCMS_INPUT_ROOM2_B             9
#define SCMS_INPUT_DOOR_ENTRY         10
#define SCMS_INPUT_DOOR_CHECKPOINT2   11
#define SCMS_INPUT_DOOR_EXIT          12
#define SCMS_INPUT_FIRE_ALARM         13
#define SCMS_INPUT_MENU_NEXT          14
#define SCMS_INPUT_MENU_SELECT        15
#define SCMS_INPUT_ACTIVE_LEVEL        1
#define SCMS_DOOR_OPEN_LEVEL           1
#define SCMS_FIRE_ACTIVE_LEVEL         1

#define SCMS_ROOM1_RELAY_GPIO         16
#define SCMS_ROOM2_RELAY_GPIO         15
#define SCMS_STATUS_LED_GPIO           2
#define SCMS_STATUS_LED_COUNT          1

/* Default Wokwi npn relay: LOW selects NO; HIGH selects NC. */
#define SCMS_RELAY_ON_LEVEL            0
#define SCMS_RELAY_OFF_LEVEL           1

#endif
