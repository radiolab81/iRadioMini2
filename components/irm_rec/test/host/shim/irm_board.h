#pragma once
#include <stdbool.h>
#ifndef IRM_SD_MOUNT_POINT
#define IRM_SD_MOUNT_POINT "/sdcard"
#endif
bool irm_board_sd_mounted(void);
