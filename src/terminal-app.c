/* terminal program */
/*
 * Copyright © 2001, 2002 Havoc Pennington
 * Copyright © 2002 Red Hat, Inc.
 * Copyright © 2002 Sun Microsystems
 * Copyright © 2003 Mariano Suarez-Alvarez
 * Copyright © 2008 Christian Persch
 *
 * This file is part of gnome-terminal.
 *
 * Gnome-terminal is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Gnome-terminal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "terminal-intl.h"

#include "terminal-app.h"
#include "terminal-accels.h"
#include "terminal-window.h"
#include "terminal-util.h"
#include "profile-editor.h"
#include "encoding.h"
#include <gconf/gconf-client.h>
#include <bonobo-activation/bonobo-activation-activate.h>
#include <bonobo-activation/bonobo-activation-register.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-listener.h>
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-help.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnomeui/gnome-url.h>
#include <libgnomeui/gnome-client.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <gdk/gdkx.h>

/* Settings storage works as follows:
 *   /apps/gnome-terminal/global/
 *   /apps/gnome-terminal/profiles/Foo/
 *
 * It's somewhat tricky to manage the profiles/ dir since we need to track
 * the list of profiles, but gconf doesn't have a concept of notifying that
 * a directory has appeared or disappeared.
 *
 * Session state is stored entirely in the RestartCommand command line.
 *
 * The number one rule: all stored information is EITHER per-session,
 * per-profile, or set from a command line option. THERE CAN BE NO
 * OVERLAP. The UI and implementation totally break if you overlap
 * these categories. See gnome-terminal 1.x for why.
 *
 * Don't use this code as an example of how to use GConf - it's hugely
 * overcomplicated due to the profiles stuff. Most apps should not
 * have to do scary things of this nature, and should not have
 * a profiles feature.
 *
 */

struct _TerminalAppClass {
  GObjectClass parent_class;

  void (* quit) (TerminalApp *app);
  void (* profile_list_changed) (TerminalApp *app);
};

struct _TerminalApp
{
  GObject parent_instance;

  GList *windows;
  GtkWidget *edit_encodings_dialog;
  GtkWidget *new_profile_dialog;
  GtkWidget *manage_profiles_dialog;
  GtkWidget *manage_profiles_list;
  GtkWidget *manage_profiles_new_button;
  GtkWidget *manage_profiles_edit_button;
  GtkWidget *manage_profiles_delete_button;
  GtkWidget *manage_profiles_default_menu;

  GConfClient *conf;
  guint profile_list_notify_id;
  guint default_profile_notify_id;
  guint system_font_notify_id;

  GHashTable *profiles;
  char* default_profile_id;
  TerminalProfile *default_profile;
  gboolean default_profile_locked;

  PangoFontDescription *system_font_desc;

  gboolean use_factory;
};

enum
{
  PROP_0,
  PROP_DEFAULT_PROFILE,
  PROP_SYSTEM_FONT,
};

enum
{
  QUIT,
  PROFILE_LIST_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

enum
{
  COL_PROFILE,
  NUM_COLUMNS
};

static TerminalApp *global_app = NULL;

#define MONOSPACE_FONT_DIR "/desktop/gnome/interface"
#define MONOSPACE_FONT_KEY MONOSPACE_FONT_DIR "/monospace_font_name"
#define DEFAULT_MONOSPACE_FONT ("Monospace 10")

#define PROFILE_LIST_KEY CONF_GLOBAL_PREFIX "/profile_list"
#define DEFAULT_PROFILE_KEY CONF_GLOBAL_PREFIX "/default_profile"

/* Helper functions */

static int
profiles_alphabetic_cmp (gconstpointer pa,
                         gconstpointer pb)
{
  TerminalProfile *a = (TerminalProfile *) pa;
  TerminalProfile *b = (TerminalProfile *) pb;
  int result;

  result =  g_utf8_collate (terminal_profile_get_property_string (a, TERMINAL_PROFILE_VISIBLE_NAME),
			    terminal_profile_get_property_string (b, TERMINAL_PROFILE_VISIBLE_NAME));
  if (result == 0)
    result = strcmp (terminal_profile_get_property_string (a, TERMINAL_PROFILE_NAME),
		     terminal_profile_get_property_string (b, TERMINAL_PROFILE_NAME));

  return result;
}

typedef struct
{
  TerminalProfile *result;
  const char *target;
} LookupInfo;

static void
profiles_lookup_by_visible_name_foreach (gpointer key,
                                         gpointer value,
                                         gpointer data)
{
  LookupInfo *info = data;
  const char *name;

  name = terminal_profile_get_property_string (value, TERMINAL_PROFILE_VISIBLE_NAME);
  if (name && strcmp (info->target, name) == 0)
    info->result = value;
}

static void
terminal_window_destroyed (TerminalWindow *window,
                           TerminalApp    *app)
{
  app->windows = g_list_remove (app->windows, window);

  if (app->windows == NULL)
    g_signal_emit (app, signals[QUIT], 0);
}

static TerminalProfile *
terminal_app_create_profile (TerminalApp *app,
                             const char *name)
{
  TerminalProfile *profile;

  g_assert (terminal_app_get_profile_by_name (app, name) == NULL);

  profile = _terminal_profile_new (name);

  g_hash_table_insert (app->profiles,
                       g_strdup (terminal_profile_get_property_string (profile, TERMINAL_PROFILE_NAME)),
                       profile /* adopts the refcount */);

  if (app->default_profile == NULL &&
      app->default_profile_id != NULL &&
      strcmp (app->default_profile_id,
              terminal_profile_get_property_string (profile, TERMINAL_PROFILE_NAME)) == 0)
    {
      /* We are the default profile */
      app->default_profile = profile;
      g_object_notify (G_OBJECT (app), "default-profile");
    }
  
  return profile;
}

static void
terminal_app_delete_profile (TerminalApp *app,
                             TerminalProfile *profile,
                             GtkWindow   *transient_parent)
{
  GHashTableIter iter;
  GSList *name_list;
  const char *name, *profile_name;
  char *gconf_dir;

  profile_name = terminal_profile_get_property_string (profile, TERMINAL_PROFILE_NAME);
  gconf_dir = gconf_concat_dir_and_key (CONF_PREFIX "/profiles", profile_name);

  name_list = NULL;
  g_hash_table_iter_init (&iter, app->profiles);
  while (g_hash_table_iter_next (&iter, (gpointer*) &name, NULL))
  {
    if (strcmp (name, profile_name) == 0)
      continue;

    name_list = g_slist_prepend (name_list, g_strdup (name));
  }

  gconf_client_set_list (app->conf,
                         CONF_GLOBAL_PREFIX"/profile_list",
                         GCONF_VALUE_STRING,
                         name_list,
                         NULL);

  g_slist_foreach (name_list, (GFunc) g_free, NULL);
  g_slist_free (name_list);

  /* And remove the profile directory */
  gconf_client_recursive_unset (app->conf, gconf_dir, GCONF_UNSET_INCLUDING_SCHEMA_NAMES, NULL);
  g_free (gconf_dir);
}

static GdkScreen*
find_screen_by_display_name (const char *display_name,
                             int         screen_number)
{
  GdkScreen *screen;
  
  /* --screen=screen_number overrides --display */
  
  screen = NULL;
  
  if (display_name == NULL)
    {
      if (screen_number >= 0)
        screen = gdk_display_get_screen (gdk_display_get_default (), screen_number);

      if (screen == NULL)
        screen = gdk_screen_get_default ();

      g_object_ref (G_OBJECT (screen));
    }
  else
    {
      GSList *displays;
      GSList *tmp;
      const char *period;
      GdkDisplay *display;
        
      period = strrchr (display_name, '.');
      if (period)
        {
          gulong n;
          char *end;
          
          errno = 0;
          end = NULL;
          n = g_ascii_strtoull (period + 1, &end, 0);
          if (errno == 0 && (period + 1) != end)
            screen_number = n;
        }
      
      displays = gdk_display_manager_list_displays (gdk_display_manager_get ());

      display = NULL;
      tmp = displays;
      while (tmp != NULL)
        {
          const char *this_name;

          display = tmp->data;
          this_name = gdk_display_get_name (display);
          
          /* compare without the screen number part */
          if (strncmp (this_name, display_name, period - display_name) == 0)
            break;

          tmp = tmp->next;
        }
      
      g_slist_free (displays);

      if (display == NULL)
        display = gdk_display_open (display_name); /* FIXME we never close displays */
      
      if (display != NULL)
        {
          if (screen_number >= 0)
            screen = gdk_display_get_screen (display, screen_number);
          
          if (screen == NULL)
            screen = gdk_display_get_default_screen (display);

          if (screen)
            g_object_ref (G_OBJECT (screen));
        }
    }

  if (screen == NULL)
    {
      screen = gdk_screen_get_default ();
      g_object_ref (G_OBJECT (screen));
    }
  
  return screen;
}

static void
terminal_app_profile_cell_data_func (GtkTreeViewColumn *tree_column,
                                     GtkCellRenderer *cell,
                                     GtkTreeModel *tree_model,
                                     GtkTreeIter *iter,
                                     gpointer data)
{
  TerminalProfile *profile;
  GValue value = { 0, };

  gtk_tree_model_get (tree_model, iter, (int) COL_PROFILE, &profile, (int) -1);

  g_value_init (&value, G_TYPE_STRING);
  g_object_get_property (G_OBJECT (profile), "visible-name", &value);
  g_object_set_property (G_OBJECT (cell), "text", &value);
  g_value_unset (&value);
}

static int
terminal_app_profile_sort_func (GtkTreeModel *model,
                                GtkTreeIter *a,
                                GtkTreeIter *b,
                                gpointer user_data)
{
  TerminalProfile *profile_a, *profile_b;
  int retval;

  gtk_tree_model_get (model, a, (int) COL_PROFILE, &profile_a, (int) -1);
  gtk_tree_model_get (model, b, (int) COL_PROFILE, &profile_b, (int) -1);

  retval = profiles_alphabetic_cmp (profile_a, profile_b);

  g_object_unref (profile_a);
  g_object_unref (profile_b);

  return retval;
}

static GtkTreeModel *
terminal_app_get_profile_liststore (TerminalApp *app,
                                    TerminalProfile *selected_profile,
                                    GtkTreeIter *selected_profile_iter,
                                    gboolean *selected_profile_iter_set)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GList *profiles, *l;
  TerminalProfile *default_profile;

  store = gtk_list_store_new (NUM_COLUMNS, TERMINAL_TYPE_PROFILE);

  *selected_profile_iter_set = FALSE;

  if (selected_profile &&
      _terminal_profile_get_forgotten (selected_profile))
    selected_profile = NULL;

  profiles = terminal_app_get_profile_list (app);
  default_profile = terminal_app_get_default_profile (app);

  for (l = profiles; l != NULL; l = l->next)
    {
      TerminalProfile *profile = TERMINAL_PROFILE (l->data);

      gtk_list_store_insert_with_values (store, &iter, 0,
                                         (int) COL_PROFILE, profile,
                                         (int) -1);

      if (selected_profile_iter && profile == selected_profile)
        {
          *selected_profile_iter = iter;
          *selected_profile_iter_set = TRUE;
        }
    }
  g_list_free (profiles);

  /* Now turn on sorting */
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
                                   COL_PROFILE,
                                   terminal_app_profile_sort_func,
                                   NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                        COL_PROFILE, GTK_SORT_ASCENDING);

  return GTK_TREE_MODEL (store);
}

static /* ref */ TerminalProfile*
profile_combo_box_get_selected (GtkWidget *widget)
{
  GtkComboBox *combo = GTK_COMBO_BOX (widget);
  TerminalProfile *profile = NULL;
  GtkTreeIter iter;

  if (gtk_combo_box_get_active_iter (combo, &iter))
    gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter,
                        (int) COL_PROFILE, &profile, (int) -1);

  return profile;
}

static void
profile_combo_box_refill (TerminalApp *app,
                          GtkWidget *widget)
{
  GtkComboBox *combo = GTK_COMBO_BOX (widget);
  GtkTreeIter iter;
  gboolean iter_set;
  TerminalProfile *selected_profile;
  GtkTreeModel *model;

  selected_profile = profile_combo_box_get_selected (widget);
  if (!selected_profile)
    {
      selected_profile = terminal_app_get_default_profile (app);
      if (selected_profile)
        g_object_ref (selected_profile);
    }

  model = terminal_app_get_profile_liststore (app,
                                              selected_profile,
                                              &iter,
                                              &iter_set);
  gtk_combo_box_set_model (combo, model);
  g_object_unref (model);

  if (iter_set)
    gtk_combo_box_set_active_iter (combo, &iter);

  if (selected_profile)
    g_object_unref (selected_profile);
}

static GtkWidget*
profile_combo_box_new (TerminalApp *app)
{
  GtkWidget *combo;
  GtkCellRenderer *renderer;
  
  combo = gtk_combo_box_new ();
  terminal_util_set_atk_name_description (combo, NULL, _("Click button to choose profile"));

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo), renderer,
                                      (GtkCellLayoutDataFunc) terminal_app_profile_cell_data_func,
                                      NULL, NULL);

  profile_combo_box_refill (app, combo);
  g_signal_connect (app, "profile-list-changed",
                    G_CALLBACK (profile_combo_box_refill), combo);
  
  return combo;
}

static void
profile_combo_box_changed_cb (GtkWidget *widget,
                              TerminalApp *app)
{
  TerminalProfile *profile;

  profile = profile_combo_box_get_selected (widget);
  if (!profile)
    return;

  gconf_client_set_string (app->conf,
                           CONF_GLOBAL_PREFIX "/default_profile",
                           terminal_profile_get_property_string (profile, TERMINAL_PROFILE_NAME),
                           NULL);

  /* Even though the gconf change notification does this, it happens too late.
   * In some cases, the default profile changes twice in quick succession,
   * and update_default_profile must be called in sync with those changes.
   */
  app->default_profile = profile;
 
  g_object_notify (G_OBJECT (app), "default-profile");

  g_object_unref (profile);
}

static void
profile_list_treeview_refill (TerminalApp *app,
                              GtkWidget *widget)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
  GtkTreeIter iter;
  gboolean iter_set;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  TerminalProfile *selected_profile = NULL;

  model = gtk_tree_view_get_model (tree_view);

  selection = gtk_tree_view_get_selection (tree_view);
  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    gtk_tree_model_get (model, &iter, (int) COL_PROFILE, &selected_profile, (int) -1);

  model = terminal_app_get_profile_liststore (terminal_app_get (),
                                              selected_profile,
                                              &iter,
                                              &iter_set);
  gtk_tree_view_set_model (tree_view, model);
  g_object_unref (model);

  if (!iter_set)
    iter_set = gtk_tree_model_get_iter_first (model, &iter);

  if (iter_set)
    gtk_tree_selection_select_iter (selection, &iter);

  if (selected_profile)
    g_object_unref (selected_profile);
}

static GtkWidget*
profile_list_treeview_create (TerminalApp *app)
{
  GtkWidget *tree_view;
  GtkTreeSelection *selection;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  tree_view = gtk_tree_view_new ();
  terminal_util_set_atk_name_description (tree_view, _("Profile list"), NULL);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree_view), FALSE);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
  gtk_tree_selection_set_mode (GTK_TREE_SELECTION (selection),
                               GTK_SELECTION_BROWSE);

  column = gtk_tree_view_column_new ();
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer, TRUE);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column), renderer,
                                      (GtkCellLayoutDataFunc) terminal_app_profile_cell_data_func,
                                      NULL, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
                               GTK_TREE_VIEW_COLUMN (column));

  return tree_view;
}

static void
profile_list_delete_confirm_response_cb (GtkWidget *dialog,
                                         int response,
                                         TerminalApp *app)
{
  TerminalProfile *profile;

  profile = TERMINAL_PROFILE (g_object_get_data (G_OBJECT (dialog), "profile"));
  g_assert (profile != NULL);
  
  if (response == GTK_RESPONSE_ACCEPT)
    terminal_app_delete_profile (app, profile,
                                 gtk_window_get_transient_for (GTK_WINDOW (dialog)));

  gtk_widget_destroy (dialog);
}

static void
profile_list_delete_button_clicked_cb (GtkWidget *button,
                                       GtkWidget *widget)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
  TerminalApp *app = terminal_app_get ();
  GtkTreeSelection *selection;
  GtkWidget *dialog;
  GtkTreeIter iter;
  GtkTreeModel *model;
  TerminalProfile *selected_profile;
  GtkWidget *transient_parent;

  model = gtk_tree_view_get_model (tree_view);
  selection = gtk_tree_view_get_selection (tree_view);

  if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
    return;
  
  gtk_tree_model_get (model, &iter, (int) COL_PROFILE, &selected_profile, (int) -1);

  transient_parent = gtk_widget_get_toplevel (widget);
  dialog = gtk_message_dialog_new (GTK_WINDOW (transient_parent),
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("Delete profile \"%s\"?"),
                                   terminal_profile_get_property_string (selected_profile, TERMINAL_PROFILE_VISIBLE_NAME));

  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                          GTK_STOCK_CANCEL,
                          GTK_RESPONSE_REJECT,
                          GTK_STOCK_DELETE,
                          GTK_RESPONSE_ACCEPT,
                          NULL);
  gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_ACCEPT,
                                           GTK_RESPONSE_REJECT,
                                           -1);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                   GTK_RESPONSE_ACCEPT);
 
  gtk_window_set_title (GTK_WINDOW (dialog), _("Delete Profile")); 
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  /* Transfer refcount of |selected_profile|, so no unref below */
  g_object_set_data_full (G_OBJECT (dialog), "profile", selected_profile, g_object_unref);
  
  g_signal_connect (dialog, "response",
                    G_CALLBACK (profile_list_delete_confirm_response_cb),
                    app);
  
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
profile_list_new_button_clicked_cb (GtkWidget   *button,
                                    gpointer data)
{
  TerminalApp *app;

  app = terminal_app_get ();
  terminal_app_new_profile (app, NULL, GTK_WINDOW (app->manage_profiles_dialog));
}

static void
profile_list_edit_button_clicked_cb (GtkWidget *button,
                                     GtkWidget *widget)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;
  TerminalProfile *selected_profile;
  TerminalApp *app;

  app = terminal_app_get ();

  model = gtk_tree_view_get_model (tree_view);
  selection = gtk_tree_view_get_selection (tree_view);

  if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
    return;
  
  gtk_tree_model_get (model, &iter, (int) COL_PROFILE, &selected_profile, (int) -1);
      
  terminal_app_edit_profile (app, selected_profile,
                             GTK_WINDOW (app->manage_profiles_dialog));
  g_object_unref (selected_profile);
}

static void
profile_list_row_activated_cb (GtkTreeView       *tree_view,
                               GtkTreePath       *path,
                               GtkTreeViewColumn *column,
                               gpointer data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  TerminalProfile *selected_profile;
  TerminalApp *app;

  app = terminal_app_get ();

  model = gtk_tree_view_get_model (tree_view);

  if (!gtk_tree_model_get_iter (model, &iter, path))
    return;
  
  gtk_tree_model_get (model, &iter, (int) COL_PROFILE, &selected_profile, (int) -1);
      
  terminal_app_edit_profile (app, selected_profile,
                             GTK_WINDOW (app->manage_profiles_dialog));
  g_object_unref (selected_profile);
}

static GList*
find_profile_link (GList      *profiles,
                   const char *name)
{
  GList *l;

  for (l = profiles; l != NULL; l = l->next)
    {
      const char *profile_name;

      profile_name = terminal_profile_get_property_string (TERMINAL_PROFILE (l->data), TERMINAL_PROFILE_NAME);
      if (profile_name && strcmp (profile_name, name) == 0)
        break;
    }

  return l;
}

static void
terminal_app_profile_list_notify_cb (GConfClient *conf,
                                     guint        cnxn_id,
                                     GConfEntry  *entry,
                                     gpointer     user_data)
{
  TerminalApp *app = TERMINAL_APP (user_data);
  GObject *object = G_OBJECT (app);
  GConfValue *val;
  GSList *value_list, *sl;
  GList *profiles_to_delete, *l;
  gboolean need_new_default;
  TerminalProfile *fallback;
  
  g_object_freeze_notify (object);

  profiles_to_delete = terminal_app_get_profile_list (app);

  val = gconf_entry_get_value (entry);
  if (val == NULL ||
      val->type != GCONF_VALUE_LIST ||
      gconf_value_get_list_type (val) != GCONF_VALUE_STRING)
    goto ensure_one_profile;
    
  value_list = gconf_value_get_list (val);

  /* Add any new ones */
  for (sl = value_list; sl != NULL; sl = sl->next)
    {
      GConfValue *listvalue = (GConfValue *) (sl->data);
      const char *profile_name;
      GList *link;

      profile_name = gconf_value_get_string (listvalue);
      if (!profile_name)
        continue;

      link = find_profile_link (profiles_to_delete, profile_name);
      if (link)
        /* make profiles_to_delete point to profiles we didn't find in the list */
        profiles_to_delete = g_list_delete_link (profiles_to_delete, link);
      else
        terminal_app_create_profile (app, profile_name);
    }

ensure_one_profile:

  fallback = NULL;
  if (terminal_app_get_profile_count (app) == 0 ||
      terminal_app_get_profile_count (app) <= g_list_length (profiles_to_delete))
    {
      /* We are going to run out, so create the fallback
       * to be sure we always have one. Must be done
       * here before we emit "forgotten" signals so that
       * screens have a profile to fall back to.
       *
       * If the profile with the FALLBACK_ID already exists,
       * we aren't allowed to delete it, unless at least one
       * other profile will still exist. And if you delete
       * all profiles, the FALLBACK_ID profile returns as
       * the living dead.
       */
      fallback = terminal_app_get_profile_by_name (app, FALLBACK_PROFILE_ID);
      if (fallback == NULL)
        fallback = terminal_app_create_profile (app, FALLBACK_PROFILE_ID);
      g_assert (fallback != NULL);
    }
  
  /* Forget no-longer-existing profiles */
  need_new_default = FALSE;
  for (l = profiles_to_delete; l != NULL; l = l->next)
    {
      TerminalProfile *profile = TERMINAL_PROFILE (l->data);
      const char *name;

      name = terminal_profile_get_property_string (profile, TERMINAL_PROFILE_NAME);
      if (strcmp (name, FALLBACK_PROFILE_ID) == 0)
        continue;

      if (profile == app->default_profile)
        {
          app->default_profile = NULL;
          need_new_default = TRUE;
        }

      g_object_add_weak_pointer (G_OBJECT (profile), (gpointer*)&profile);

      _terminal_profile_forget (profile);
      g_hash_table_remove (app->profiles, name);

      /* |profile| should now be dead */
      g_assert (profile == NULL);
    }
  g_list_free (profiles_to_delete);
  
  if (need_new_default)
    {
      TerminalProfile *new_default;

      new_default = terminal_app_get_profile_by_name (app, FALLBACK_PROFILE_ID);
      if (new_default == NULL)
        {
          GHashTableIter iter;

          g_hash_table_iter_init (&iter, app->profiles);
          if (!g_hash_table_iter_next (&iter, NULL, (gpointer *) &new_default))
            /* shouldn't really happen ever, but just to be safe */
            new_default = terminal_app_create_profile (app, FALLBACK_PROFILE_ID); 
        }
      g_assert (new_default != NULL);

      app->default_profile = new_default;
    
      g_object_notify (object, "default-profile");
    }

  g_assert (terminal_app_get_profile_count (app) > 0);

  g_signal_emit (app, signals[PROFILE_LIST_CHANGED], 0);

  g_object_thaw_notify (object);
}

static void
terminal_app_default_profile_notify_cb (GConfClient *client,
                                        guint        cnxn_id,
                                        GConfEntry  *entry,
                                        gpointer     user_data)
{
  TerminalApp *app = TERMINAL_APP (user_data);
  GConfValue *val;
  const char *name;
  
  app->default_profile_locked = !gconf_entry_get_is_writable (entry);

  val = gconf_entry_get_value (entry);  
  if (val == NULL ||
      val->type != GCONF_VALUE_STRING)
    return;
  
  name = gconf_value_get_string (val);
  if (!name)
    name = FALLBACK_PROFILE_ID; /* FIXMEchpe? */

  g_free (app->default_profile_id);
  app->default_profile_id = g_strdup (name);

  app->default_profile = terminal_app_get_profile_by_name (app, name);

  g_object_notify (G_OBJECT (app), "default-profile");
}

static void
terminal_app_system_font_notify_cb (GConfClient *client,
                                    guint        cnxn_id,
                                    GConfEntry  *entry,
                                    gpointer     user_data)
{
  TerminalApp *app = TERMINAL_APP (user_data);
  GConfValue *gconf_value;
  const char *font;
  PangoFontDescription *font_desc;

  if (strcmp (gconf_entry_get_key (entry), MONOSPACE_FONT_KEY) != 0)
    return;

  gconf_value = gconf_entry_get_value (entry);
  if (!gconf_value || gconf_value->type != GCONF_VALUE_STRING)
    return;

  font = gconf_value_get_string (gconf_value);
  if (!font)
    return;

  font_desc = pango_font_description_from_string (font);
  if (app->system_font_desc &&
      pango_font_description_equal (app->system_font_desc, font_desc))
    {
      pango_font_description_free (font_desc);
      return;
    }

  if (app->system_font_desc)
    pango_font_description_free (app->system_font_desc);

  app->system_font_desc = font_desc;

  g_object_notify (G_OBJECT (app), "system-font");
}

static void
new_profile_response_cb (GtkWidget *new_profile_dialog,
                         int        response_id,
                         TerminalApp *app)
{
  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      GtkWidget *name_entry;
      char *name;
      const char *new_profile_name;
      GtkWidget *base_option_menu;
      TerminalProfile *base_profile = NULL;
      TerminalProfile *new_profile;
      GList *profiles;
      GList *tmp;
      GtkWindow *transient_parent;
      GtkWidget *confirm_dialog;
      gint retval;
      GSList *list;

      base_option_menu = g_object_get_data (G_OBJECT (new_profile_dialog), "base_option_menu");
      base_profile = profile_combo_box_get_selected (base_option_menu);
      if (!base_profile)
        base_profile = terminal_app_get_default_profile (app);
      if (!base_profile)
        return; /* shouldn't happen ever though */

      name_entry = g_object_get_data (G_OBJECT (new_profile_dialog), "name_entry");
      name = gtk_editable_get_chars (GTK_EDITABLE (name_entry), 0, -1);
      g_strstrip (name); /* name will be non empty after stripping */
      
      profiles = terminal_app_get_profile_list (app);
      for (tmp = profiles; tmp != NULL; tmp = tmp->next)
        {
          TerminalProfile *profile = tmp->data;
          const char *visible_name;

          visible_name = terminal_profile_get_property_string (profile, TERMINAL_PROFILE_VISIBLE_NAME);

          if (visible_name && strcmp (name, visible_name) == 0)
            break;
        }
      if (tmp)
        {
          confirm_dialog = gtk_message_dialog_new (GTK_WINDOW (new_profile_dialog), 
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_QUESTION, 
                                                   GTK_BUTTONS_YES_NO, 
                                                   _("You already have a profile called \"%s\". Do you want to create another profile with the same name?"), name);
          retval = gtk_dialog_run (GTK_DIALOG (confirm_dialog));
          gtk_widget_destroy (confirm_dialog);
          if (retval == GTK_RESPONSE_NO)   
            goto cleanup;
        }
      g_list_free (profiles);

      transient_parent = gtk_window_get_transient_for (GTK_WINDOW (new_profile_dialog));
      
//       gtk_widget_destroy (new_profile_dialog);

      new_profile = _terminal_profile_clone (base_profile, name);
      new_profile_name = terminal_profile_get_property_string (new_profile, TERMINAL_PROFILE_NAME);
      g_hash_table_insert (app->profiles,
                           g_strdup (new_profile_name),
                           new_profile /* adopts the refcount */);

      /* And now save the list to gconf */
      list = gconf_client_get_list (app->conf,
                                    CONF_GLOBAL_PREFIX"/profile_list",
                                    GCONF_VALUE_STRING,
                                    NULL);
      list = g_slist_append (list, g_strdup (new_profile_name));
      gconf_client_set_list (app->conf,
                             CONF_GLOBAL_PREFIX"/profile_list",
                             GCONF_VALUE_STRING,
                             list,
                             NULL);

      terminal_profile_edit (new_profile, transient_parent);

    cleanup:
      g_free (name);
    }
      
  gtk_widget_destroy (new_profile_dialog);
}

static void
new_profile_dialog_destroy_cb (GtkWidget *new_profile_dialog,
                               TerminalApp *app)
{
  GtkWidget *combo;
      
  combo = g_object_get_data (G_OBJECT (new_profile_dialog), "base_option_menu");
  g_signal_handlers_disconnect_by_func (app, G_CALLBACK (profile_combo_box_refill), combo);

  app->new_profile_dialog = NULL;
}

static void
new_profile_name_entry_changed_cb (GtkEntry *entry,
                                   GtkDialog *dialog)
{
  const char *name;

  name = gtk_entry_get_text (entry);

  /* make the create button sensitive only if something other than space has been set */
  while (*name != '\0' && g_ascii_isspace (*name))
    ++name;

  gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_ACCEPT, name[0] != '\0');
}

void
terminal_app_new_profile (TerminalApp     *app,
                          TerminalProfile *default_base_profile,
                          GtkWindow       *transient_parent)
{
  if (app->new_profile_dialog == NULL)
    {
      GtkWidget *create_button, *table, *name_label, *name_entry, *base_label, *combo;

      if (!terminal_util_load_builder_file ("profile-new-dialog.ui",
                                            "new-profile-dialog", &app->new_profile_dialog,
                                            "new-profile-create-button", &create_button,
                                            "new-profile-table", &table,
                                            "new-profile-name-label", &name_label,
                                            "new-profile-name-entry", &name_entry,
                                            "new-profile-base-label", &base_label,
                                            NULL))
        return;

      g_signal_connect (G_OBJECT (app->new_profile_dialog), "response", G_CALLBACK (new_profile_response_cb), app);
      g_signal_connect (app->new_profile_dialog, "destroy", G_CALLBACK (new_profile_dialog_destroy_cb), app);

      g_object_set_data (G_OBJECT (app->new_profile_dialog), "create_button", create_button);
      gtk_widget_set_sensitive (create_button, FALSE);

      /* the name entry */
      g_object_set_data (G_OBJECT (app->new_profile_dialog), "name_entry", name_entry);
      g_signal_connect (name_entry, "changed", G_CALLBACK (new_profile_name_entry_changed_cb), app->new_profile_dialog);
      gtk_entry_set_activates_default (GTK_ENTRY (name_entry), TRUE);
      gtk_widget_grab_focus (name_entry);

      gtk_label_set_mnemonic_widget (GTK_LABEL (name_label), name_entry);

      /* the base profile option menu */
      combo = profile_combo_box_new (app);
      gtk_table_attach_defaults (GTK_TABLE (table), combo, 1, 2, 1, 2);
      g_object_set_data (G_OBJECT (app->new_profile_dialog), "base_option_menu", combo);
      terminal_util_set_atk_name_description (combo, NULL, _("Choose base profile"));

      gtk_label_set_mnemonic_widget (GTK_LABEL (base_label), combo);

      gtk_dialog_set_default_response (GTK_DIALOG (app->new_profile_dialog), GTK_RESPONSE_ACCEPT);
      gtk_dialog_set_response_sensitive (GTK_DIALOG (app->new_profile_dialog), GTK_RESPONSE_ACCEPT, FALSE);
    }

  gtk_window_set_transient_for (GTK_WINDOW (app->new_profile_dialog),
                                transient_parent);

  gtk_window_present (GTK_WINDOW (app->new_profile_dialog));
}

static void
profile_list_selection_changed_cb (GtkTreeSelection *selection,
                                   TerminalApp *app)
{
  gboolean selected;

  selected = gtk_tree_selection_get_selected (selection, NULL, NULL);

  gtk_widget_set_sensitive (app->manage_profiles_edit_button, selected);
  gtk_widget_set_sensitive (app->manage_profiles_delete_button,
                            selected &&
                            terminal_app_get_profile_count (app) > 1);
}

static void
profile_list_response_cb (GtkWidget *dialog,
                          int        id,
                          TerminalApp *app)
{
  g_assert (app->manage_profiles_dialog == dialog);
  
  if (id == GTK_RESPONSE_HELP)
    {
      terminal_util_show_help ("gnome-terminal-manage-profiles", GTK_WINDOW (dialog));
      return;
    }
    
  gtk_widget_destroy (dialog);
}

static void
profile_list_destroyed_cb (GtkWidget   *manage_profiles_dialog,
                           TerminalApp *app)
{
  g_signal_handlers_disconnect_by_func (app, G_CALLBACK (profile_list_treeview_refill), app->manage_profiles_list);
  g_signal_handlers_disconnect_by_func (app, G_CALLBACK (profile_combo_box_refill), app->manage_profiles_default_menu);

  app->manage_profiles_dialog = NULL;
  app->manage_profiles_list = NULL;
  app->manage_profiles_new_button = NULL;
  app->manage_profiles_edit_button = NULL;
  app->manage_profiles_delete_button = NULL;
  app->manage_profiles_default_menu = NULL;
}

void
terminal_app_manage_profiles (TerminalApp     *app,
                              GtkWindow       *transient_parent)
{
  GObject *dialog;
  GObject *tree_view_container, *new_button, *edit_button, *remove_button;
  GObject *default_hbox, *default_label;
  GtkTreeSelection *selection;

  if (app->manage_profiles_dialog)
    {
      gtk_window_set_transient_for (GTK_WINDOW (app->manage_profiles_dialog), transient_parent);
      gtk_window_present (GTK_WINDOW (app->manage_profiles_dialog));
      return;
    }

  if (!terminal_util_load_builder_file ("profile-manager.ui",
                                        "profile-manager", &dialog,
                                        "profiles-treeview-container", &tree_view_container,
                                        "new-profile-button", &new_button,
                                        "edit-profile-button", &edit_button,
                                        "delete-profile-button", &remove_button,
                                        "default-profile-hbox", &default_hbox,
                                        "default-profile-label", &default_label,
                                        NULL))
    return;

  app->manage_profiles_dialog = GTK_WIDGET (dialog);
  app->manage_profiles_new_button = GTK_WIDGET (new_button);
  app->manage_profiles_edit_button = GTK_WIDGET (edit_button);
  app->manage_profiles_delete_button  = GTK_WIDGET (remove_button);

  g_signal_connect (dialog, "response", G_CALLBACK (profile_list_response_cb), app);
  g_signal_connect (dialog, "destroy", G_CALLBACK (profile_list_destroyed_cb), app);

  app->manage_profiles_list = profile_list_treeview_create (app);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (app->manage_profiles_list));
  g_signal_connect (selection, "changed", G_CALLBACK (profile_list_selection_changed_cb), app);

  profile_list_treeview_refill (app, app->manage_profiles_list);
  g_signal_connect (app, "profile-list-changed",
                    G_CALLBACK (profile_list_treeview_refill), app->manage_profiles_list);

  g_signal_connect (app->manage_profiles_list, "row-activated",
                    G_CALLBACK (profile_list_row_activated_cb), app);

  gtk_container_add (GTK_CONTAINER (tree_view_container), app->manage_profiles_list);
  gtk_widget_show (app->manage_profiles_list);

  g_signal_connect (new_button, "clicked",
                    G_CALLBACK (profile_list_new_button_clicked_cb),
                    app->manage_profiles_list);   
  g_signal_connect (edit_button, "clicked",
                    G_CALLBACK (profile_list_edit_button_clicked_cb),
                    app->manage_profiles_list);
  g_signal_connect (remove_button, "clicked",
                    G_CALLBACK (profile_list_delete_button_clicked_cb),
                    app->manage_profiles_list);
            
  app->manage_profiles_default_menu = profile_combo_box_new (app);
  g_signal_connect (app->manage_profiles_default_menu, "changed",
                    G_CALLBACK (profile_combo_box_changed_cb), app);

  gtk_box_pack_start (GTK_BOX (default_hbox), app->manage_profiles_default_menu, FALSE, FALSE, 0);
  gtk_widget_show (app->manage_profiles_default_menu);

  gtk_label_set_mnemonic_widget (GTK_LABEL (default_label), app->manage_profiles_default_menu);

  gtk_widget_grab_focus (app->manage_profiles_list);

  gtk_window_set_transient_for (GTK_WINDOW (app->manage_profiles_dialog),
                                transient_parent);

  gtk_window_present (GTK_WINDOW (app->manage_profiles_dialog));
}

static void
terminal_app_get_clone_command (TerminalApp *app,
                                int         *argcp,
                                char      ***argvp)
{
  GList *tmp;
  GPtrArray* args;
  
  args = g_ptr_array_new ();

   g_ptr_array_add (args, g_strdup (EXECUTABLE_NAME));

  if (!app->use_factory)
    {
       g_ptr_array_add (args, g_strdup ("--disable-factory"));
    }

  tmp = app->windows;
  while (tmp != NULL)
    {
      GList *tabs;
      GList *tmp2;
      TerminalWindow *window = tmp->data;
      TerminalScreen *active_screen;

      active_screen = terminal_window_get_active (window);
      
      tabs = terminal_window_list_screens (window);

      tmp2 = tabs;
      while (tmp2 != NULL)
        {
          TerminalScreen *screen = tmp2->data;
          const char *profile_id;
          const char **override_command;
          const char *title;
          double zoom;
          
          profile_id = terminal_profile_get_property_string (terminal_screen_get_profile (screen),
                                                             TERMINAL_PROFILE_NAME);
          
          if (tmp2 == tabs)
            {
               g_ptr_array_add (args, g_strdup_printf ("--window-with-profile-internal-id=%s",
                                                     profile_id));
              if (terminal_window_get_menubar_visible (window))
                 g_ptr_array_add (args, g_strdup ("--show-menubar"));
              else
                 g_ptr_array_add (args, g_strdup ("--hide-menubar"));

               g_ptr_array_add (args, g_strdup_printf ("--role=%s",
                                                     gtk_window_get_role (GTK_WINDOW (window))));
            }
          else
            {
               g_ptr_array_add (args, g_strdup_printf ("--tab-with-profile-internal-id=%s",
                                                     profile_id));
            }

          if (screen == active_screen)
            {
              int w, h, x, y;

              /* FIXME saving the geometry is not great :-/ */
               g_ptr_array_add (args, g_strdup ("--active"));

               g_ptr_array_add (args, g_strdup ("--geometry"));

              terminal_screen_get_size (screen, &w, &h);
              gtk_window_get_position (GTK_WINDOW (window), &x, &y);
              g_ptr_array_add (args, g_strdup_printf ("%dx%d+%d+%d", w, h, x, y));
            }

          override_command = terminal_screen_get_override_command (screen);
          if (override_command)
            {
              char *flattened;

               g_ptr_array_add (args, g_strdup ("--command"));
              
              flattened = g_strjoinv (" ", (char**) override_command);
               g_ptr_array_add (args, flattened);
            }

          title = terminal_screen_get_dynamic_title (screen);
          if (title)
            {
               g_ptr_array_add (args, g_strdup ("--title"));
               g_ptr_array_add (args, g_strdup (title));
            }

          {
            const char *dir;

            dir = terminal_screen_get_working_dir (screen);

            if (dir != NULL && *dir != '\0') /* should always be TRUE anyhow */
              {
                 g_ptr_array_add (args, g_strdup ("--working-directory"));
                g_ptr_array_add (args, g_strdup (dir));
              }
          }

          zoom = terminal_screen_get_font_scale (screen);
          if (zoom < -1e-6 || zoom > 1e-6) /* if not 1.0 */
            {
              char buf[G_ASCII_DTOSTR_BUF_SIZE];

              g_ascii_dtostr (buf, sizeof (buf), zoom);
              
              g_ptr_array_add (args, g_strdup ("--zoom"));
              g_ptr_array_add (args, g_strdup (buf));
            }
          
          tmp2 = tmp2->next;
        }
      
      g_list_free (tabs);
      
      tmp = tmp->next;
    }

  /* final NULL */
  g_ptr_array_add (args, NULL);

  *argcp = args->len;
  *argvp = (char**) g_ptr_array_free (args, FALSE);
}

static gboolean
terminal_app_save_yourself_cb (GnomeClient        *client,
                               gint                phase,
                               GnomeSaveStyle      save_style,
                               gboolean            shutdown,
                               GnomeInteractStyle  interact_style,
                               gboolean            fast,
                               void               *data)
{
  char **clone_command;
  TerminalApp *app;
  int argc;
#ifdef GNOME_ENABLE_DEBUG
  int i;
#endif

  app = data;
  
  terminal_app_get_clone_command (app, &argc, &clone_command);

  /* GnomeClient builds the clone command from the restart command */
  gnome_client_set_restart_command (client, argc, clone_command);

#ifdef GNOME_ENABLE_DEBUG
  /* Debug spew */
  g_print ("Saving session: ");
  i = 0;
  while (clone_command[i])
    {
      g_print ("%s ", clone_command[i]);
      ++i;
    }
  g_print ("\n");
#endif

  g_strfreev (clone_command);
  
  /* success */
  return TRUE;
}

static void
terminal_app_client_die_cb (GnomeClient *client,
                            TerminalApp *app)
{
  g_signal_emit (app, signals[QUIT], 0);
}

/* Class implementation */

G_DEFINE_TYPE (TerminalApp, terminal_app, G_TYPE_OBJECT)

static void
terminal_app_init (TerminalApp *app)
{
  GnomeClient *sm_client;

  global_app = app;

  app->profiles = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  
  app->conf = gconf_client_get_default ();

  gconf_client_add_dir (app->conf, CONF_GLOBAL_PREFIX,
                        GCONF_CLIENT_PRELOAD_ONELEVEL,
                        NULL);
  gconf_client_add_dir (app->conf, MONOSPACE_FONT_DIR,
                        GCONF_CLIENT_PRELOAD_ONELEVEL,
                        NULL);
  
  app->profile_list_notify_id =
    gconf_client_notify_add (app->conf, PROFILE_LIST_KEY,
                             terminal_app_profile_list_notify_cb,
                             app, NULL, NULL);

  app->default_profile_notify_id =
    gconf_client_notify_add (app->conf,
                             DEFAULT_PROFILE_KEY,
                             terminal_app_default_profile_notify_cb,
                             app, NULL, NULL);

  app->system_font_notify_id =
    gconf_client_notify_add (app->conf,
                             MONOSPACE_FONT_KEY,
                             terminal_app_system_font_notify_cb,
                             app, NULL, NULL);

  gconf_client_notify (app->conf, PROFILE_LIST_KEY);
  gconf_client_notify (app->conf, DEFAULT_PROFILE_KEY);
  gconf_client_notify (app->conf, MONOSPACE_FONT_KEY);

  terminal_accels_init ();
  terminal_encoding_init ();
  
  sm_client = gnome_master_client ();
  g_signal_connect (sm_client,
                    "save_yourself",
                    G_CALLBACK (terminal_app_save_yourself_cb),
                    terminal_app_get ());

  g_signal_connect (sm_client, "die",
                    G_CALLBACK (terminal_app_client_die_cb),
                    NULL);
}

static void
terminal_app_finalize (GObject *object)
{
  TerminalApp *app = TERMINAL_APP (object);

  if (app->profile_list_notify_id != 0)
    gconf_client_notify_remove (app->conf, app->profile_list_notify_id);
  if (app->default_profile_notify_id != 0)
    gconf_client_notify_remove (app->conf, app->default_profile_notify_id);
  if (app->system_font_notify_id != 0)
    gconf_client_notify_remove (app->conf, app->system_font_notify_id);

  gconf_client_remove_dir (app->conf, CONF_GLOBAL_PREFIX, NULL);
  gconf_client_remove_dir (app->conf, MONOSPACE_FONT_DIR, NULL);

  g_object_unref (app->conf);

  g_free (app->default_profile_id);

  g_hash_table_destroy (app->profiles);

  if (app->system_font_desc)
    pango_font_description_free (app->system_font_desc);

  G_OBJECT_CLASS (terminal_app_parent_class)->finalize (object);

  global_app = NULL;
}

static void
terminal_app_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  TerminalApp *app = TERMINAL_APP (object);

  switch (prop_id)
    {
      case PROP_SYSTEM_FONT:
        if (app->system_font_desc)
          g_value_set_boxed (value, app->system_font_desc);
        else
          g_value_take_boxed (value, pango_font_description_from_string (DEFAULT_MONOSPACE_FONT));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
terminal_app_class_init (TerminalAppClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = terminal_app_finalize;
  object_class->get_property = terminal_app_get_property;

  signals[QUIT] =
    g_signal_new (I_("quit"),
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (TerminalAppClass, quit),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[PROFILE_LIST_CHANGED] =
    g_signal_new (I_("profile-list-changed"),
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (TerminalAppClass, profile_list_changed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  g_object_class_install_property
    (object_class,
     PROP_SYSTEM_FONT,
     g_param_spec_boxed ("system-font", NULL, NULL,
                         PANGO_TYPE_FONT_DESCRIPTION,
                         G_PARAM_READABLE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  /* FIMXEchpe make rw prop */
  g_object_class_install_property
    (object_class,
     PROP_DEFAULT_PROFILE,
     g_param_spec_object ("default-profile", NULL, NULL,
                          TERMINAL_TYPE_PROFILE,
                          G_PARAM_READABLE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));
}

/* Public API */

void
terminal_app_initialize (gboolean use_factory)
{
  g_assert (global_app == NULL);
  g_object_new (TERMINAL_TYPE_APP, NULL);
  g_assert (global_app != NULL);

  global_app->use_factory = use_factory;
}

void
terminal_app_shutdown (void)
{
  g_assert (global_app != NULL);
  g_object_unref (global_app);
}

TerminalApp*
terminal_app_get (void)
{
  g_assert (global_app != NULL);
  return global_app;
}

TerminalWindow *
terminal_app_new_window (TerminalApp *app,
                         const char *role,
                         const char *startup_id,
                         const char *display_name,
                         int screen_number)
{
  GdkScreen *gdk_screen;
  TerminalWindow *window;
  
  window = terminal_window_new ();
  
  app->windows = g_list_append (app->windows, window);
  g_signal_connect (window, "destroy",
                    G_CALLBACK (terminal_window_destroyed), app);

  gdk_screen = find_screen_by_display_name (display_name, screen_number);
  if (gdk_screen != NULL)
    {
      gtk_window_set_screen (GTK_WINDOW (window), gdk_screen);
      g_object_unref (G_OBJECT (gdk_screen));
    }

  if (startup_id != NULL)
    terminal_window_set_startup_id (window, startup_id);
  
  if (role == NULL)
    terminal_util_set_unique_role (GTK_WINDOW (window), "gnome-terminal");
  else
    gtk_window_set_role (GTK_WINDOW (window), role);

  return window;
}

void
terminal_app_new_terminal (TerminalApp     *app,
                           TerminalProfile *profile,
                           TerminalWindow  *window,
                           TerminalScreen  *screen,
                           gboolean         force_menubar_state,
                           gboolean         forced_menubar_state,
                           gboolean         start_fullscreen,
                           char           **override_command,
                           const char      *geometry,
                           const char      *title,
                           const char      *working_dir,
                           const char      *role,
                           double           zoom,
                           const char      *startup_id,
                           const char      *display_name,
                           int              screen_number)
{
  gboolean window_created;
  gboolean screen_created;
  
  g_return_if_fail (profile);

  window_created = FALSE;
  if (window == NULL)
    {
      window = terminal_app_new_window (app, role, startup_id, display_name, screen_number);
      window_created = TRUE;
    }

  if (force_menubar_state)
    {
      terminal_window_set_menubar_visible (window, forced_menubar_state);
    }

  screen_created = FALSE;
  if (screen == NULL)
    {  
      screen_created = TRUE;
      screen = terminal_screen_new ();
      
      terminal_screen_set_profile (screen, profile);
    
      if (title)
        {
          terminal_screen_set_title (screen, title);
          terminal_screen_set_dynamic_title (screen, title, FALSE);
          terminal_screen_set_dynamic_icon_title (screen, title, FALSE);
        }

      if (working_dir)
        terminal_screen_set_working_dir (screen, working_dir);
      
      if (override_command)    
        terminal_screen_set_override_command (screen, override_command);
    
      terminal_screen_set_font_scale (screen, zoom);
      terminal_screen_set_font (screen);
    
      terminal_window_add_screen (window, screen, -1);

//       terminal_screen_reread_profile (screen); FIXMEchpe this should be obsolete by the set_profile above
    
      terminal_window_set_active (window, screen);
      gtk_widget_grab_focus (GTK_WIDGET (screen));
    }
  else
    {
      TerminalWindow *source_window;

      source_window = terminal_screen_get_window (screen);
      if (source_window)
        {
          g_object_ref_sink (screen);
          terminal_window_remove_screen (source_window, screen);
          terminal_window_add_screen (window, screen, -1);
          g_object_unref (screen);

          terminal_window_set_active (window, screen);
          gtk_widget_grab_focus (GTK_WIDGET (screen));
        }
    }

  if (geometry)
    {
      if (!gtk_window_parse_geometry (GTK_WINDOW (window),
                                      geometry))
        g_printerr (_("Invalid geometry string \"%s\"\n"),
                    geometry);
    }

  if (start_fullscreen)
    {
      gtk_window_fullscreen (GTK_WINDOW (window));
    }

  /* don't present on new tab, or we can accidentally make the
   * terminal jump workspaces.
   * http://bugzilla.gnome.org/show_bug.cgi?id=78253
   */
  if (window_created)
    gtk_window_present (GTK_WINDOW (window));

  if (screen_created)
    terminal_screen_launch_child (screen);
}


void
terminal_app_edit_profile (TerminalApp     *app,
                           TerminalProfile *profile,
                           GtkWindow       *transient_parent)
{
  terminal_profile_edit (profile, transient_parent);
}

void
terminal_app_edit_keybindings (TerminalApp     *app,
                               GtkWindow       *transient_parent)
{
  terminal_edit_keys_dialog_show (transient_parent);
}

void
terminal_app_edit_encodings (TerminalApp     *app,
                             GtkWindow       *transient_parent)
{
  GtkWindow *old_transient_parent;

  if (app->edit_encodings_dialog == NULL)
    {      
      old_transient_parent = NULL;      

      app->edit_encodings_dialog = terminal_encoding_dialog_new (transient_parent);
      if (app->edit_encodings_dialog == NULL)
        return;
      
      g_signal_connect (G_OBJECT (app->edit_encodings_dialog),
                        "destroy",
                        G_CALLBACK (gtk_widget_destroyed),
                        &(app->edit_encodings_dialog));
    }
  else 
    {
      gtk_window_set_transient_for (GTK_WINDOW (app->edit_encodings_dialog),
                                    transient_parent);
    }
  
  gtk_window_present (GTK_WINDOW (app->edit_encodings_dialog));
}

TerminalWindow *
terminal_app_get_current_window (TerminalApp *app)
{
  /* FIXMEchpe take focus into account! */
  return g_list_last (app->windows)->data;
}

/* FIXMEchpe: make this list contain ref'd objects */
/**
 * terminal_profile_get_list:
 *
 * Returns: a #GList containing all #TerminalProfile objects.
 *   The content of the list is owned by the backend and
 *   should not be modified or freed. Use g_list_free() when done
 *   using the list.
 */
GList*
terminal_app_get_profile_list (TerminalApp *app)
{
  g_return_val_if_fail (TERMINAL_IS_APP (app), NULL);

  return g_list_sort (g_hash_table_get_values (app->profiles), profiles_alphabetic_cmp);
}

guint
terminal_app_get_profile_count (TerminalApp *app)
{
  g_return_val_if_fail (TERMINAL_IS_APP (app), 0);

  return g_hash_table_size (app->profiles);
}

TerminalProfile*
terminal_app_get_profile_by_name (TerminalApp *app,
                                  const char *name)
{
  g_return_val_if_fail (TERMINAL_IS_APP (app), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  return g_hash_table_lookup (app->profiles, name);
}

TerminalProfile*
terminal_app_get_profile_by_visible_name (TerminalApp *app,
                                          const char *name)
{
  LookupInfo info;

  g_return_val_if_fail (TERMINAL_IS_APP (app), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  info.result = NULL;
  info.target = name;

  g_hash_table_foreach (app->profiles,
                        profiles_lookup_by_visible_name_foreach,
                        &info);
  return info.result;
}

TerminalProfile*
terminal_app_get_default_profile (TerminalApp *app)
{
  g_return_val_if_fail (TERMINAL_IS_APP (app), NULL);

  return app->default_profile;
}

TerminalProfile*
terminal_app_get_profile_for_new_term (TerminalApp *app,
                                       TerminalProfile *current)
{
  GList *list;
  TerminalProfile *profile;

  g_return_val_if_fail (TERMINAL_IS_APP (app), NULL);

  if (current)
    return current;
  
  if (app->default_profile)
    return app->default_profile;	

  list = terminal_app_get_profile_list (app);
  if (list)
    profile = list->data;
  else
    profile = NULL;

  g_list_free (list);

  return profile;
}