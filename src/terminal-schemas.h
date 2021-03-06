/*
 * Copyright © 2008, 2010 Christian Persch
 *
 * This programme is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This programme is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this programme; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef TERMINAL_SCHEMAS_H
#define TERMINAL_SCHEMAS_H

#include <glib.h>

G_BEGIN_DECLS

#define TERMINAL_PROFILE_SCHEMA         "org.gnome.Terminal.Profile"
#define TERMINAL_SETTING_SCHEMA         "org.gnome.Terminal.Settings"

#define TERMINAL_PROFILE_ALLOW_BOLD_KEY                 "allow-bold"
#define TERMINAL_PROFILE_AUDIBLE_BELL_KEY               "audible-bell"
#define TERMINAL_PROFILE_BACKGROUND_COLOR_KEY           "background-color"
#define TERMINAL_PROFILE_BACKSPACE_BINDING_KEY          "backspace-binding"
#define TERMINAL_PROFILE_BOLD_COLOR_KEY                 "bold-color"
#define TERMINAL_PROFILE_BOLD_COLOR_SAME_AS_FG_KEY      "bold-color-same-as-fg"
#define TERMINAL_PROFILE_CURSOR_BLINK_MODE_KEY          "cursor-blink-mode"
#define TERMINAL_PROFILE_CURSOR_SHAPE_KEY               "cursor-shape"
#define TERMINAL_PROFILE_CUSTOM_COMMAND_KEY             "custom-command"
#define TERMINAL_PROFILE_DEFAULT_SIZE_COLUMNS_KEY       "default-size-columns"
#define TERMINAL_PROFILE_DEFAULT_SIZE_ROWS_KEY          "default-size-rows"
#define TERMINAL_PROFILE_DELETE_BINDING_KEY             "delete-binding"
#define TERMINAL_PROFILE_ENCODING                       "encoding"
#define TERMINAL_PROFILE_EXIT_ACTION_KEY                "exit-action"
#define TERMINAL_PROFILE_FONT_KEY                       "font"
#define TERMINAL_PROFILE_FOREGROUND_COLOR_KEY           "foreground-color"
#define TERMINAL_PROFILE_LOGIN_SHELL_KEY                "login-shell"
#define TERMINAL_PROFILE_NAME_KEY                       "name"
#define TERMINAL_PROFILE_PALETTE_KEY                    "palette"
#define TERMINAL_PROFILE_SCROLLBACK_LINES_KEY           "scrollback-lines"
#define TERMINAL_PROFILE_SCROLLBACK_UNLIMITED_KEY       "scrollback-unlimited"
#define TERMINAL_PROFILE_SCROLLBAR_POLICY_KEY           "scrollbar-policy"
#define TERMINAL_PROFILE_SCROLL_ON_KEYSTROKE_KEY        "scroll-on-keystroke"
#define TERMINAL_PROFILE_SCROLL_ON_OUTPUT_KEY           "scroll-on-output"
#define TERMINAL_PROFILE_TITLE_MODE_KEY                 "title-mode"
#define TERMINAL_PROFILE_TITLE_KEY                      "title"
#define TERMINAL_PROFILE_UPDATE_RECORDS_KEY             "update-records"
#define TERMINAL_PROFILE_USE_CUSTOM_COMMAND_KEY         "use-custom-command"
#define TERMINAL_PROFILE_USE_CUSTOM_DEFAULT_SIZE_KEY    "use-custom-default-size"
#define TERMINAL_PROFILE_USE_SKEY_KEY                   "use-skey"
#define TERMINAL_PROFILE_USE_SYSTEM_FONT_KEY            "use-system-font"
#define TERMINAL_PROFILE_USE_THEME_COLORS_KEY           "use-theme-colors"
#define TERMINAL_PROFILE_VISIBLE_NAME_KEY               "visible-name"
#define TERMINAL_PROFILE_WORD_CHARS_KEY                 "word-chars"

#define TERMINAL_SETTING_CONFIRM_CLOSE_KEY              "confirm-close"
#define TERMINAL_SETTING_DEFAULT_PROFILE_KEY            "default-profile"
#define TERMINAL_SETTING_DEFAULT_SHOW_MENUBAR_KEY       "default-show-menubar"
#define TERMINAL_SETTING_ENABLE_MENU_BAR_ACCEL_KEY      "menu-accelerator-enabled"
#define TERMINAL_SETTING_ENABLE_MNEMONICS_KEY           "mnemonics-enabled"
#define TERMINAL_SETTING_ENCODINGS_KEY                  "encodings"

#define TERMINAL_PROFILES_PATH_PREFIX   "/org/gnome/terminal/profiles:/"
#define TERMINAL_DEFAULT_PROFILE_ID     ":profile0"
#define TERMINAL_DEFAULT_PROFILE_PATH   TERMINAL_PROFILES_PATH_PREFIX TERMINAL_DEFAULT_PROFILE_ID "/"

G_END_DECLS

#endif /* TERMINAL_SCHEMAS_H */
