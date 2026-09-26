#pragma once

// LVGL config for the s3_display node (ESP32_8048S050C, 800x480 RGB panel).
// Trimmed from lvgl's lv_conf_template.h down to what GameView actually
// uses (lv_obj, lv_label, lv_bar, lv_canvas + their button/click event
// machinery) — see the "s3_display requires LVGL" project memory for why
// this board is on LVGL at all. Pulled in via -DLV_CONF_INCLUDE_SIMPLE
// (platformio.ini, [env:s3_display]), which makes LVGL #include "lv_conf.h"
// by bare name off this folder's -I path.

#define LV_COLOR_DEPTH 16

// 8MB PSRAM / 16MB flash on this board (boards/esp32-8048S050C.json) —
// headroom isn't a concern; size generously rather than tuning tightly.
#define LV_MEM_SIZE (64U * 1024U)

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#define LV_USE_LOG 0

// --- widgets GameView actually builds screens out of ---
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_CANVAS 1
// lv_canvas embeds an lv_image_t internally (canvas is built on top of the
// image widget in v9) — required even though GameView never creates a
// standalone lv_image object.
#define LV_USE_IMAGE 1

// Everything else off — no demos/examples/unused widgets/themes beyond the
// bare minimum needed to draw an lv_obj at all.
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_USE_DEMOS 0
#define LV_USE_EXAMPLES 0
#define LV_BUILD_EXAMPLES 0

#define LV_USE_SYSMON 0
#define LV_USE_PROFILER 0
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
