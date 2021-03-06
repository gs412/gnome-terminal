/*
 * Copyright © 2001 Havoc Pennington
 * Copyright © 2002 Red Hat, Inc.
 * Copyright © 2008, 2010, 2011, 2012 Christian Persch
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

#include <config.h>

#include "terminal-notebook.h"

#include <gtk/gtk.h>

#include "terminal-debug.h"
#include "terminal-intl.h"
#include "terminal-mdi-container.h"
#include "terminal-screen-container.h"
#include "terminal-tab-label.h"

#define TERMINAL_NOTEBOOK_GET_PRIVATE(notebook)(G_TYPE_INSTANCE_GET_PRIVATE ((notebook), TERMINAL_TYPE_NOTEBOOK, TerminalNotebookPrivate))

struct _TerminalNotebookPrivate
{
  TerminalScreen *active_screen;
};

enum
{
  PROP_0,
  PROP_ACTIVE_SCREEN
};

/* helper functions */

static void
update_tab_visibility (GtkNotebook *notebook,
                       int change)
{
  gboolean show_tabs;
  guint num;

  num = gtk_notebook_get_n_pages (notebook);
  show_tabs = (num + change) > 1;
  gtk_notebook_set_show_tabs (notebook, show_tabs);
}

static void
close_button_clicked_cb (TerminalTabLabel *tab_label,
                         TerminalNotebook *notebook)
{
  TerminalScreen *screen;

  screen = terminal_tab_label_get_screen (tab_label);

  g_signal_emit_by_name (notebook, "screen-close-request", screen);
}


/* TerminalMdiContainer impl */

static void
terminal_notebook_add_screen (TerminalMdiContainer *container,
                              TerminalScreen *screen)
{
  TerminalNotebook *notebook = TERMINAL_NOTEBOOK (container);
  GtkNotebook *gtk_notebook = GTK_NOTEBOOK (notebook);
  GtkWidget *screen_container, *tab_label;
  const int position = -1;

  g_warn_if_fail (gtk_widget_get_parent (GTK_WIDGET (screen)) == NULL);

  screen_container = terminal_screen_container_new (screen);
  gtk_widget_show (screen_container);

  update_tab_visibility (gtk_notebook, +1);

  tab_label = terminal_tab_label_new (screen);
  g_signal_connect (tab_label, "close-button-clicked",
                    G_CALLBACK (close_button_clicked_cb), notebook);

  gtk_notebook_insert_page (gtk_notebook,
                            screen_container,
                            tab_label,
                            position);
  gtk_container_child_set (GTK_CONTAINER (notebook),
                           screen_container,
                           "tab-expand", TRUE,
                           "tab-fill", TRUE,
                           NULL);
  gtk_notebook_set_tab_reorderable (gtk_notebook, screen_container, TRUE);
  gtk_notebook_set_tab_detachable (gtk_notebook, screen_container, TRUE);
}

static void
terminal_notebook_remove_screen (TerminalMdiContainer *container,
                                 TerminalScreen *screen)
{
  TerminalNotebook *notebook = TERMINAL_NOTEBOOK (container);
  TerminalScreenContainer *screen_container;

  g_warn_if_fail (gtk_widget_is_ancestor (GTK_WIDGET (screen), GTK_WIDGET (notebook)));

  update_tab_visibility (GTK_NOTEBOOK (notebook), -1);

  screen_container = terminal_screen_container_get_from_screen (screen);
  gtk_container_remove (GTK_CONTAINER (notebook),
                        GTK_WIDGET (screen_container));
}

static TerminalScreen *
terminal_notebook_get_active_screen (TerminalMdiContainer *container)
{
  TerminalNotebook *notebook = TERMINAL_NOTEBOOK (container);
  GtkNotebook *gtk_notebook = GTK_NOTEBOOK (notebook);
  GtkWidget *widget;

  widget = gtk_notebook_get_nth_page (gtk_notebook, gtk_notebook_get_current_page (gtk_notebook));
  return terminal_screen_container_get_screen (TERMINAL_SCREEN_CONTAINER (widget));
}

static void
terminal_notebook_set_active_screen (TerminalMdiContainer *container,
                                     TerminalScreen *screen)
{
  TerminalNotebook *notebook = TERMINAL_NOTEBOOK (container);
  GtkNotebook *gtk_notebook = GTK_NOTEBOOK (notebook);
  TerminalScreenContainer *screen_container;
  GtkWidget *widget;

  screen_container = terminal_screen_container_get_from_screen (screen);
  widget = GTK_WIDGET (screen_container);

  gtk_notebook_set_current_page (gtk_notebook,
                                 gtk_notebook_page_num (gtk_notebook, widget));
}

static GList *
terminal_notebook_list_screen_containers (TerminalMdiContainer *container)
{
  /* We are trusting that GtkNotebook will return pages in order */
  return gtk_container_get_children (GTK_CONTAINER (container));
}

static GList *
terminal_notebook_list_screens (TerminalMdiContainer *container)
{
  GList *list, *l;

  list = terminal_notebook_list_screen_containers (container);
  for (l = list; l != NULL; l = l->next)
    l->data = terminal_screen_container_get_screen ((TerminalScreenContainer *) l->data);

  return list;
}

static int
terminal_notebook_get_n_screens (TerminalMdiContainer *container)
{
  return gtk_notebook_get_n_pages (GTK_NOTEBOOK (container));
}

static int
terminal_notebook_get_active_screen_num (TerminalMdiContainer *container)
{
  return gtk_notebook_get_current_page (GTK_NOTEBOOK (container));
}

static void
terminal_notebook_reorder_screen (TerminalMdiContainer *container,
                                  TerminalScreen *screen,
                                  int new_position)
{
  GtkNotebook *notebook = GTK_NOTEBOOK (container);
  GtkWidget *child;
  int n, pos;

  g_return_if_fail (new_position == 1 || new_position == -1);

  child = GTK_WIDGET (terminal_screen_container_get_from_screen (screen));
  n = gtk_notebook_get_n_pages (notebook);
  pos = gtk_notebook_page_num (notebook, child);

  pos += new_position;
  gtk_notebook_reorder_child (notebook, child, 
                              pos < 0 ? n - 1 : pos < n ? pos : 0);
}

static void
terminal_notebook_mdi_iface_init (TerminalMdiContainerInterface *iface)
{
  iface->add_screen = terminal_notebook_add_screen;
  iface->remove_screen = terminal_notebook_remove_screen;
  iface->get_active_screen = terminal_notebook_get_active_screen;
  iface->set_active_screen = terminal_notebook_set_active_screen;
  iface->list_screens = terminal_notebook_list_screens;
  iface->list_screen_containers = terminal_notebook_list_screen_containers;
  iface->get_n_screens = terminal_notebook_get_n_screens;
  iface->get_active_screen_num = terminal_notebook_get_active_screen_num;
  iface->reorder_screen = terminal_notebook_reorder_screen;
}

G_DEFINE_TYPE_WITH_CODE (TerminalNotebook, terminal_notebook, GTK_TYPE_NOTEBOOK,
                         G_IMPLEMENT_INTERFACE (TERMINAL_TYPE_MDI_CONTAINER, terminal_notebook_mdi_iface_init))

/* GtkNotebookClass impl */

static void
terminal_notebook_switch_page (GtkNotebook     *gtk_notebook,
                               GtkWidget       *child,
                               guint            page_num)
{
  TerminalNotebook *notebook = TERMINAL_NOTEBOOK (gtk_notebook);
  TerminalNotebookPrivate *priv = notebook->priv;
  TerminalScreen *screen, *old_active_screen;

  GTK_NOTEBOOK_CLASS (terminal_notebook_parent_class)->switch_page (gtk_notebook, child, page_num);

  screen = terminal_screen_container_get_screen (TERMINAL_SCREEN_CONTAINER (child));

  old_active_screen = priv->active_screen;
  if (screen == old_active_screen)
    return;

  /* Workaround to remove gtknotebook's feature of computing its size based on
   * all pages. When the widget is hidden, its size will not be taken into
   * account.
   * FIXME!
   */
//   if (old_active_screen)
//     gtk_widget_hide (GTK_WIDGET (terminal_screen_container_get_from_screen (old_active_screen)));
  /* Make sure that the widget is no longer hidden due to the workaround */
//   if (child)
//     gtk_widget_show (child);
  if (old_active_screen)
    gtk_widget_hide (GTK_WIDGET (old_active_screen));
  if (screen)
    gtk_widget_show (GTK_WIDGET (screen));

  priv->active_screen = screen;

  g_signal_emit_by_name (notebook, "screen-switched", old_active_screen, screen);
  g_object_notify (G_OBJECT (notebook), "active-screen");
}

static void
terminal_notebook_page_added (GtkNotebook     *notebook,
                              GtkWidget       *child,
                              guint            page_num)
{
  void (* page_added) (GtkNotebook *, GtkWidget *, guint) =
    GTK_NOTEBOOK_CLASS (terminal_notebook_parent_class)->page_added;

  if (page_added)
    page_added (notebook, child, page_num);

  update_tab_visibility (notebook, 0);
  g_signal_emit_by_name (notebook, "screen-added", 
                         terminal_screen_container_get_screen (TERMINAL_SCREEN_CONTAINER (child)));
}

static void
terminal_notebook_page_removed (GtkNotebook     *notebook,
                                GtkWidget       *child,
                                guint            page_num)
{
  void (* page_removed) (GtkNotebook *, GtkWidget *, guint) =
    GTK_NOTEBOOK_CLASS (terminal_notebook_parent_class)->page_removed;

  if (page_removed)
    page_removed (notebook, child, page_num);

  update_tab_visibility (notebook, 0);
  g_signal_emit_by_name (notebook, "screen-removed",
                         terminal_screen_container_get_screen (TERMINAL_SCREEN_CONTAINER (child)));
}

static void
terminal_notebook_page_reordered (GtkNotebook     *notebook,
                                  GtkWidget       *child,
                                  guint            page_num)
{
  void (* page_reordered) (GtkNotebook *, GtkWidget *, guint) =
    GTK_NOTEBOOK_CLASS (terminal_notebook_parent_class)->page_reordered;

  if (page_reordered)
    page_reordered (notebook, child, page_num);

  g_signal_emit_by_name (notebook, "screens-reordered");
}

static GtkNotebook *
terminal_notebook_create_window (GtkNotebook       *notebook,
                                 GtkWidget         *page,
                                 gint               x,
                                 gint               y)
{
  return GTK_NOTEBOOK_CLASS (terminal_notebook_parent_class)->create_window (notebook, page, x, y);
}

/* GtkWidgetClass impl */

/* Tab scrolling was removed from GtkNotebook in gtk 3, so reimplement it here */
static gboolean
terminal_notebook_scroll_event (GtkWidget      *widget,
                                GdkEventScroll *event)
{
  GtkNotebook *notebook = GTK_NOTEBOOK (widget);
  gboolean (* scroll_event) (GtkWidget *, GdkEventScroll *) =
    GTK_WIDGET_GET_CLASS (widget)->scroll_event;
  GtkWidget *child, *event_widget, *action_widget;

  child = gtk_notebook_get_nth_page (notebook, gtk_notebook_get_current_page (notebook));
  if (child == NULL)
    goto chain_up;

  event_widget = gtk_get_event_widget ((GdkEvent *) event);

  /* Ignore scroll events from the content of the page */
  if (event_widget == NULL ||
      event_widget == child ||
      gtk_widget_is_ancestor (event_widget, child))
    goto chain_up;

  /* And also from the action widgets */
  action_widget = gtk_notebook_get_action_widget (notebook, GTK_PACK_START);
  if (event_widget == action_widget ||
      (action_widget != NULL && gtk_widget_is_ancestor (event_widget, action_widget)))
    goto chain_up;
  action_widget = gtk_notebook_get_action_widget (notebook, GTK_PACK_END);
  if (event_widget == action_widget ||
      (action_widget != NULL && gtk_widget_is_ancestor (event_widget, action_widget)))
    goto chain_up;

  switch (event->direction) {
    case GDK_SCROLL_RIGHT:
    case GDK_SCROLL_DOWN:
      gtk_notebook_next_page (notebook);
      return TRUE;
    case GDK_SCROLL_LEFT:
    case GDK_SCROLL_UP:
      gtk_notebook_prev_page (notebook);
      return TRUE;
#if GTK_CHECK_VERSION (3, 3, 17)
    case GDK_SCROLL_SMOOTH:
      switch (gtk_notebook_get_tab_pos (notebook)) {
        case GTK_POS_LEFT:
        case GTK_POS_RIGHT:
          if (event->delta_y > 0)
            gtk_notebook_next_page (notebook);
          else if (event->delta_y < 0)
            gtk_notebook_prev_page (notebook);
          break;
        case GTK_POS_TOP:
        case GTK_POS_BOTTOM:
          if (event->delta_x > 0)
            gtk_notebook_next_page (notebook);
          else if (event->delta_x < 0)
            gtk_notebook_prev_page (notebook);
          break;
      }
      return TRUE;
#endif
  }

chain_up:
  if (scroll_event)
    return scroll_event (widget, event);

  return FALSE;
}

/* GObjectClass impl */

static void
terminal_notebook_init (TerminalNotebook *notebook)
{
  TerminalNotebookPrivate *priv;

  priv = notebook->priv = TERMINAL_NOTEBOOK_GET_PRIVATE (notebook);

  priv->active_screen = NULL;
}

static void
terminal_notebook_constructed (GObject *object)
{
  GtkWidget *widget = GTK_WIDGET (object);
  GtkNotebook *notebook = GTK_NOTEBOOK (object);

  G_OBJECT_CLASS (terminal_notebook_parent_class)->constructed (object);

  gtk_notebook_set_show_tabs (notebook, FALSE);
  gtk_notebook_set_scrollable (notebook, TRUE);
  gtk_notebook_set_show_border (notebook, FALSE);
  gtk_notebook_set_group_name (notebook, I_("gnome-terminal-window"));

  /* Necessary for scroll events */
  gtk_widget_add_events (widget, GDK_SCROLL_MASK);
}

static void
terminal_notebook_get_property (GObject *object,
                                guint prop_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  TerminalMdiContainer *mdi_container = TERMINAL_MDI_CONTAINER (object);

  switch (prop_id) {
    case PROP_ACTIVE_SCREEN:
      g_value_set_object (value, terminal_notebook_get_active_screen (mdi_container));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
terminal_notebook_set_property (GObject *object,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  TerminalMdiContainer *mdi_container = TERMINAL_MDI_CONTAINER (object);

  switch (prop_id) {
    case PROP_ACTIVE_SCREEN:
      terminal_notebook_set_active_screen (mdi_container, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
terminal_notebook_class_init (TerminalNotebookClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkNotebookClass *notebook_class = GTK_NOTEBOOK_CLASS (klass);

  g_type_class_add_private (gobject_class, sizeof (TerminalNotebookPrivate));

  gobject_class->constructed = terminal_notebook_constructed;
  gobject_class->get_property = terminal_notebook_get_property;
  gobject_class->set_property = terminal_notebook_set_property;

  g_object_class_override_property (gobject_class, PROP_ACTIVE_SCREEN, "active-screen");

  widget_class->scroll_event = terminal_notebook_scroll_event;

  notebook_class->switch_page = terminal_notebook_switch_page;
  notebook_class->create_window = terminal_notebook_create_window;
  notebook_class->page_added = terminal_notebook_page_added;
  notebook_class->page_removed = terminal_notebook_page_removed;
  notebook_class->page_reordered = terminal_notebook_page_reordered;
}

/* public API */

/**
 * terminal_notebook_new:
 *
 * Returns: (transfer full): a new #TerminalNotebook
 */
GtkWidget *
terminal_notebook_new (void)
{
  return g_object_new (TERMINAL_TYPE_NOTEBOOK, NULL);
}
