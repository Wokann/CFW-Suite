#pragma once

char *selectFirmware(void);

#define MAX(a, b)   ((a) < (b) ? (b) : (a))
#define MIN(a, b)   ((a) > (b) ? (b) : (a))
#define MAX_DISPLAY_WIDTH 29   // Max characters displayed per file entry line
#define PATH_DISPLAY_WIDTH 26  // Max characters for path scrolling (excluding "Path: ")
#define SCROLL_DELAY 15        // Frames delay before scrolling text
#define END_PAUSE 30           // Frames to pause at end of scrolling
#define MAX_FILE_COUNT 512     // Maximum number of directory entries
#define MAX_FILENAME_LEN 256
#define MAX_PATH_LEN 512
