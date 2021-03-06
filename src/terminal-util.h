/*
 * Copyright © 2001 Havoc Pennington
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

#ifndef TERMINAL_UTIL_H
#define TERMINAL_UTIL_H

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "terminal-screen.h"

G_BEGIN_DECLS

void terminal_util_set_unique_role (GtkWindow *window, const char *prefix);

void terminal_util_show_error_dialog (GtkWindow *transient_parent, 
                                      GtkWidget **weap_ptr, 
                                      GError *error,
                                      const char *message_format, ...) G_GNUC_PRINTF(4, 5);

void terminal_util_show_help (const char *topic, GtkWindow  *transient_parent);

void terminal_util_show_about (GtkWindow *transient_parent);

void terminal_util_set_labelled_by          (GtkWidget  *widget,
                                             GtkLabel   *label);
void terminal_util_set_atk_name_description (GtkWidget  *widget,
                                             const char *name,
                                             const char *desc);

void terminal_util_open_url (GtkWidget *parent,
                             const char *orig_url,
                             TerminalURLFlavour flavor,
                             guint32 user_time);

void terminal_util_transform_uris_to_quoted_fuse_paths (char **uris);

char *terminal_util_concat_uris (char **uris,
                                 gsize *length);

char *terminal_util_get_licence_text (void);

gboolean terminal_util_load_builder_file (const char *filename,
                                          const char *object_name,
                                          ...);

gboolean terminal_util_dialog_response_on_delete (GtkWindow *widget);

void terminal_util_key_file_set_string_escape    (GKeyFile *key_file,
                                                  const char *group,
                                                  const char *key,
                                                  const char *string);
void terminal_util_key_file_set_argv      (GKeyFile *key_file,
                                           const char *group,
                                           const char *key,
                                           int argc,
                                           char **argv);

void terminal_util_add_proxy_env (GHashTable *env_table);

GdkScreen *terminal_util_get_screen_by_display_name (const char *display_name,
                                                     int screen_number);

gboolean terminal_util_x11_get_net_wm_desktop (GdkWindow *window,
					       guint32   *desktop);
void     terminal_util_x11_set_net_wm_desktop (GdkWindow *window,
					       guint32    desktop);

void terminal_util_x11_clear_demands_attention (GdkWindow *window);

gboolean terminal_util_x11_window_is_minimized (GdkWindow *window);

const GdkRGBA *terminal_g_settings_get_rgba (GSettings  *settings,
                                             const char *key,
                                             GdkRGBA    *rgba);
void terminal_g_settings_set_rgba (GSettings  *settings,
                                   const char *key,
                                   const GdkRGBA *rgba);

GdkRGBA *terminal_g_settings_get_rgba_palette (GSettings  *settings,
                                               const char *key,
                                               gsize      *n_colors);
void terminal_g_settings_set_rgba_palette (GSettings      *settings,
                                           const char     *key,
                                           const GdkRGBA  *colors,
                                           gsize           n_colors);

void terminal_util_bind_mnemonic_label_sensitivity (GtkWidget *widget);

G_END_DECLS

#endif /* TERMINAL_UTIL_H */
