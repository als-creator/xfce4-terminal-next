/*-
 * Copyright (c) 2004-2008 os-cillation e.K.
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <glib/gstdio.h>
#ifdef ENABLE_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

#include <gdk/gdk.h>
#include <libxfce4ui/libxfce4ui.h>

#include "terminal-encoding-action.h"
#include "terminal-enum-types.h"
#include "terminal-keys.h"
#include "terminal-tmux.h"
#include "terminal-marshal.h"
#include "terminal-options.h"
#include "terminal-panes.h"
#include "terminal-preferences-dialog.h"
#include "terminal-private.h"
#include "terminal-search-dialog.h"
#include "terminal-util.h"
#include "terminal-widget.h"
#include "terminal-window-dropdown.h"
#include "terminal-window.h"



/* Signal identifiers */
enum
{
  NEW_WINDOW,
  NEW_WINDOW_WITH_SCREEN,
  LAST_SIGNAL
};

/* Used by confirm_close() */
enum
{
  CONFIRMED_NONE,
  CONFIRMED_CLOSE_TAB,
  CONFIRMED_CLOSE_WINDOW
};

#define NOTEBOOK_NAME PACKAGE_NAME "-notebook"

/* Clipboard slots: buffers of one's own, so that a second string can be taken
 * along without throwing the first one away. They belong to the application,
 * not to a window: a string copied in one window is pasted in another. How
 * many there are is up to the setting, and what a window holds now it keeps
 * when the setting grows. */
#define CLIPBOARD_SLOTS_DEFAULT 3
#define CLIPBOARD_SLOTS_MIN 2
#define CLIPBOARD_SLOTS_MAX 10

static gchar **clipboard_slots;
static guint clipboard_slots_n = CLIPBOARD_SLOTS_DEFAULT;
static guint clipboard_slot = 0;

/* The slot that was used last: the one the paste accelerator takes from, be
 * it the slot the selection was copied to or the slot that was pasted out
 * of. */
static guint clipboard_slot_last = 0;


/* The switches of the feature are plain booleans. terminal_preferences_get ()
 * hands out a reference on every call, so the release lives here, in one
 * place, instead of in every caller. */
static gboolean
terminal_window_clipboard_slots_flag (const gchar *property)
{
  gboolean value = FALSE;
  TerminalPreferences *preferences = terminal_preferences_get ();

  g_object_get (G_OBJECT (preferences), property, &value, NULL);
  g_object_unref (preferences);

  return value;
}


/* One switch for the whole feature: off means no entry in any menu and no
 * slot accelerator working, exactly as if there were no slots at all. */
static gboolean
terminal_window_clipboard_slots_enabled (void)
{
  return terminal_window_clipboard_slots_flag ("misc-clipboard-slots-enabled");
}


/* Whether the slots of a direction sit under a submenu of their own, as they
 * did before they were spread out one entry per slot. */
static gboolean
terminal_window_clipboard_slots_nested (void)
{
  return terminal_window_clipboard_slots_flag ("misc-clipboard-slots-nested");
}


/* the number of slots, and the array sized for it; growing keeps what is
 * already in the slots */
static guint
terminal_window_clipboard_slots (void)
{
  guint n = CLIPBOARD_SLOTS_DEFAULT;
  TerminalPreferences *preferences = terminal_preferences_get ();
  guint i;

  g_object_get (G_OBJECT (preferences), "misc-clipboard-slots", &n, NULL);
  g_object_unref (preferences);
  n = CLAMP (n, (guint) CLIPBOARD_SLOTS_MIN, (guint) CLIPBOARD_SLOTS_MAX);

  if (clipboard_slots == NULL)
    {
      /* the array is born empty; every cell is a string of its own and has to
       * start as one, whatever the count happens to be */
      clipboard_slots = g_new0 (gchar *, n);
      clipboard_slots_n = n;
    }
  else if (clipboard_slots_n != n)
    {
      /* a smaller number gives up the tail: realloc would drop the pointer
       * and nothing would ever free that string */
      for (i = n; i < clipboard_slots_n; i++)
        g_clear_pointer (&clipboard_slots[i], g_free);

      clipboard_slots = g_realloc (clipboard_slots, n * sizeof (gchar *));
      for (i = clipboard_slots_n; i < n; i++)
        clipboard_slots[i] = NULL;
      clipboard_slots_n = n;
    }

  if (clipboard_slot >= n)
    clipboard_slot = 0;

  return n;
}



/* The file the slots are kept in: what the setting says, and the default path
 * when it says nothing. A path typed as "~/..." is opened under the home
 * directory, because nothing else would expand it and the working directory
 * of a terminal started from the menu is not a place anyone chooses. */
static gchar *
terminal_window_clipboard_slots_file (void)
{
  TerminalPreferences *preferences = terminal_preferences_get ();
  gchar *file = NULL;
  gchar *path = NULL;

  g_object_get (G_OBJECT (preferences), "misc-clipboard-slots-file", &file, NULL);
  g_object_unref (preferences);

  if (file == NULL || *file == '\0')
    path = terminal_preferences_clipboard_slots_default_file ();
  else if (g_str_has_prefix (file, "~/"))
    path = g_build_filename (g_get_home_dir (), file + 2, NULL);
  else
    path = g_strdup (file);

  g_free (file);

  return path;
}



/* The slots are read once, when the application starts: what the last session
 * left behind. A file that is not there is the normal case of a first run, and
 * a slot the file does not mention stays empty.
 *
 * Reading it later, when the first menu is built, would not do: a session that
 * never opens a menu would never read the file, and the write at the end would
 * then wipe what the file held. */
void
terminal_window_clipboard_slots_load (void)
{
  GKeyFile *key_file;
  GError *error = NULL;
  guint n;
  guint i;
  gchar *path;

  /* the array has to exist before the file has anywhere to land */
  n = terminal_window_clipboard_slots ();

  path = terminal_window_clipboard_slots_file ();
  key_file = g_key_file_new ();

  if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error))
    {
      g_clear_error (&error);
      goto out;
    }

  for (i = 0; i < n; i++)
    {
      gchar *key = g_strdup_printf ("slot-%u", i + 1);
      gchar *text = g_key_file_get_string (key_file, "Slots", key, NULL);

      /* an empty line is not a slot worth keeping: it would grey out the
       * entry the same way an empty slot does, for nothing */
      if (text != NULL && *text != '\0')
        {
          g_free (clipboard_slots[i]);
          clipboard_slots[i] = text;
        }
      else
        {
          g_free (text);
        }

      g_free (key);
    }

out:
  g_key_file_free (key_file);
  g_free (path);
}



/* Called once, when the last window goes: the slots are of the application
 * and not of a window, so there is no window left to hold them. */
void
terminal_window_clipboard_slots_save (void)
{
  GKeyFile *key_file;
  GError *error = NULL;
  gchar *path;
  gchar *directory;
  gchar *data;
  gsize length;
  guint i;

  if (!terminal_window_clipboard_slots_enabled ()
      || !terminal_window_clipboard_slots_flag ("misc-clipboard-slots-save"))
    return;

  key_file = g_key_file_new ();

  for (i = 0; i < clipboard_slots_n; i++)
    {
      gchar *key;

      if (clipboard_slots[i] == NULL || *clipboard_slots[i] == '\0')
        continue;

      key = g_strdup_printf ("slot-%u", i + 1);
      g_key_file_set_string (key_file, "Slots", key, clipboard_slots[i]);
      g_free (key);
    }

  data = g_key_file_to_data (key_file, &length, NULL);
  path = terminal_window_clipboard_slots_file ();

  /* a first run has no directory to write into, and g_file_set_contents does
   * not make one: it would fail with "No such file or directory" */
  directory = g_path_get_dirname (path);
  g_mkdir_with_parents (directory, 0755);
  g_free (directory);

  /* a failure here is not worth a dialog: the terminal is on its way out and
   * the reason is in the journal */
  if (!g_file_set_contents (path, data, (gssize) length, &error))
    {
      g_warning ("Failed to write the clipboard slots to %s: %s", path, error->message);
    }
  else
    {
      /* what the slots hold can be a password as well as a path: the file is
       * for the owner alone, whatever the umask of the session says */
      if (g_chmod (path, 0600) != 0)
        g_warning ("Failed to restrict the clipboard slots file %s to its owner", path);
    }

  g_clear_error (&error);
  g_free (data);
  g_free (path);
  g_key_file_free (key_file);
}


void
terminal_window_free_clipboard_slots (void)
{
  guint i;

  /* the last moment the slots exist: what was copied is written out before
   * the memory goes, if the setting asked for a file */
  terminal_window_clipboard_slots_save ();

  /* the array only appears when a slot is touched, and the application may be
   * finalized before that ever happened */
  if (clipboard_slots == NULL)
    return;

  for (i = 0; i < clipboard_slots_n; i++)
    g_clear_pointer (&clipboard_slots[i], g_free);
  g_clear_pointer (&clipboard_slots, g_free);
  clipboard_slots_n = CLIPBOARD_SLOTS_DEFAULT;
}

/* CSS for slim notebook tabs style */
const gchar *CSS_SLIM_TABS =
  "#" NOTEBOOK_NAME " tab {\n"
  "  min-height: 0;\n"
  "  font-weight: normal;\n"
  "  padding: 1px;\n"
  "  margin: 0;\n"
  "}\n"
  "#" NOTEBOOK_NAME " tab button {\n"
  "  min-height: 0;\n"
  "  min-width: 0;\n"
  "  padding: 1px;\n"
  "  margin: 0;\n"
  "}\n"
  "#" NOTEBOOK_NAME " button {\n"
  "  min-height: 0;\n"
  "  min-width: 0;\n"
  "  padding: 1px;\n"
  "}\n";

/* See gnome-terminal bug #789356 */
#define WINDOW_STATE_TILED (GDK_WINDOW_STATE_TILED | GDK_WINDOW_STATE_LEFT_TILED | GDK_WINDOW_STATE_RIGHT_TILED | GDK_WINDOW_STATE_TOP_TILED | GDK_WINDOW_STATE_BOTTOM_TILED)



typedef struct
{
  TerminalWindow *window;
  gint signal;
} SendSignalData;



static void
terminal_window_finalize (GObject *object);
static gboolean
terminal_window_delete_event (GtkWidget *widget,
                              GdkEventAny *event);
static gboolean
terminal_window_state_event (GtkWidget *widget,
                             GdkEventWindowState *event);
static void
terminal_window_style_set (GtkWidget *widget,
                           GtkStyle *previous_style);
static gboolean
terminal_window_scroll_event (GtkWidget *widget,
                              GdkEventScroll *event);
static gboolean
terminal_window_map_event (GtkWidget *widget,
                           GdkEventAny *event);
static gboolean
terminal_window_focus_in_event (GtkWidget *widget,
                                GdkEventFocus *event);
static gboolean
terminal_window_focus_out_event (GtkWidget *widget,
                                 GdkEventFocus *event);
static gboolean
terminal_window_key_press_event (GtkWidget *widget,
                                 GdkEventKey *event);
static gboolean
terminal_window_key_release_event (GtkWidget *widget,
                                   GdkEventKey *event);
static gint
terminal_window_confirm_close (TerminalScreen *screen,
                               TerminalWindow *window);
static void
terminal_window_size_push (TerminalWindow *window);
static gboolean
terminal_window_size_pop (gpointer data);
static void
terminal_window_set_size_force_grid (TerminalWindow *window,
                                     TerminalScreen *screen,
                                     glong force_grid_width,
                                     glong force_grid_height);
static void
terminal_window_update_slim_tabs (TerminalWindow *window);
static void
terminal_window_update_mnemonic_modifier (TerminalWindow *window);
static void
terminal_window_notebook_page_switched (GtkNotebook *notebook,
                                        GtkWidget *page,
                                        guint page_num,
                                        TerminalWindow *window);
static void
terminal_window_notebook_page_added (GtkNotebook *notebook,
                                     GtkWidget *child,
                                     guint page_num,
                                     TerminalWindow *window);
static void
terminal_window_notebook_page_removed (GtkNotebook *notebook,
                                       GtkWidget *child,
                                       guint page_num,
                                       TerminalWindow *window);
static gboolean
terminal_window_notebook_event_in_allocation (gint event_x,
                                              gint event_y,
                                              GtkWidget *widget);
static gboolean
terminal_window_notebook_button_press_event (GtkNotebook *notebook,
                                             GdkEventButton *event,
                                             TerminalWindow *window);
static gboolean
terminal_window_notebook_button_release_event (GtkNotebook *notebook,
                                               GdkEventButton *event,
                                               TerminalWindow *window);
#if !LIBXFCE4UI_CHECK_VERSION(4, 21, 1)
static gboolean
terminal_window_notebook_scroll_event (GtkNotebook *notebook,
                                       GdkEventScroll *event,
                                       TerminalWindow *window);
#endif
static void
terminal_window_notebook_drag_data_received (GtkWidget *widget,
                                             GdkDragContext *context,
                                             gint x,
                                             gint y,
                                             GtkSelectionData *selection_data,
                                             guint info,
                                             guint time,
                                             TerminalWindow *window);
static GtkNotebook *
terminal_window_notebook_create_window (GtkNotebook *notebook,
                                        GtkWidget *child,
                                        gint x,
                                        gint y,
                                        TerminalWindow *window);
static GtkWidget *
terminal_window_get_context_menu (TerminalScreen *screen,
                                  TerminalWindow *window);
static void
terminal_window_notify_title (TerminalScreen *screen,
                              GParamSpec *pspec,
                              TerminalWindow *window);
static void
terminal_window_action_set_encoding (GtkAction *action,
                                     const gchar *charset,
                                     TerminalWindow *window);
static gboolean
terminal_window_action_new_tab (TerminalWindow *window);
static gboolean
terminal_window_action_new_window (TerminalWindow *window);
static gboolean
terminal_window_action_open_folder (TerminalWindow *window);
static gboolean
terminal_window_action_undo_close_tab (TerminalWindow *window);
static gboolean
terminal_window_action_detach_tab (TerminalWindow *window);
static gboolean
terminal_window_action_close_tab (TerminalWindow *window);
static gboolean
terminal_window_action_close_other_tabs (TerminalWindow *window);
static gboolean
terminal_window_action_close_tabs_right (TerminalWindow *window);
static gboolean
terminal_window_action_close_all_tabs (TerminalWindow *window);
static gboolean
terminal_window_action_close_window (TerminalWindow *window);
static gboolean
terminal_window_action_copy (TerminalWindow *window);
static gboolean
terminal_window_action_copy_html (TerminalWindow *window);
static gboolean
terminal_window_action_copy_as (TerminalWindow *window);
static gboolean
terminal_window_action_copy_to_slot (TerminalWindow *window);
static gboolean
terminal_window_action_goto_bookmark (GtkMenuItem *item,
                                     TerminalWindow *window);
static gboolean
terminal_window_action_paste_from_slot (TerminalWindow *window);
static void
terminal_window_slot_paste (TerminalWindow *window,
                            const gchar *text);
static gboolean
terminal_window_action_paste (TerminalWindow *window);
static gboolean
terminal_window_action_paste_selection (TerminalWindow *window);
static gboolean
terminal_window_action_select_all (TerminalWindow *window);
static gboolean
terminal_window_action_copy_input (TerminalWindow *window);
static gboolean
terminal_window_action_prefs (TerminalWindow *window);
static gboolean
terminal_window_action_toggle_menubar (TerminalWindow *window);
static gboolean
terminal_window_action_show_menubar (TerminalWindow *window);
static gboolean
terminal_window_action_toggle_toolbar (TerminalWindow *window);
static gboolean
terminal_window_action_toggle_borders (TerminalWindow *window);
static gboolean
terminal_window_action_fullscreen (TerminalWindow *window);
static gboolean
terminal_window_action_readonly (TerminalWindow *window);
static gboolean
terminal_window_action_scroll_on_output (TerminalWindow *window);
static gboolean
terminal_window_action_zoom_in (TerminalWindow *window);
static gboolean
terminal_window_action_zoom_out (TerminalWindow *window);
static gboolean
terminal_window_action_zoom_reset (TerminalWindow *window);
static gboolean
terminal_window_action_prev_tab (TerminalWindow *window);
static gboolean
terminal_window_action_next_tab (TerminalWindow *window);
static gboolean
terminal_window_action_last_active_tab (TerminalWindow *window);
static gboolean
terminal_window_action_move_tab_left (TerminalWindow *window);
static gboolean
terminal_window_action_move_tab_right (TerminalWindow *window);
static gboolean
terminal_window_action_goto_tab (GtkRadioAction *action,
                                 GtkNotebook *notebook);
static gboolean
terminal_window_action_set_title (TerminalWindow *window);
static gboolean
terminal_window_action_set_title_color (TerminalWindow *window);
static gboolean
terminal_window_action_search (TerminalWindow *window);
static gboolean
terminal_window_action_search_next (TerminalWindow *window);
static gboolean
terminal_window_action_search_prev (TerminalWindow *window);
static gboolean
terminal_window_action_save_contents (TerminalWindow *window);
static gboolean
terminal_window_action_reset (TerminalWindow *window);
static gboolean
terminal_window_action_reset_and_clear (TerminalWindow *window);
static void
terminal_window_action_send_signal (SendSignalData *data);
static gboolean
terminal_window_action_split_horizontal (TerminalWindow *window);
static gboolean
terminal_window_action_start_tmux (TerminalWindow *window);
static gboolean
terminal_window_action_split_vertical (TerminalWindow *window);
static gboolean
terminal_window_add_fresh_tab_idle (gpointer user_data);
static gboolean
terminal_window_action_close_pane (TerminalWindow *window);
static gboolean
terminal_window_action_focus_up (TerminalWindow *window);
static gboolean
terminal_window_action_focus_down (TerminalWindow *window);
static gboolean
terminal_window_action_focus_left (TerminalWindow *window);
static gboolean
terminal_window_action_focus_right (TerminalWindow *window);
static gboolean
terminal_window_action_focus_next (TerminalWindow *window);
static gboolean
terminal_window_action_focus_prev (TerminalWindow *window);
static gboolean
terminal_window_action_move_pane_up (TerminalWindow *window);
static gboolean
terminal_window_action_move_pane_down (TerminalWindow *window);
static gboolean
terminal_window_action_move_pane_left (TerminalWindow *window);
static gboolean
terminal_window_action_move_pane_right (TerminalWindow *window);
static gboolean
terminal_window_action_contents (TerminalWindow *window);
static gboolean
terminal_window_action_about (TerminalWindow *window);
static gboolean
terminal_window_action_do_nothing (TerminalWindow *window);

static void
terminal_window_zoom_update_screens (TerminalWindow *window);
static TerminalPanes *
terminal_window_get_current_panes (TerminalWindow *window);
static void
terminal_window_set_active_screen (TerminalWindow *window,
                                   TerminalPanes *panes,
                                   TerminalScreen *screen);
static void
terminal_window_setup_screen (TerminalWindow *window,
                              TerminalScreen *screen);
static TerminalPanes *
terminal_window_screen_get_panes (TerminalScreen *screen);
static GtkWidget *
terminal_window_screen_get_page (TerminalWindow *window,
                                 TerminalScreen *screen);
static gboolean
terminal_window_screen_focus_in (GtkWidget *widget,
                                 GdkEventFocus *event,
                                 TerminalWindow *window);
static void
terminal_window_panes_active_screen_changed (TerminalPanes *panes,
                                             TerminalScreen *screen,
                                             TerminalWindow *window);
static void
terminal_window_update_panes_label (TerminalWindow *window,
                                    TerminalPanes *panes);
static void
terminal_window_panes_close_clicked (GtkWidget *button,
                                     TerminalPanes *panes);
static GtkWidget *
terminal_window_create_panes_label (TerminalWindow *window,
                                    TerminalPanes *panes);
static void
terminal_window_switch_tab (GtkNotebook *notebook,
                            gboolean switch_left);
static void
terminal_window_move_tab (GtkNotebook *notebook,
                          gboolean move_left);
static void
title_popover_close (GtkWidget *popover,
                     TerminalWindow *window);
static void
terminal_window_do_close_tab (TerminalScreen *screen,
                              TerminalWindow *window);
static gboolean
terminal_window_panes_layout_restore_idle (gpointer user_data);
static void
terminal_window_restore_panes_layout (TerminalWindow *window,
                                       TerminalPanes   *panes,
                                       const gchar     *layout);
static void
terminal_window_restore_panes_commands (TerminalPanes *panes,
                                        const gchar   *record);
static void
terminal_window_save_session_state (TerminalWindow *window);
static void
terminal_window_restore_session_state (TerminalWindow *window);
static gboolean
terminal_window_panes_layouts_enabled (TerminalWindow *window);
static gchar *
terminal_window_saved_pane_directory (const gchar *record,
                                      guint        nth);

static void
terminal_window_create_menu (TerminalWindow *window,
                             TerminalWindowAction action,
                             GCallback cb_update_menu);
static void
terminal_window_menu_clean (GtkMenu *menu);
static void
terminal_window_menu_add_section (TerminalWindow *window,
                                  GtkWidget *menu,
                                  MenuSections sections,
                                  gboolean as_submenu);
static void
terminal_window_update_file_menu (TerminalWindow *window,
                                  GtkWidget *menu);
static void
terminal_window_update_edit_menu (TerminalWindow *window,
                                  GtkWidget *menu);
static void
terminal_window_apply_title (TerminalWindow *window,
                             TerminalScreen *screen);
static void
terminal_window_update_view_menu (TerminalWindow *window,
                                  GtkWidget *menu);
static void
terminal_window_update_terminal_menu (TerminalWindow *window,
                                      GtkWidget *menu);
static void
terminal_window_update_tabs_menu (TerminalWindow *window,
                                  GtkWidget *menu);
static void
terminal_window_add_slots_menu (TerminalWindow *window,
                                GtkWidget     *menu,
                                gboolean       with_hotkey);
static gboolean
terminal_window_action_slot_item (GtkMenuItem *item,
                                  TerminalWindow *window);
static void
terminal_window_update_help_menu (TerminalWindow *window,
                                  GtkWidget *menu);
static gboolean
terminal_window_can_go_left (TerminalWindow *window);
static gboolean
terminal_window_can_go_right (TerminalWindow *window);


struct _TerminalWindowPrivate
{
  GtkWidget *vbox;
  GtkWidget *notebook;
  GtkWidget *tabs_menu; /* used for the go-to tab accelerators */

  /* for the drop-down to keep open with dialogs */
  guint n_child_windows;

  GSList *tabs_menu_actions;


  TerminalPreferences *preferences;
  GtkWidget *preferences_dialog;

  GtkAccelGroup *accel_group;

  GtkWidget *search_dialog;
  GtkWidget *title_popover;

  /* pushed size of screen */
  glong grid_width;
  glong grid_height;

  GtkAction *encoding_action;

  TerminalScreen *active;
  TerminalScreen *last_active;

  GQueue *closed_tabs_list;


  gchar *font;

  TerminalVisibility scrollbar_visibility;
  TerminalZoomLevel zoom;

  /* saved pane layouts of the tabs, replayed while the startup tabs appear */
  gchar **panes_layouts;
  gint panes_layout_index;
  guint panes_layout_idle;

  /* tmux was already started for the first pane of this window */

  /* if this is a TerminalWindowDropdown */
  guint drop_down : 1;
};

static guint window_signals[LAST_SIGNAL];
static gchar *window_notebook_group = PACKAGE_NAME;
static GQuark tabs_menu_action_quark = 0;
static gchar *signal_names[] = {
  NULL,
  "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM",
  "STKFLT", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "INCH", "IO",
  "PWR", "SYS"
};



static const GtkTargetEntry targets[] = {
  { "GTK_NOTEBOOK_TAB", GTK_TARGET_SAME_APP, TARGET_GTK_NOTEBOOK_TAB },
};



static XfceGtkActionEntry action_entries[] = {
  /* Use old accelerator paths for backwards compatibility */
  {
    TERMINAL_WINDOW_ACTION_FILE_MENU,
    "<Actions>/terminal-window/file-menu",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_File"),
    NULL,
    NULL,
    NULL,
  },
  {
    TERMINAL_WINDOW_ACTION_NEW_TAB,
    "<Actions>/terminal-window/new-tab",
    "<control><shift>t",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Open _Tab"),
    N_ ("Open a new terminal tab"),
    "tab-new",
    G_CALLBACK (terminal_window_action_new_tab),
  },
  {
    TERMINAL_WINDOW_ACTION_NEW_WINDOW,
    "<Actions>/terminal-window/new-window",
    "<control><shift>n",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Open T_erminal"),
    N_ ("Open a new terminal window"),
    "window-new",
    G_CALLBACK (terminal_window_action_new_window),
  },
  {
    TERMINAL_WINDOW_ACTION_OPEN_FOLDER,
    "<Actions>/terminal-window/open-folder",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Open File Manager Here"),
    N_ ("Open the current directory"),
    "folder-open",
    G_CALLBACK (terminal_window_action_open_folder),
  },
  {
    TERMINAL_WINDOW_ACTION_UNDO_CLOSE_TAB,
    "<Actions>/terminal-window/undo-close-tab",
    "<control><shift>d",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Undo Close Tab"),
    NULL,
    "document-revert",
    G_CALLBACK (terminal_window_action_undo_close_tab),
  },
  {
    TERMINAL_WINDOW_ACTION_DETACH_TAB,
    "<Actions>/terminal-window/detach-tab",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Detach Tab"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_detach_tab),
  },
  {
    TERMINAL_WINDOW_ACTION_CLOSE_TAB,
    "<Actions>/terminal-window/close-tab",
    "<control><shift>w",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Close T_ab"),
    NULL,
    "window-close",
    G_CALLBACK (terminal_window_action_close_tab),
  },
  {
    TERMINAL_WINDOW_ACTION_CLOSE_OTHER_TABS,
    "<Actions>/terminal-window/close-other-tabs",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Close Other Ta_bs"),
    NULL,
    "edit-clear",
    G_CALLBACK (terminal_window_action_close_other_tabs),
  },
  {
    TERMINAL_WINDOW_ACTION_CLOSE_TABS_RIGHT,
    "<Actions>/terminal-window/close-tabs-right",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Close Tabs to the _Right"),
    NULL,
    "edit-clear",
    G_CALLBACK (terminal_window_action_close_tabs_right),
  },
  {
    TERMINAL_WINDOW_ACTION_CLOSE_ALL_TABS,
    "<Actions>/terminal-window/close-all-tabs",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Close _All Tabs"),
    NULL,
    "edit-clear",
    G_CALLBACK (terminal_window_action_close_all_tabs),
  },
  {
    TERMINAL_WINDOW_ACTION_CLOSE_WINDOW,
    "<Actions>/terminal-window/close-window",
    "<control><shift>q",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Close _Window"),
    NULL,
    "application-exit",
    G_CALLBACK (terminal_window_action_close_window),
  },
  {
    TERMINAL_WINDOW_ACTION_EDIT_MENU,
    "<Actions>/terminal-window/edit-menu",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Edit"),
    NULL,
    NULL,
    NULL,
  },
  {
    TERMINAL_WINDOW_ACTION_COPY,
    "<Actions>/terminal-window/copy",
    "<control><shift>c",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Copy"),
    N_ ("Copy to clipboard"),
    "edit-copy",
    G_CALLBACK (terminal_window_action_copy),
  },
  {
    TERMINAL_WINDOW_ACTION_COPY_HTML,
    "<Actions>/terminal-window/copy-html",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Copy as _HTML"),
    N_ ("Copy to clipboard as HTML"),
    "edit-copy",
    G_CALLBACK (terminal_window_action_copy_html),
  },
  {
    TERMINAL_WINDOW_ACTION_COPY_AS,
    "<Actions>/terminal-window/copy-as",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Copy As _..."),
    N_ ("Ask in which form the selection goes into the clipboard"),
    "edit-copy",
    G_CALLBACK (terminal_window_action_copy_as),
  },
  {
    TERMINAL_WINDOW_ACTION_COPY_TO_SLOT,
    "<Actions>/terminal-window/copy-to-slot",
    "<control><alt>c",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Copy to Clipboard Slot"),
    N_ ("Copy the selection into the next clipboard slot"),
    "edit-copy",
    G_CALLBACK (terminal_window_action_copy_to_slot),
  },
  {
    TERMINAL_WINDOW_ACTION_PASTE_FROM_SLOT,
    "<Actions>/terminal-window/paste-from-slot",
    "<control><alt>v",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Paste from Clipboard _Slot"),
    N_ ("Paste the text of the last used clipboard slot"),
    "edit-paste",
    G_CALLBACK (terminal_window_action_paste_from_slot),
  },
  {
    TERMINAL_WINDOW_ACTION_PASTE,
    "<Actions>/terminal-window/paste",
    "<control><shift>v",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Paste"),
    N_ ("Paste from clipboard"),
    "edit-paste",
    G_CALLBACK (terminal_window_action_paste),
  },
  {
    TERMINAL_WINDOW_ACTION_PASTE_ALT,
    "<Actions>/terminal-window/paste-alt",
    "<control><shift>Insert",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Paste Alt"),
    NULL,
    "edit-paste-alt",
    G_CALLBACK (terminal_window_action_paste),
  },
  {
    TERMINAL_WINDOW_ACTION_PASTE_SELECTION,
    "<Actions>/terminal-window/paste-selection",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("Paste _Selection"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_paste_selection),
  },
  {
    TERMINAL_WINDOW_ACTION_PASTE_SELECTION_ALT,
    "<Actions>/terminal-window/paste-selection-alt",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("Paste _Selection Alt"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_paste_selection),
  },
  {
    TERMINAL_WINDOW_ACTION_SELECT_ALL,
    "<Actions>/terminal-window/select-all",
    "<control><shift>a",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Select _All"),
    NULL,
    "edit-select-all",
    G_CALLBACK (terminal_window_action_select_all),
  },
  {
    TERMINAL_WINDOW_ACTION_COPY_INPUT,
    "<Actions>/terminal-window/copy-input",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("Copy _Input To All Tabs..."),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_copy_input),
  },
  {
    TERMINAL_WINDOW_ACTION_PREFERENCES,
    "<Actions>/terminal-window/preferences",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Pr_eferences..."),
    N_ ("Open the preferences dialog"),
    "preferences-system",
    G_CALLBACK (terminal_window_action_prefs),
  },
  {
    TERMINAL_WINDOW_ACTION_VIEW_MENU,
    "<Actions>/terminal-window/view-menu",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_View"),
    NULL,
    NULL,
    NULL,
  },
  {
    TERMINAL_WINDOW_ACTION_ZOOM_IN,
    "<Actions>/terminal-window/zoom-in",
    "<control>plus",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Zoom _In"),
    N_ ("Zoom in with larger font"),
    "zoom-in",
    G_CALLBACK (terminal_window_action_zoom_in),
  },
  {
    TERMINAL_WINDOW_ACTION_ZOOM_IN_ALT,
    "<Actions>/terminal-window/zoom-in-alt",
    "<control>KP_Add",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Zoom In Alt"),
    NULL,
    "zoom-in-alt",
    G_CALLBACK (terminal_window_action_zoom_in),
  },
  {
    TERMINAL_WINDOW_ACTION_ZOOM_OUT,
    "<Actions>/terminal-window/zoom-out",
    "<control>minus",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Zoom _Out"),
    N_ ("Zoom out with smaller font"),
    "zoom-out",
    G_CALLBACK (terminal_window_action_zoom_out),
  },
  {
    TERMINAL_WINDOW_ACTION_ZOOM_OUT_ALT,
    "<Actions>/terminal-window/zoom-out-alt",
    "<control>KP_Subtract",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Zoom Out Alt"),
    NULL,
    "zoom-out-alt",
    G_CALLBACK (terminal_window_action_zoom_out),
  },
  {
    TERMINAL_WINDOW_ACTION_ZOOM_RESET,
    "<Actions>/terminal-window/zoom-reset",
    "<control>0",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Normal Size"),
    N_ ("Zoom to default size"),
    "zoom-original",
    G_CALLBACK (terminal_window_action_zoom_reset),
  },
  {
    TERMINAL_WINDOW_ACTION_ZOOM_RESET_ALT,
    "<Actions>/terminal-window/zoom-reset-alt",
    "<control>KP_0",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Normal Size Alt"),
    NULL,
    "zoom-original-alt",
    G_CALLBACK (terminal_window_action_zoom_reset),
  },
  {
    TERMINAL_WINDOW_ACTION_TERMINAL_MENU,
    "<Actions>/terminal-window/terminal-menu",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Terminal"),
    NULL,
    NULL,
    NULL,
  },
  {
    TERMINAL_WINDOW_ACTION_SET_TITLE,
    "<Actions>/terminal-window/set-title",
    "<control><shift>s",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Set Title..."),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_set_title),
  },
  {
    TERMINAL_WINDOW_ACTION_SET_TITLE_COLOR,
    "<Actions>/terminal-window/set-title-color",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("Set Title Co_lor..."),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_set_title_color),
  },
  {
    TERMINAL_WINDOW_ACTION_SEARCH,
    "<Actions>/terminal-window/search",
    "<control><shift>f",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Find..."),
    N_ ("Search terminal contents"),
    "edit-find",
    G_CALLBACK (terminal_window_action_search),
  },
  {
    TERMINAL_WINDOW_ACTION_SEARCH_NEXT,
    "<Actions>/terminal-window/search-next",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("Find Ne_xt"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_search_next),
  },
  {
    TERMINAL_WINDOW_ACTION_SEARCH_PREV,
    "<Actions>/terminal-window/search-prev",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("Find Pre_vious"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_search_prev),
  },
  {
    TERMINAL_WINDOW_ACTION_SAVE_CONTENTS,
    "<Actions>/terminal-window/save-contents",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Sa_ve Contents..."),
    NULL,
    "document-save-as",
    G_CALLBACK (terminal_window_action_save_contents),
  },
  {
    TERMINAL_WINDOW_ACTION_RESET,
    "<Actions>/terminal-window/reset",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Reset"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_reset),
  },
  {
    TERMINAL_WINDOW_ACTION_RESET_AND_CLEAR,
    "<Actions>/terminal-window/reset-and-clear",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Clear Scrollback and Reset"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_reset_and_clear),
  },
  {
    TERMINAL_WINDOW_ACTION_TABS_MENU,
    "<Actions>/terminal-window/tabs-menu",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("T_abs"),
    NULL,
    NULL,
    NULL,
  },
  {
    TERMINAL_WINDOW_ACTION_PREV_TAB,
    "<Actions>/terminal-window/prev-tab",
    "<control>Page_Up",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Previous Tab"),
    N_ ("Switch to previous tab"),
    "go-previous",
    G_CALLBACK (terminal_window_action_prev_tab),
  },
  {
    TERMINAL_WINDOW_ACTION_NEXT_TAB,
    "<Actions>/terminal-window/next-tab",
    "<control>Page_Down",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Next Tab"),
    N_ ("Switch to next tab"),
    "go-next",
    G_CALLBACK (terminal_window_action_next_tab),
  },
  {
    TERMINAL_WINDOW_ACTION_LAST_ACTIVE_TAB,
    "<Actions>/terminal-window/last-active-tab",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("Last _Active Tab"),
    N_ ("Switch to last active tab"),
    NULL,
    G_CALLBACK (terminal_window_action_last_active_tab),
  },
  {
    TERMINAL_WINDOW_ACTION_MOVE_TAB_LEFT,
    "<Actions>/terminal-window/move-tab-left",
    "<control><shift>Page_Up",
    XFCE_GTK_MENU_ITEM,
    N_ ("Move Tab _Left"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_move_tab_left),
  },
  {
    TERMINAL_WINDOW_ACTION_MOVE_TAB_RIGHT,
    "<Actions>/terminal-window/move-tab-right",
    "<control><shift>Page_Down",
    XFCE_GTK_MENU_ITEM,
    N_ ("Move Tab _Right"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_move_tab_right),
  },
  {
    TERMINAL_WINDOW_ACTION_SPLIT_HORIZONTAL,
    "<Actions>/terminal-window/split-horizontal",
    "<control><shift>h",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Split _Horizontally"),
    N_ ("Split the terminal side by side"),
    "view-split-left-right",
    G_CALLBACK (terminal_window_action_split_horizontal),
  },
  {
    TERMINAL_WINDOW_ACTION_SPLIT_VERTICAL,
    "<Actions>/terminal-window/split-vertical",
    "<control><shift>e",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Split _Vertically"),
    N_ ("Split the terminal one above the other"),
    "view-split-top-bottom",
    G_CALLBACK (terminal_window_action_split_vertical),
  },
  {
    TERMINAL_WINDOW_ACTION_CLOSE_PANE,
    "<Actions>/terminal-window/close-pane",
    "<control><shift>x",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Close Pane"),
    N_ ("Close the current split pane"),
    "window-close",
    G_CALLBACK (terminal_window_action_close_pane),
  },
  {
    TERMINAL_WINDOW_ACTION_START_TMUX,
    "<Actions>/terminal-window/start-tmux",
    "<control><shift>m",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("Start tmux Session"),
    N_ ("Run the configured tmux start command in this pane"),
    "utilities-terminal",
    G_CALLBACK (terminal_window_action_start_tmux),
  },
  {
    TERMINAL_WINDOW_ACTION_FOCUS_UP,
    "<Actions>/terminal-window/focus-up",
    "<alt>Up",
    XFCE_GTK_MENU_ITEM,
    N_ ("Focus Pane _Up"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_focus_up),
  },
  {
    TERMINAL_WINDOW_ACTION_FOCUS_DOWN,
    "<Actions>/terminal-window/focus-down",
    "<alt>Down",
    XFCE_GTK_MENU_ITEM,
    N_ ("Focus Pane _Down"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_focus_down),
  },
  {
    TERMINAL_WINDOW_ACTION_FOCUS_LEFT,
    "<Actions>/terminal-window/focus-left",
    "<alt>Left",
    XFCE_GTK_MENU_ITEM,
    N_ ("Focus Pane _Left"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_focus_left),
  },
  {
    TERMINAL_WINDOW_ACTION_FOCUS_RIGHT,
    "<Actions>/terminal-window/focus-right",
    "<alt>Right",
    XFCE_GTK_MENU_ITEM,
    N_ ("Focus Pane _Right"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_focus_right),
  },
  {
    TERMINAL_WINDOW_ACTION_FOCUS_NEXT,
    "<Actions>/terminal-window/focus-next",
    "<control>Tab",
    XFCE_GTK_MENU_ITEM,
    N_ ("Next Pane"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_focus_next),
  },
  {
    TERMINAL_WINDOW_ACTION_FOCUS_PREV,
    "<Actions>/terminal-window/focus-prev",
    "<control><shift>Tab",
    XFCE_GTK_MENU_ITEM,
    N_ ("Previous Pane"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_focus_prev),
  },
  {
    TERMINAL_WINDOW_ACTION_MOVE_PANE_UP,
    "<Actions>/terminal-window/move-pane-up",
    "<control><shift>Up",
    XFCE_GTK_MENU_ITEM,
    N_ ("Move Pane _Up"),
    N_ ("Move the current split pane up"),
    NULL,
    G_CALLBACK (terminal_window_action_move_pane_up),
  },
  {
    TERMINAL_WINDOW_ACTION_MOVE_PANE_DOWN,
    "<Actions>/terminal-window/move-pane-down",
    "<control><shift>Down",
    XFCE_GTK_MENU_ITEM,
    N_ ("Move Pane _Down"),
    N_ ("Move the current split pane down"),
    NULL,
    G_CALLBACK (terminal_window_action_move_pane_down),
  },
  {
    TERMINAL_WINDOW_ACTION_MOVE_PANE_LEFT,
    "<Actions>/terminal-window/move-pane-left",
    "<control><shift>Left",
    XFCE_GTK_MENU_ITEM,
    N_ ("Move Pane _Left"),
    N_ ("Move the current split pane left"),
    NULL,
    G_CALLBACK (terminal_window_action_move_pane_left),
  },
  {
    TERMINAL_WINDOW_ACTION_MOVE_PANE_RIGHT,
    "<Actions>/terminal-window/move-pane-right",
    "<control><shift>Right",
    XFCE_GTK_MENU_ITEM,
    N_ ("Move Pane _Right"),
    N_ ("Move the current split pane right"),
    NULL,
    G_CALLBACK (terminal_window_action_move_pane_right),
  },
  {
    TERMINAL_WINDOW_ACTION_HELP_MENU,
    "<Actions>/terminal-window/help-menu",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Help"),
    NULL,
    NULL,
    NULL,
  },
  {
    TERMINAL_WINDOW_ACTION_CONTENTS,
    "<Actions>/terminal-window/contents",
    "F1",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_Contents"),
    N_ ("Display help contents"),
    "help-browser",
    G_CALLBACK (terminal_window_action_contents),
  },
  {
    TERMINAL_WINDOW_ACTION_ABOUT,
    "<Actions>/terminal-window/about",
    "",
    XFCE_GTK_IMAGE_MENU_ITEM,
    N_ ("_About"),
    NULL,
    "help-about",
    G_CALLBACK (terminal_window_action_about),
  },
  {
    TERMINAL_WINDOW_ACTION_ZOOM_MENU,
    "<Actions>/terminal-window/zoom-menu",
    "",
    XFCE_GTK_MENU_ITEM,
    N_ ("_Zoom"),
    NULL,
    NULL,
    NULL,
  },
  {
    TERMINAL_WINDOW_ACTION_TOGGLE_MENUBAR,
    "<Actions>/terminal-window/toggle-menubar",
    "F10",
    XFCE_GTK_MENU_ITEM,
    N_ ("Show Menubar Temporarily"),
    "",
    NULL,
    G_CALLBACK (terminal_window_action_toggle_menubar),
  },
  {
    TERMINAL_WINDOW_ACTION_SHOW_MENUBAR,
    "<Actions>/terminal-window/show-menubar",
    "",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Show _Menubar"),
    N_ ("Show/hide the menubar"),
    NULL,
    G_CALLBACK (terminal_window_action_show_menubar),
  },
  {
    TERMINAL_WINDOW_ACTION_SHOW_TOOLBAR,
    "<Actions>/terminal-window/show-toolbar",
    "",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Show _Toolbar"),
    N_ ("Show/hide the toolbar"),
    NULL,
    G_CALLBACK (terminal_window_action_toggle_toolbar),
  },
  {
    TERMINAL_WINDOW_ACTION_SHOW_BORDERS,
    "<Actions>/terminal-window/show-borders",
    "",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Show Window _Borders"),
    N_ ("Show/hide the window decorations"),
    NULL,
    G_CALLBACK (terminal_window_action_toggle_borders),
  },
  {
    TERMINAL_WINDOW_ACTION_FULLSCREEN,
    "<Actions>/terminal-window/fullscreen",
    "F11",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("_Fullscreen"),
    N_ ("Toggle fullscreen mode"),
    "view-fullscreen",
    G_CALLBACK (terminal_window_action_fullscreen),
  },
  {
    TERMINAL_WINDOW_ACTION_READ_ONLY,
    "<Actions>/terminal-window/read-only",
    "",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("_Read-Only"),
    N_ ("Toggle read-only mode"),
    NULL,
    G_CALLBACK (terminal_window_action_readonly),
  },
  {
    TERMINAL_WINDOW_ACTION_SCROLL_ON_OUTPUT,
    "<Actions>/terminal-window/scroll-on-output",
    "",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Scroll on _Output"),
    N_ ("Toggle scroll on output"),
    NULL,
    G_CALLBACK (terminal_window_action_scroll_on_output),
  },
  /* used for changing the accelerator keys via the ShortcutsEditor, the logic is still handle by GtkActionEntries */
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_1,
    "<Actions>/terminal-window/goto-tab-1",
    "<Alt>1",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 1"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_2,
    "<Actions>/terminal-window/goto-tab-2",
    "<Alt>2",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 2"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_3,
    "<Actions>/terminal-window/goto-tab-3",
    "<Alt>3",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 3"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_4,
    "<Actions>/terminal-window/goto-tab-4",
    "<Alt>4",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 4"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_5,
    "<Actions>/terminal-window/goto-tab-5",
    "<Alt>5",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 5"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_6,
    "<Actions>/terminal-window/goto-tab-6",
    "<Alt>6",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 6"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_7,
    "<Actions>/terminal-window/goto-tab-7",
    "<Alt>7",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 7"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_8,
    "<Actions>/terminal-window/goto-tab-8",
    "<Alt>8",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 8"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
  {
    TERMINAL_WINDOW_ACTION_GOTO_TAB_9,
    "<Actions>/terminal-window/goto-tab-9",
    "<Alt>9",
    XFCE_GTK_CHECK_MENU_ITEM,
    N_ ("Go to Tab 9"),
    NULL,
    NULL,
    G_CALLBACK (terminal_window_action_do_nothing),
  },
};

#define get_action_entry(id) xfce_gtk_get_action_entry_by_id (action_entries, G_N_ELEMENTS (action_entries), id)



G_DEFINE_TYPE_WITH_CODE (TerminalWindow, terminal_window, GTK_TYPE_WINDOW, G_ADD_PRIVATE (TerminalWindow))



static void
terminal_window_class_init (TerminalWindowClass *klass)
{
  GtkWidgetClass *gtkwidget_class;
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = terminal_window_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->window_state_event = terminal_window_state_event;
  gtkwidget_class->delete_event = terminal_window_delete_event;
  gtkwidget_class->style_set = terminal_window_style_set;
  gtkwidget_class->scroll_event = terminal_window_scroll_event;
  gtkwidget_class->map_event = terminal_window_map_event;
  gtkwidget_class->focus_in_event = terminal_window_focus_in_event;
  gtkwidget_class->focus_out_event = terminal_window_focus_out_event;
  gtkwidget_class->key_press_event = terminal_window_key_press_event;
  gtkwidget_class->key_release_event = terminal_window_key_release_event;

  xfce_gtk_translate_action_entries (action_entries, G_N_ELEMENTS (action_entries));

  /**
   * TerminalWindow::new-window
   **/
  window_signals[NEW_WINDOW] =
    g_signal_new (I_ ("new-window"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRING);

  /**
   * TerminalWindow::new-window-with-screen:
   **/
  window_signals[NEW_WINDOW_WITH_SCREEN] =
    g_signal_new (I_ ("new-window-with-screen"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  _terminal_marshal_VOID__OBJECT_INT_INT,
                  G_TYPE_NONE, 3,
                  G_TYPE_OBJECT,
                  G_TYPE_INT, G_TYPE_INT);

  /* initialize quark */
  tabs_menu_action_quark = g_quark_from_static_string ("tabs-menu-item");
}



static void
terminal_window_init (TerminalWindow *window)
{
  gboolean always_show_tabs;
  GdkScreen *screen;
  GdkVisual *visual;
  GtkStyleContext *context;
  GtkToolItem *item;

  window->priv = terminal_window_get_instance_private (window);

  window->priv->preferences = terminal_preferences_get ();

  window->priv->font = NULL;
  window->priv->zoom = TERMINAL_ZOOM_LEVEL_DEFAULT;
  window->priv->closed_tabs_list = g_queue_new ();

  /* setup dnd support for widgets other than screen: menubar, toolbar, etc.*/
  gtk_drag_dest_set (GTK_WIDGET (window),
                     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP,
                     targets, G_N_ELEMENTS (targets),
                     GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE);
  g_signal_connect_swapped (G_OBJECT (window), "drag-data-received",
                            G_CALLBACK (terminal_window_notebook_drag_data_received), NULL);

  /* try to set the rgba colormap so vte can use real transparency */
  screen = gtk_window_get_screen (GTK_WINDOW (window));
  visual = gdk_screen_get_rgba_visual (screen);
  if (visual != NULL)
    gtk_widget_set_visual (GTK_WIDGET (window), visual);

  /* required for vte transparency support: see https://bugzilla.gnome.org/show_bug.cgi?id=729884 */
  gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);

  window->priv->accel_group = gtk_accel_group_new ();
  xfce_gtk_accel_map_add_entries (action_entries, G_N_ELEMENTS (action_entries));
  xfce_gtk_accel_group_connect_action_entries (window->priv->accel_group,
                                               action_entries,
                                               G_N_ELEMENTS (action_entries),
                                               window);

  /* the tabs that exist at startup get their panes back from the saved layout;
   * later tabs are new ones and start with a single pane. A negative index means
   * that there is nothing to put back, which is also how the idle handler ends
   * the restore phase. */
  if (terminal_window_panes_layouts_enabled (window))
    {
      g_object_get (G_OBJECT (window->priv->preferences), "misc-panes-layouts",
                    &window->priv->panes_layouts, NULL);
      window->priv->panes_layout_index = 0;
    }
  else
    window->priv->panes_layout_index = -1;

  window->priv->panes_layout_idle = g_idle_add (terminal_window_panes_layout_restore_idle, window);

  gtk_window_add_accel_group (GTK_WINDOW (window), window->priv->accel_group);

  window->menubar = gtk_menu_bar_new ();
  terminal_window_create_menu (window, TERMINAL_WINDOW_ACTION_FILE_MENU, G_CALLBACK (terminal_window_update_file_menu));
  terminal_window_create_menu (window, TERMINAL_WINDOW_ACTION_EDIT_MENU, G_CALLBACK (terminal_window_update_edit_menu));
  terminal_window_create_menu (window, TERMINAL_WINDOW_ACTION_VIEW_MENU, G_CALLBACK (terminal_window_update_view_menu));
  terminal_window_create_menu (window, TERMINAL_WINDOW_ACTION_TERMINAL_MENU, G_CALLBACK (terminal_window_update_terminal_menu));
  terminal_window_create_menu (window, TERMINAL_WINDOW_ACTION_TABS_MENU, G_CALLBACK (terminal_window_update_tabs_menu));
  terminal_window_create_menu (window, TERMINAL_WINDOW_ACTION_HELP_MENU, G_CALLBACK (terminal_window_update_help_menu));
  gtk_widget_show_all (window->menubar);

  window->toolbar = gtk_toolbar_new ();
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEW_TAB), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEW_WINDOW), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), gtk_separator_tool_item_new (), 2);
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_COPY), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PASTE), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), gtk_separator_tool_item_new (), 5);
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SEARCH), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), gtk_separator_tool_item_new (), 7);
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PREV_TAB), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEXT_TAB), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), gtk_separator_tool_item_new (), 10);
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PREFERENCES), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), gtk_separator_tool_item_new (), 12);
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CONTENTS), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  /* expand last separator */
  item = gtk_separator_tool_item_new ();
  gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (item), FALSE);
  gtk_tool_item_set_expand (item, TRUE);
  gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), item, 14);
  xfce_gtk_tool_button_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_FULLSCREEN), G_OBJECT (window), GTK_TOOLBAR (window->toolbar));
  gtk_widget_show_all (window->toolbar);

  window->priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (window), window->priv->vbox);

  /* avoid transparent widgets, such as menubar or tabbar */
  context = gtk_widget_get_style_context (window->priv->vbox);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_BACKGROUND);

  /* allocate the notebook for the terminal screens */
  g_object_get (G_OBJECT (window->priv->preferences), "misc-always-show-tabs", &always_show_tabs, NULL);
#if LIBXFCE4UI_CHECK_VERSION(4, 21, 1)
  window->priv->notebook = g_object_new (XFCE_TYPE_NOTEBOOK,
#else
  window->priv->notebook = g_object_new (GTK_TYPE_NOTEBOOK,
#endif
                                         "scrollable", TRUE,
                                         "show-border", FALSE,
                                         "show-tabs", always_show_tabs,
                                         NULL);
  gtk_widget_add_events (window->priv->notebook, GDK_SCROLL_MASK);
  g_signal_connect_swapped (G_OBJECT (window->priv->preferences), "notify::misc-always-show-tabs",
                            G_CALLBACK (terminal_window_notebook_show_tabs), window);

  /* set the notebook group id */
  gtk_notebook_set_group_name (GTK_NOTEBOOK (window->priv->notebook), window_notebook_group);

  /* set notebook tabs style */
  gtk_widget_set_name (window->priv->notebook, NOTEBOOK_NAME);
  terminal_window_update_slim_tabs (window);

  /* signals */
  g_signal_connect (G_OBJECT (window->priv->notebook), "switch-page",
                    G_CALLBACK (terminal_window_notebook_page_switched), window);
  g_signal_connect (G_OBJECT (window->priv->notebook), "page-removed",
                    G_CALLBACK (terminal_window_notebook_page_removed), window);
  g_signal_connect (G_OBJECT (window->priv->notebook), "page-added",
                    G_CALLBACK (terminal_window_notebook_page_added), window);
  g_signal_connect (G_OBJECT (window->priv->notebook), "create-window",
                    G_CALLBACK (terminal_window_notebook_create_window), window);
  g_signal_connect (G_OBJECT (window->priv->notebook), "button-press-event",
                    G_CALLBACK (terminal_window_notebook_button_press_event), window);
  g_signal_connect (G_OBJECT (window->priv->notebook), "button-release-event",
                    G_CALLBACK (terminal_window_notebook_button_release_event), window);
#if !LIBXFCE4UI_CHECK_VERSION(4, 21, 1)
  g_signal_connect (G_OBJECT (window->priv->notebook), "scroll-event",
                    G_CALLBACK (terminal_window_notebook_scroll_event), window);
#endif

  gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->menubar, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->toolbar, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->notebook, TRUE, TRUE, 0);
  gtk_widget_show_all (window->priv->vbox);

  /* create encoding action */
  window->priv->encoding_action = terminal_encoding_action_new ("set-encoding", _("Set _Encoding"));
  g_signal_connect (G_OBJECT (window->priv->encoding_action), "encoding-changed",
                    G_CALLBACK (terminal_window_action_set_encoding), window);

  /* monitor the shortcuts-no-mnemonics setting */
  terminal_window_update_mnemonic_modifier (window);
  g_signal_connect_swapped (G_OBJECT (window->priv->preferences), "notify::shortcuts-no-mnemonics",
                            G_CALLBACK (terminal_window_update_mnemonic_modifier), window);

  window->fullscreen_supported = TRUE;
#ifdef ENABLE_X11
  if (GDK_IS_X11_SCREEN (screen))
    {
      /* setup fullscreen mode */
      if (!gdk_x11_screen_supports_net_wm_hint (screen, gdk_atom_intern ("_NET_WM_STATE_FULLSCREEN", FALSE)))
        window->fullscreen_supported = FALSE;
    }
#endif
}



static void
terminal_window_action_prefs_died (gpointer user_data,
                                   GObject *where_the_object_was)
{
  TerminalWindow *window = TERMINAL_WINDOW (user_data);

  window->priv->preferences_dialog = NULL;
  window->priv->n_child_windows--;

  if (window->priv->drop_down)
    terminal_util_activate_window (GTK_WINDOW (window));
}



static void
terminal_window_finalize (GObject *object)
{
  TerminalWindow *window = TERMINAL_WINDOW (object);

  if (window->priv->panes_layout_idle != 0)
    g_source_remove (window->priv->panes_layout_idle);
  g_strfreev (window->priv->panes_layouts);

  /* disconnect prefs signal handlers */
  g_signal_handlers_disconnect_by_func (G_OBJECT (window->priv->preferences),
                                        G_CALLBACK (terminal_window_update_mnemonic_modifier), window);
  g_signal_handlers_disconnect_by_func (G_OBJECT (window->priv->preferences),
                                        G_CALLBACK (terminal_window_notebook_show_tabs), window);

  if (window->priv->preferences_dialog != NULL)
    {
      g_object_weak_unref (G_OBJECT (window->priv->preferences_dialog),
                           terminal_window_action_prefs_died, window);
      gtk_widget_destroy (window->priv->preferences_dialog);
    }
  g_object_unref (G_OBJECT (window->priv->preferences));
  g_object_unref (G_OBJECT (window->priv->accel_group));
  g_object_unref (G_OBJECT (window->priv->encoding_action));

  g_slist_free (window->priv->tabs_menu_actions);
  g_free (window->priv->font);
  g_queue_free_full (window->priv->closed_tabs_list, (GDestroyNotify) terminal_tab_attr_free);

  (*G_OBJECT_CLASS (terminal_window_parent_class)->finalize) (object);
}



static gboolean
terminal_window_delete_event (GtkWidget *widget,
                              GdkEventAny *event)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);
  GtkWidget *child;
  gint n_pages, i, response;

  response = terminal_window_confirm_close (NULL, window);

  /* disconnect remove signal if we're closing the window */
  if (response == CONFIRMED_CLOSE_WINDOW)
    {
      /* disconnect handlers for closing Set Title dialog */
      if (window->priv->title_popover != NULL)
        {
          n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
          for (i = 0; i < n_pages; i++)
            {
              child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), i);
              g_signal_handlers_disconnect_by_func (G_OBJECT (child),
                                                    G_CALLBACK (title_popover_close), window);
            }
        }

      /* remember the way the tabs are split right now: panes whose shell ended
       * by itself and panes that were closed by hand are already gone from the
       * tree, while nothing else saves after the handlers below are gone. A
       * window closed as a whole never sees a page removed, so this is the
       * only moment left to write the layout down. */
      terminal_window_save_panes_layouts (window);
      terminal_window_save_session_state (window);

      /* avoid a lot of page remove calls */
      g_signal_handlers_disconnect_by_func (G_OBJECT (window->priv->notebook),
                                            G_CALLBACK (terminal_window_notebook_page_removed), window);

      /* let gtk close the window */
      if (GTK_WIDGET_CLASS (terminal_window_parent_class)->delete_event != NULL)
        return (*GTK_WIDGET_CLASS (terminal_window_parent_class)->delete_event) (widget, event);

      return FALSE;
    }
  else if (response == CONFIRMED_CLOSE_TAB)
    terminal_window_do_close_tab (window->priv->active, window);

  return TRUE;
}



static gboolean
terminal_window_state_event (GtkWidget *widget,
                             GdkEventWindowState *event)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  /* update the fullscreen action if the fullscreen state changed by the wm */
  if ((event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) != 0
      && gtk_widget_get_visible (widget))
    {
      window->is_fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;

      /* update drop-down window geometry, otherwise it'll be incorrect */
      if (!window->is_fullscreen && window->priv->drop_down)
        terminal_window_dropdown_update_geometry (TERMINAL_WINDOW_DROPDOWN (window));
    }

  if (GTK_WIDGET_CLASS (terminal_window_parent_class)->window_state_event != NULL)
    return (*GTK_WIDGET_CLASS (terminal_window_parent_class)->window_state_event) (widget, event);

  return FALSE;
}



static void
terminal_window_style_set (GtkWidget *widget,
                           GtkStyle *previous_style)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);

  if (previous_style != NULL)
    terminal_window_size_push (window);

  (*GTK_WIDGET_CLASS (terminal_window_parent_class)->style_set) (widget, previous_style);

  /* delay the pop until after size allocate */
  if (previous_style != NULL)
    gdk_threads_add_idle (terminal_window_size_pop, window);
}



static gboolean
terminal_window_scroll_event (GtkWidget *widget,
                              GdkEventScroll *event)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);
  gboolean mouse_wheel_zoom;
  const guint modifiers = event->state & gtk_accelerator_get_default_mod_mask ();

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-mouse-wheel-zoom", &mouse_wheel_zoom, NULL);

  if (mouse_wheel_zoom && modifiers == (GDK_SHIFT_MASK | GDK_CONTROL_MASK)
      && event->direction == GDK_SCROLL_UP)
    {
      terminal_window_action_zoom_in (window);
      return TRUE;
    }

  if (mouse_wheel_zoom && modifiers == (GDK_SHIFT_MASK | GDK_CONTROL_MASK)
      && event->direction == GDK_SCROLL_DOWN)
    {
      terminal_window_action_zoom_out (window);
      return TRUE;
    }

  return FALSE;
}



static gboolean
terminal_window_map_event (GtkWidget *widget,
                           GdkEventAny *event)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);

  if (window->is_fullscreen)
    gtk_window_fullscreen (GTK_WINDOW (widget));

  return FALSE;
}



static gboolean
terminal_window_focus_in_event (GtkWidget *widget,
                                GdkEventFocus *event)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);

  /* reset activity indicator for the active tab when focusing window */
  terminal_screen_reset_activity (window->priv->active);

  /* the modifier keys may have been pressed while another window had the
   * focus */
  terminal_keys_focus_in ();

  return (*GTK_WIDGET_CLASS (terminal_window_parent_class)->focus_in_event) (widget, event);
}



static gboolean
terminal_window_focus_out_event (GtkWidget *widget,
                                 GdkEventFocus *event)
{
  /* the modifier keys may be released while another window has the focus */
  terminal_keys_focus_out ();

  return (*GTK_WIDGET_CLASS (terminal_window_parent_class)->focus_out_event) (widget, event);
}



/* Layout independent normalization of the accelerators of this window.
 *
 * Under a non-latin layout a key press such as Ctrl+Shift+T produces a cyrillic
 * keysym, so the accelerators of the window (which are stored as latin keysyms)
 * never match and they look dead. Map the key press back to the latin symbol of
 * the same physical key and run the action.
 *
 * This runs before the accelerators of the window are consulted, but only for
 * key presses that really are layout dependent: a latin key press is returned
 * untouched, so on a latin layout nothing changes at all. */
static gboolean
terminal_window_activate_layout_accel (TerminalWindow *window,
                                       GdkEventKey    *event,
                                       guint           mods_held)
{
  guint32 keyval = 0;
  guint mods = 0;
  guint mask;
  gint e;

  if (!terminal_keys_normalize_layout_accel_ext (event, mods_held, &keyval, &mods))
    return FALSE;

  mask = gtk_accelerator_get_default_mod_mask ();

  for (e = 0; e < (gint) G_N_ELEMENTS (action_entries); e++)
    {
      const XfceGtkActionEntry *entry = &action_entries[e];
      GtkAccelKey current;
      gboolean matched;

      if (entry->callback == NULL || entry->accel_path == NULL)
        continue;

      /* prefer the accelerator the user has bound in the keyboard shortcuts
       * dialog, fall back to the one from the action entry; the accelerator
       * map is keyed by the full accel path, e.g.
       * "<Actions>/terminal-window/split-horizontal" */
      memset (&current, 0, sizeof (current));
      matched = gtk_accel_map_lookup_entry (entry->accel_path, &current)
                && current.accel_key != 0
                && current.accel_key == keyval
                && (current.accel_mods & mask) == mods;

      if (!matched)
        matched = terminal_keys_accel_matches (entry->default_accelerator, keyval, mods);

      if (matched)
        {
          ((void (*) (TerminalWindow *, gpointer)) entry->callback) (window, NULL);
          return TRUE;
        }
    }

  return FALSE;
}



static gboolean
terminal_window_key_press_event (GtkWidget *widget,
                                 GdkEventKey *event)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);
  guint mods_held;

  /* F1 shows and hides a drop-down window. A normal window keeps the upstream
   * binding and opens the help, since there the key is more useful there. */
  if (TERMINAL_IS_WINDOW_DROPDOWN (window)
      && event->keyval == GDK_KEY_F1
      && (event->state & gtk_accelerator_get_default_mod_mask ()) == 0)
    {
      terminal_window_dropdown_toggle (TERMINAL_WINDOW_DROPDOWN (window), NULL, FALSE);
      return TRUE;
    }

  /* the layout switch may have swallowed a modifier out of the event state
   * (shift with Ctrl pressed first, control with Shift pressed first, alt
   * with Shift pressed first): put it back, so the accelerators below (this
   * table, the tab handler, the menu bindings, the terminal widget) see it */
  terminal_keys_key_press (event);

  mods_held = terminal_keys_modifiers_held ();

  if (terminal_window_activate_layout_accel (window, event, mods_held))
    return TRUE;

  if (xfce_gtk_handle_tab_accels (event, window->priv->accel_group, window, action_entries, G_N_ELEMENTS (action_entries)))
    return TRUE;

  return (*GTK_WIDGET_CLASS (terminal_window_parent_class)->key_press_event) (widget, event);
}



static gboolean
terminal_window_key_release_event (GtkWidget *widget,
                                   GdkEventKey *event)
{
  terminal_keys_key_release (event);

  return (*GTK_WIDGET_CLASS (terminal_window_parent_class)->key_release_event) (widget, event);
}



static gint
terminal_window_confirm_close (TerminalScreen *screen,
                               TerminalWindow *window)
{
  GtkWidget *dialog;
  GtkWidget *button;
  GtkWidget *hbox;
  GtkWidget *image;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *checkbox;
  const gchar *title;
  gchar *message;
  gchar *markup;
  gint response;
  gint i, n_tabs;
  gboolean confirm_close;

  g_object_get (G_OBJECT (window->priv->preferences), "misc-confirm-close", &confirm_close, NULL);
  if (!confirm_close)
    return (screen != NULL) ? CONFIRMED_CLOSE_TAB : CONFIRMED_CLOSE_WINDOW;

  n_tabs = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
  confirm_close = FALSE;
  for (i = 0; i < n_tabs; ++i)
    {
      TerminalPanes *panes;
      GSList *screens, *lp;

      panes = TERMINAL_PANES (gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), i));

      /* when closing a tab (screen != NULL) only check the panes of that tab */
      if (screen != NULL && !terminal_panes_contains (panes, screen))
        continue;

      screens = terminal_panes_get_screens (panes);
      for (lp = screens; lp != NULL; lp = lp->next)
        {
          if (terminal_screen_has_foreground_process (TERMINAL_SCREEN (lp->data)))
            {
              confirm_close = TRUE;
              break;
            }
        }
      g_slist_free (screens);

      if (confirm_close)
        break;
    }

  if ((screen != NULL || n_tabs < 2) && !confirm_close)
    return (screen != NULL) ? CONFIRMED_CLOSE_TAB : CONFIRMED_CLOSE_WINDOW;

  dialog = gtk_dialog_new ();
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Warning"));

  button = xfce_gtk_button_new_mixed ("gtk-cancel", _("_Cancel"));
  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_CANCEL);

  if (screen == NULL && n_tabs > 1)
    {
      /* closing window with multiple tabs */
      if (confirm_close)
        {
          /* and process running */
          message = g_strdup (_("There are still processes running in some tabs.\n"
                                "Closing this window will kill all of them."));
        }
      else
        {
          /* and no process running */
          message = g_strdup_printf (_("This window has %d tabs open. Closing this window\n"
                                       "will also close all its tabs."),
                                     n_tabs);
        }

      title = _("Close all tabs?");
    }
  else
    {
      if (screen != NULL)
        {
          /* closing a tab, and process running */
          message = g_strdup (_("There is still a process running.\n"
                                "Closing this tab will kill it."));
          title = _("Close tab?");
        }
      else
        {
          /* closing a single tab window, and process running */
          message = g_strdup (_("There is still a process running.\n"
                                "Closing this window will kill it."));
          title = _("Close window?");
        }
    }

  if (screen != NULL || n_tabs > 1)
    {
      button = xfce_gtk_button_new_mixed ("window-close", _("Close T_ab"));
      gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_CLOSE);
      gtk_widget_grab_focus (button);
    }

  if (screen == NULL)
    {
      button = xfce_gtk_button_new_mixed ("application-exit", _("Close _Window"));
      gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_YES);
      gtk_widget_grab_focus (button);
    }

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 8);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), hbox, TRUE, TRUE, 0);

  image = gtk_image_new_from_icon_name ("dialog-warning", GTK_ICON_SIZE_DIALOG);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (image, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

  markup = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s", title, message);
  g_free (message);

  label = g_object_new (GTK_TYPE_LABEL,
                        "label", markup,
                        "use-markup", TRUE,
                        "xalign", 0.0,
                        NULL);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

  g_free (markup);

  checkbox = gtk_check_button_new_with_mnemonic (_("Do _not ask me again"));
  gtk_box_pack_start (GTK_BOX (vbox), checkbox, FALSE, FALSE, 0);

  gtk_widget_show_all (dialog);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response == GTK_RESPONSE_YES)
    {
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox)))
        {
          g_object_set (G_OBJECT (window->priv->preferences),
                        "misc-confirm-close", FALSE,
                        NULL);
        }
    }

  gtk_widget_destroy (dialog);

  if (response == GTK_RESPONSE_YES)
    return CONFIRMED_CLOSE_WINDOW;
  if (response == GTK_RESPONSE_CLOSE)
    return CONFIRMED_CLOSE_TAB;
  return CONFIRMED_NONE;
}



static void
terminal_window_size_push (TerminalWindow *window)
{
  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  if (window->priv->active != NULL)
    {
      terminal_screen_get_size (window->priv->active,
                                &window->priv->grid_width,
                                &window->priv->grid_height);
    }
}



static gboolean
terminal_window_size_pop (gpointer data)
{
  TerminalWindow *window = TERMINAL_WINDOW (data);

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  if (window->priv->active != NULL)
    {
      terminal_window_set_size_force_grid (window, window->priv->active,
                                           window->priv->grid_width,
                                           window->priv->grid_height);
    }

  return FALSE;
}



static void
terminal_window_set_size_force_grid (TerminalWindow *window,
                                     TerminalScreen *screen,
                                     glong force_grid_width,
                                     glong force_grid_height)
{
  GdkWindow *gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (TERMINAL_IS_SCREEN (screen));

  /* required to get the char height/width right */
  if (gtk_widget_get_realized (GTK_WIDGET (screen))
      && gdk_window != NULL
      && (gdk_window_get_state (gdk_window) & (GDK_WINDOW_STATE_FULLSCREEN | WINDOW_STATE_TILED)) == 0
      && !window->priv->drop_down)
    {
      terminal_screen_force_resize_window (screen, GTK_WINDOW (window),
                                           force_grid_width, force_grid_height);
    }
}



static void
terminal_window_update_slim_tabs (TerminalWindow *window)
{
  GdkScreen *screen = gtk_window_get_screen (GTK_WINDOW (window));
  GtkCssProvider *provider;
  gboolean slim_tabs;

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-slim-tabs", &slim_tabs,
                NULL);
  if (slim_tabs)
    {
      provider = gtk_css_provider_new ();
      gtk_style_context_add_provider_for_screen (screen,
                                                 GTK_STYLE_PROVIDER (provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      gtk_css_provider_load_from_data (provider, CSS_SLIM_TABS, -1, NULL);
      g_object_unref (provider);
    }
}



static void
terminal_window_update_mnemonic_modifier (TerminalWindow *window)
{
  gboolean no_mnemonics;

  g_object_get (G_OBJECT (window->priv->preferences),
                "shortcuts-no-mnemonics", &no_mnemonics,
                NULL);
  if (no_mnemonics)
    gtk_window_set_mnemonic_modifier (GTK_WINDOW (window), GDK_MODIFIER_MASK & ~GDK_RELEASE_MASK);
  else
    gtk_window_set_mnemonic_modifier (GTK_WINDOW (window), GDK_MOD1_MASK);
}



static void
terminal_window_notebook_page_switched (GtkNotebook *notebook,
                                        GtkWidget *page,
                                        guint page_num,
                                        TerminalWindow *window)
{
  TerminalScreen *active;
  const gchar *encoding;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (page == NULL || TERMINAL_IS_PANES (page));

  /* the notebook page holds a pane tree, from which we take the active terminal */
  active = (page != NULL && TERMINAL_IS_PANES (page))
             ? terminal_panes_get_active (TERMINAL_PANES (page))
             : NULL;

  /* only update when really changed */
  if (G_LIKELY (window->priv->active != active))
    {
      /* store last and set new active tab */
      window->priv->last_active = window->priv->active;
      window->priv->active = active;

      /* set the new window title */
      terminal_window_notify_title (active, NULL, window);

      if (active != NULL)
        {
          /* reset the activity counter */
          terminal_screen_reset_activity (active);

          /* set charset for menu */
          encoding = terminal_screen_get_encoding (window->priv->active);
          terminal_encoding_action_set_charset (window->priv->encoding_action, encoding);
          terminal_screen_widget_append_accels (active, window->priv->accel_group);
        }
    }
}



static void
terminal_window_close_tab_request (TerminalScreen *screen,
                                   TerminalWindow *window)
{
  if (terminal_window_confirm_close (screen, window) == CONFIRMED_CLOSE_TAB)
    terminal_window_do_close_tab (screen, window);
}



static void
terminal_window_notebook_page_added (GtkNotebook *notebook,
                                     GtkWidget *child,
                                     guint page_num,
                                     TerminalWindow *window)
{
  TerminalPanes *panes;
  GSList *screens, *lp;
  glong w, h;

  g_return_if_fail (TERMINAL_IS_PANES (child));
  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (GTK_IS_NOTEBOOK (notebook));
  g_return_if_fail (window->priv->notebook == GTK_WIDGET (notebook));

  panes = TERMINAL_PANES (child);
  screens = terminal_panes_get_screens (panes);

  /* this pane tree belongs to this window now */
  g_signal_connect (G_OBJECT (panes), "active-screen-changed",
                    G_CALLBACK (terminal_window_panes_active_screen_changed), window);

  /* put the panes of this tab back the way they were before a restart */
  if (window->priv->panes_layout_index >= 0
      && window->priv->panes_layouts != NULL
      && window->priv->panes_layouts[window->priv->panes_layout_index] != NULL)
    {
      const gchar *record = window->priv->panes_layouts[window->priv->panes_layout_index];

      /* the shell of the first pane is started after this handler returns, so
       * this is the last chance to send it to the directory it was left in */
      if (screens != NULL)
        {
          TerminalScreen *first = TERMINAL_SCREEN (screens->data);

          if (!terminal_screen_has_explicit_directory (first))
            {
              gchar *directory = terminal_window_saved_pane_directory (record, 0);

              if (directory != NULL && *directory != '\0'
                  && g_file_test (directory, G_FILE_TEST_IS_DIR))
                terminal_screen_set_working_directory (first, directory);

              g_free (directory);
            }
        }

      terminal_window_restore_panes_layout (window, panes, record);
      terminal_window_restore_panes_commands (panes, record);

      /* apply the title of the pane that came back */
      terminal_window_apply_title (window, terminal_panes_get_active (panes));

      window->priv->panes_layout_index++;
    }

  /* connect the screen signals for every pane in the tree */
  for (lp = screens; lp != NULL; lp = lp->next)
    {
      TerminalScreen *screen = TERMINAL_SCREEN (lp->data);

      terminal_window_setup_screen (window, screen);

      /* release to the grid size applies */
      gtk_widget_realize (GTK_WIDGET (screen));
    }

  /* match zoom and font */
  if (window->priv->font || window->priv->zoom != TERMINAL_ZOOM_LEVEL_DEFAULT)
    {
      for (lp = screens; lp != NULL; lp = lp->next)
        terminal_screen_update_font (TERMINAL_SCREEN (lp->data));
    }

  if (G_LIKELY (window->priv->active != NULL))
    {
      /* match the size of the active screen */
      terminal_screen_get_size (window->priv->active, &w, &h);
      for (lp = screens; lp != NULL; lp = lp->next)
        terminal_screen_set_size (TERMINAL_SCREEN (lp->data), w, h);

      /* show the tabs when needed */
      terminal_window_notebook_show_tabs (window);
    }
  else if (G_UNLIKELY (window->priv->drop_down))
    {
      TerminalScreen *first;

      /* try to calculate a decent grid size based on the info we have now */
      first = (screens != NULL) ? TERMINAL_SCREEN (screens->data) : NULL;
      if (first != NULL)
        {
          terminal_window_dropdown_get_size (TERMINAL_WINDOW_DROPDOWN (window), first, &w, &h);
          terminal_screen_set_size (first, w, h);
        }
    }

  for (lp = screens; lp != NULL; lp = lp->next)
    terminal_screen_widget_append_accels (TERMINAL_SCREEN (lp->data), window->priv->accel_group);

  g_slist_free (screens);

  /* update the go-to accelerators */
  if (window->priv->tabs_menu != NULL)
    terminal_window_update_tabs_menu (window, window->priv->tabs_menu);
}



static void
terminal_window_notebook_page_removed (GtkNotebook *notebook,
                                       GtkWidget *child,
                                       guint page_num,
                                       TerminalWindow *window)
{
  TerminalPanes *panes;
  GSList *screens, *lp;
  GtkWidget *new_page;
  gint new_page_num;
  gint npages;

  g_return_if_fail (TERMINAL_IS_PANES (child));
  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  panes = TERMINAL_PANES (child);
  screens = terminal_panes_get_screens (panes);

  /* this pane tree no longer belongs to this window */
  g_signal_handlers_disconnect_by_func (G_OBJECT (panes),
                                        terminal_window_panes_active_screen_changed, window);

  /* unset the go menu item */
  g_object_set_qdata (G_OBJECT (child), tabs_menu_action_quark, NULL);

  /* disconnect the window-specific signals on all panes of the page */
  for (lp = screens; lp != NULL; lp = lp->next)
    {
      GObject *screen = G_OBJECT (lp->data);

      g_signal_handlers_disconnect_by_func (screen,
                                            terminal_window_get_context_menu, window);
      g_signal_handlers_disconnect_by_func (screen,
                                            terminal_window_notify_title, window);
      g_signal_handlers_disconnect_by_func (screen,
                                            terminal_window_close_tab_request, window);
      g_signal_handlers_disconnect_by_func (screen,
                                            terminal_window_notebook_drag_data_received, window);
      g_signal_handlers_disconnect_by_func (screen,
                                            terminal_window_screen_focus_in, window);
    }

  /* clear last_active tab if it has just been closed */
  if (window->priv->last_active != NULL &&
      g_slist_find (screens, window->priv->last_active) != NULL)
    window->priv->last_active = NULL;

  g_slist_free (screens);

  /* set tab visibility */
  npages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
  if (G_UNLIKELY (npages == 0))
    {
      if (TERMINAL_IS_WINDOW_DROPDOWN (window)
          && !gtk_widget_in_destruction (GTK_WIDGET (window)))
        {
          gboolean keep_alive = FALSE;

          g_object_get (G_OBJECT (window->priv->preferences),
                        "dropdown-keep-alive", &keep_alive, NULL);

          if (keep_alive)
            {
              /* the active screen is the one going away; forget it, otherwise
               * add_panes() would size the fresh tab after that dead screen
               * instead of after the monitor geometry */
              window->priv->active = NULL;

              /* A drop-down window that keeps its shell: its shell ending
               * must not take the panel (and the status icon) away, so a fresh
               * tab is opened instead.  It is done in an idle because the
               * notebook is still removing the page here. */
              g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                               terminal_window_add_fresh_tab_idle,
                               g_object_ref (window),
                               g_object_unref);
              return;
            }
        }

      /* no tabs, destroy the window */
      gtk_widget_destroy (GTK_WIDGET (window));
      return;
    }

  /* the panes of the remaining tabs changed, remember them */
  terminal_window_save_panes_layouts (window);
  terminal_window_save_session_state (window);

  /* show the tabs when needed */
  terminal_window_notebook_show_tabs (window);

  /* send a signal about switching to another tab */
  new_page_num = gtk_notebook_get_current_page (GTK_NOTEBOOK (window->priv->notebook));
  new_page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), new_page_num);
  terminal_window_notebook_page_switched (notebook, new_page, new_page_num, window);

  /* update the go-to accelerators */
  if (window->priv->tabs_menu != NULL)
    terminal_window_update_tabs_menu (window, window->priv->tabs_menu);
}



/**
 * terminal_window_screen_get_panes:
 *
 * Walks up the widget tree of @screen to find the #TerminalPanes
 * container it belongs to. Returns %NULL if the screen is not part
 * of a pane tree.
 **/
static TerminalPanes *
terminal_window_screen_get_panes (TerminalScreen *screen)
{
  GtkWidget *widget;

  g_return_val_if_fail (TERMINAL_IS_SCREEN (screen), NULL);

  widget = GTK_WIDGET (screen);
  while (widget != NULL)
    {
      if (TERMINAL_IS_PANES (widget))
        return TERMINAL_PANES (widget);
      widget = gtk_widget_get_parent (widget);
    }

  return NULL;
}



/**
 * terminal_window_screen_get_page:
 *
 * Returns the notebook page (a #TerminalPanes widget) that contains
 * @screen, or %NULL.
 **/
static GtkWidget *
terminal_window_screen_get_page (TerminalWindow *window,
                                 TerminalScreen *screen)
{
  GtkNotebook *notebook;
  GSList *screens;
  gint npages, n;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), NULL);
  g_return_val_if_fail (screen == NULL || TERMINAL_IS_SCREEN (screen), NULL);

  if (screen == NULL)
    return NULL;

  notebook = GTK_NOTEBOOK (window->priv->notebook);
  npages = gtk_notebook_get_n_pages (notebook);

  for (n = 0; n < npages; n++)
    {
      GtkWidget *page = gtk_notebook_get_nth_page (notebook, n);

      if (!TERMINAL_IS_PANES (page))
        continue;

      screens = terminal_panes_get_screens (TERMINAL_PANES (page));
      if (g_slist_find (screens, screen) != NULL)
        {
          g_slist_free (screens);
          return page;
        }
      g_slist_free (screens);
    }

  return NULL;
}



static TerminalPanes *
terminal_window_get_current_panes (TerminalWindow *window)
{
  GtkWidget *page;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), NULL);

  page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook),
                                    gtk_notebook_get_current_page (GTK_NOTEBOOK (window->priv->notebook)));

  if (page == NULL || !TERMINAL_IS_PANES (page))
    return NULL;

  return TERMINAL_PANES (page);
}



/**
 * terminal_window_setup_screen:
 *
 * Connects the window-specific signals to a newly created screen
 * (used both for new tabs and for screens added by a split).
 **/
static void
terminal_window_setup_screen (TerminalWindow *window,
                              TerminalScreen *screen)
{
  /* connect the screen signals */
  g_signal_connect (G_OBJECT (screen), "get-context-menu",
                    G_CALLBACK (terminal_window_get_context_menu), window);
  g_signal_connect (G_OBJECT (screen), "notify::title",
                    G_CALLBACK (terminal_window_notify_title), window);
  g_signal_connect (G_OBJECT (screen), "close-tab-request",
                    G_CALLBACK (terminal_window_close_tab_request), window);
  g_signal_connect (G_OBJECT (screen), "drag-data-received",
                    G_CALLBACK (terminal_window_notebook_drag_data_received), window);
  g_signal_connect (G_OBJECT (screen), "focus-in-event",
                    G_CALLBACK (terminal_window_screen_focus_in), window);
}



static gboolean
terminal_window_screen_focus_in (GtkWidget *widget,
                                 GdkEventFocus *event G_GNUC_UNUSED,
                                 TerminalWindow *window)
{
  TerminalPanes *panes;

  if (!TERMINAL_IS_SCREEN (widget))
    return FALSE;

  panes = terminal_window_screen_get_panes (TERMINAL_SCREEN (widget));
  if (panes != NULL)
    terminal_panes_set_active (panes, TERMINAL_SCREEN (widget));

  return FALSE;
}



/**
 * terminal_window_set_active_screen:
 *
 * Updates the window bookkeeping (window title, encoding menu, accels)
 * after the active pane of @panes changed.
 **/
static void
terminal_window_set_active_screen (TerminalWindow *window,
                                   TerminalPanes *panes,
                                   TerminalScreen *screen)
{
  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (screen == NULL || TERMINAL_IS_SCREEN (screen));

  if (G_LIKELY (window->priv->active == screen))
    return;

  /* note: last_active is only updated on real tab switches
   * (see terminal_window_notebook_page_switched), so switching
   * panes inside a tab does not clobber it */
  window->priv->active = screen;

  /* make sure the switch to the page containing the pane happens */
  if (GTK_IS_NOTEBOOK (window->priv->notebook) && screen != NULL)
    {
      GtkWidget *page = terminal_window_screen_get_page (window, screen);

      if (page != NULL && gtk_notebook_get_current_page (GTK_NOTEBOOK (window->priv->notebook))
                          != gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), page))
        gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook),
                                       gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), page));
    }

  if (screen != NULL)
    {
      const gchar *encoding;

      /* set the new window title */
      terminal_window_apply_title (window, screen);

      /* reset the activity counter */
      terminal_screen_reset_activity (screen);

      /* set charset for menu */
      encoding = terminal_screen_get_encoding (screen);
      terminal_encoding_action_set_charset (window->priv->encoding_action, encoding);
      terminal_screen_widget_append_accels (screen, window->priv->accel_group);
    }
}



static void
terminal_window_panes_active_screen_changed (TerminalPanes *panes,
                                             TerminalScreen *screen,
                                             TerminalWindow *window)
{
  terminal_window_update_panes_label (window, panes);
  terminal_window_set_active_screen (window, panes, screen);
}



static void
terminal_window_update_panes_label (TerminalWindow *window G_GNUC_UNUSED,
                                    TerminalPanes *panes)
{
  TerminalScreen *active;
  GtkWidget *label;

  label = g_object_get_data (G_OBJECT (panes), "panes-tab-label");
  if (label == NULL)
    return;

  active = terminal_panes_get_active (panes);
  if (active != NULL)
    {
      gchar *title;

      title = terminal_screen_get_title (active);
      gtk_label_set_text (GTK_LABEL (label), title);
      gtk_widget_set_tooltip_text (label, title);
      g_free (title);
    }
}



static void
terminal_window_panes_close_clicked (GtkWidget *button G_GNUC_UNUSED,
                                     TerminalPanes *panes)
{
  TerminalScreen *active;
  TerminalWindow *window;

  active = terminal_panes_get_active (panes);
  if (active == NULL)
    return;

  window = TERMINAL_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (panes)));
  if (!TERMINAL_IS_WINDOW (window))
    return;

  /* closing a tab closes the whole pane tree of that tab */
  if (terminal_window_confirm_close (active, window) == CONFIRMED_CLOSE_TAB)
    gtk_widget_destroy (GTK_WIDGET (panes));
}



static GtkWidget *
terminal_window_create_panes_label (TerminalWindow *window,
                                    TerminalPanes *panes)
{
  GtkWidget *hbox, *label, *button, *image;

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 2);

  label = gtk_label_new (NULL);
  gtk_widget_set_margin_start (label, 2);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
  gtk_widget_set_has_tooltip (label, TRUE);

  button = gtk_button_new ();
  gtk_widget_set_focus_on_click (button, FALSE);
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  gtk_widget_set_can_focus (button, FALSE);
  gtk_widget_set_can_default (button, FALSE);
  gtk_widget_set_tooltip_text (button, _("Close this tab"));
  image = gtk_image_new_from_icon_name ("window-close-symbolic", GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (button), image);
  g_signal_connect (G_OBJECT (button), "clicked",
                    G_CALLBACK (terminal_window_panes_close_clicked), panes);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);

  g_object_set_data (G_OBJECT (panes), "panes-tab-label", label);

  /* show the box and all its widgets */
  gtk_widget_show_all (hbox);

  /* respect the show/hide buttons option */
  g_object_bind_property (G_OBJECT (window->priv->preferences), "misc-tab-close-buttons",
                          G_OBJECT (button), "visible",
                          G_BINDING_SYNC_CREATE);

  terminal_window_update_panes_label (window, panes);

  return hbox;
}



static gboolean
terminal_window_notebook_event_in_allocation (gint event_x,
                                              gint event_y,
                                              GtkWidget *widget)
{
  GtkAllocation allocation;

  gtk_widget_get_allocation (widget, &allocation);

  if (event_x >= allocation.x && event_x <= allocation.x + allocation.width
      && event_y >= allocation.y && event_y <= allocation.y + allocation.height)
    {
      return TRUE;
    }

  return FALSE;
}



static gboolean
terminal_window_notebook_button_press_event (GtkNotebook *notebook,
                                             GdkEventButton *event,
                                             TerminalWindow *window)
{
  GtkWidget *page, *label, *menu;
  gint page_num = 0;
  gboolean close_middle_click;
  gint x, y;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);
  g_return_val_if_fail (GTK_IS_NOTEBOOK (notebook), FALSE);

  gdk_window_get_position (event->window, &x, &y);
  x += event->x;
  y += event->y;

  if (event->button == 1)
    {
      if (event->type == GDK_2BUTTON_PRESS)
        {
          /* check if the user double-clicked on the label */
          {
            GtkWidget *active_page;

            active_page = terminal_window_screen_get_page (window, window->priv->active);
            if (active_page != NULL)
              {
                label = gtk_notebook_get_tab_label (notebook, active_page);
                if (terminal_window_notebook_event_in_allocation (x, y, label))
                  {
                    terminal_window_action_set_title (window);
                    return TRUE;
                  }
              }
          }
        }
    }
  else if (event->type == GDK_BUTTON_PRESS && event->button <= 3)
    {
      /* select the page the user clicked on */
      while ((page = gtk_notebook_get_nth_page (notebook, page_num)) != NULL)
        {
          label = gtk_notebook_get_tab_label (notebook, page);
          if (terminal_window_notebook_event_in_allocation (x, y, label))
            break;
          page_num++;
        }

      /* leave if somehow no tab was found */
      if (G_UNLIKELY (page == NULL))
        return FALSE;

      if (event->button == 2)
        {
          /* close the tab on middle click */
          g_object_get (G_OBJECT (window->priv->preferences),
                        "misc-tab-close-middle-click", &close_middle_click, NULL);
          if (close_middle_click)
            terminal_window_close_tab_request (terminal_panes_get_active (TERMINAL_PANES (page)), window);
        }
      else
        {
          /* update the current tab before we show the menu */
          gtk_notebook_set_current_page (notebook, page_num);

          /* show the tab menu */
          menu = gtk_menu_new ();
          terminal_window_update_tabs_menu (window, menu);
          gtk_menu_popup_at_pointer (GTK_MENU (menu), NULL);
        }

      return TRUE;
    }

  return FALSE;
}



static gboolean
terminal_window_notebook_button_release_event (GtkNotebook *notebook,
                                               GdkEventButton *event,
                                               TerminalWindow *window)
{
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);
  g_return_val_if_fail (GTK_IS_NOTEBOOK (notebook), FALSE);

  if (G_LIKELY (window->priv->active != NULL))
    terminal_screen_focus (window->priv->active);

  return FALSE;
}



#if !LIBXFCE4UI_CHECK_VERSION(4, 21, 1)
static gboolean
terminal_window_notebook_scroll_event (GtkNotebook *notebook,
                                       GdkEventScroll *event,
                                       TerminalWindow *window)
{
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);
  g_return_val_if_fail (GTK_IS_NOTEBOOK (notebook), FALSE);

  if ((event->state & gtk_accelerator_get_default_mod_mask ()) != 0)
    return FALSE;

  switch (event->direction)
    {
    case GDK_SCROLL_RIGHT:
    case GDK_SCROLL_DOWN:
      gtk_notebook_next_page (notebook);
      return TRUE;

    case GDK_SCROLL_LEFT:
    case GDK_SCROLL_UP:
      gtk_notebook_prev_page (notebook);
      return TRUE;

    default: /* GDK_SCROLL_SMOOTH */
      switch (gtk_notebook_get_tab_pos (notebook))
        {
        case GTK_POS_LEFT:
        case GTK_POS_RIGHT:
          if (event->delta_y > 0)
            gtk_notebook_next_page (notebook);
          else if (event->delta_y < 0)
            gtk_notebook_prev_page (notebook);
          break;

        default: /* GTK_POS_TOP or GTK_POS_BOTTOM */
          if (event->delta_x > 0)
            gtk_notebook_next_page (notebook);
          else if (event->delta_x < 0)
            gtk_notebook_prev_page (notebook);
          break;
        }
      return TRUE;
    }

  return FALSE;
}
#endif



static void
terminal_window_notebook_drag_data_received (GtkWidget *widget,
                                             GdkDragContext *context,
                                             gint x,
                                             gint y,
                                             GtkSelectionData *selection_data,
                                             guint info,
                                             guint time,
                                             TerminalWindow *window)
{
  GtkWidget *notebook;
  GtkWidget *child;
  gint i, n_pages;
  gboolean succeed = FALSE;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (widget == NULL || TERMINAL_IS_SCREEN (widget) || TERMINAL_IS_PANES (widget));

  /* check */
  if (G_LIKELY (info == TARGET_GTK_NOTEBOOK_TAB))
    {
      /* drag data received on window widget other than a pane tree */
      if (widget == NULL)
        {
          TerminalPanes *panes = terminal_window_get_current_panes (window);

          widget = (panes != NULL) ? GTK_WIDGET (panes) : GTK_WIDGET (window->priv->active);
        }

      /* get the source notebook (other window) */
      notebook = gtk_drag_get_source_widget (context);
      g_return_if_fail (GTK_IS_NOTEBOOK (notebook));

      /* get the dragged page: a pane tree (or a screen in legacy code paths) */
      memcpy (&child, gtk_selection_data_get_data (selection_data), sizeof (child));
      if (!TERMINAL_IS_SCREEN (child) && !TERMINAL_IS_PANES (child))
        goto leave;

      /* leave if we dropped in the same screen and there is only one
       * page in the notebook (window will close before we insert) */
      if (gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook)) < 2 && child == widget)
        goto leave;

      /* figure out where to insert the tab in the notebook */
      n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
      for (i = 0; i < n_pages; i++)
        {
          /* get the child label */
          GtkAllocation allocation;
          GtkWidget *child_page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), i);
          GtkWidget *label = gtk_notebook_get_tab_label (GTK_NOTEBOOK (window->priv->notebook), child_page);
          gtk_widget_get_allocation (label, &allocation);

          /* break if we have a matching drop position */
          if (x < (allocation.x + allocation.width / 2))
            break;
        }

      if (notebook == window->priv->notebook)
        {
          /* if we're in the same notebook, don't risk anything and do a
           * simple reorder */
          gtk_notebook_reorder_child (GTK_NOTEBOOK (notebook), child, i);
        }
      else
        {
          /* take a reference */
          g_object_ref (G_OBJECT (child));
          g_object_ref (G_OBJECT (window));

          /* remove the page from the source notebook */
          gtk_notebook_detach_tab (GTK_NOTEBOOK (notebook), child);

          /* add the pane tree to the new window */
          if (TERMINAL_IS_PANES (child))
            terminal_window_add_panes (window, TERMINAL_PANES (child));
          else
            {
              terminal_window_add (window, TERMINAL_SCREEN (child));
              child = gtk_widget_get_parent (GTK_WIDGET (child));
            }

          /* move the child to the correct position */
          gtk_notebook_reorder_child (GTK_NOTEBOOK (window->priv->notebook), child, i);

          /* release reference */
          g_object_unref (G_OBJECT (child));
          g_object_unref (G_OBJECT (window));
        }

      /* looks like everything worked out */
      succeed = TRUE;
    }

  /* finish the drag */
leave:
  gtk_drag_finish (context, succeed, FALSE, time);
}



static GtkNotebook *
terminal_window_notebook_create_window (GtkNotebook *notebook,
                                        GtkWidget *child,
                                        gint x,
                                        gint y,
                                        TerminalWindow *window)
{
  TerminalPanes *panes;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), NULL);
  g_return_val_if_fail (TERMINAL_IS_PANES (child), NULL);
  g_return_val_if_fail (notebook == GTK_NOTEBOOK (window->priv->notebook), NULL);

  panes = TERMINAL_PANES (child);

  /* a tab holding a split layout can't be moved into its own window yet */
  if (terminal_panes_get_n_screens (panes) > 1)
    return NULL;

  /* only create new window when there are more then 2 tabs (bug #2686) */
  if (gtk_notebook_get_n_pages (notebook) >= 2)
    {
      TerminalScreen *screen = terminal_panes_get_active (panes);

      /* take a reference */
      g_object_ref (G_OBJECT (child));

      /* remove the pane tree from the active window */
      gtk_notebook_detach_tab (notebook, child);

      /* create new window with the active screen */
      g_signal_emit (G_OBJECT (window), window_signals[NEW_WINDOW_WITH_SCREEN], 0, screen, x, y);

      /* release our reference */
      g_object_unref (G_OBJECT (child));
    }

  return NULL;
}



static GtkWidget *
terminal_window_get_context_menu (TerminalScreen *screen,
                                  TerminalWindow *window)
{
  GtkWidget *context_menu;
  GList *children, *lp;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), NULL);

  /* make the clicked pane the active one so the menu items below
   * operate on the right terminal */
  if (TERMINAL_IS_SCREEN (screen))
    {
      TerminalPanes *panes = terminal_window_screen_get_panes (screen);

      if (panes != NULL)
        terminal_panes_set_active (panes, screen);
    }

  context_menu = g_object_new (GTK_TYPE_MENU, NULL);

  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEW_TAB), G_OBJECT (window), GTK_MENU_SHELL (context_menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEW_WINDOW), G_OBJECT (window), GTK_MENU_SHELL (context_menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_OPEN_FOLDER), G_OBJECT (window), GTK_MENU_SHELL (context_menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (context_menu));

  terminal_window_menu_add_section (window, context_menu, MENU_SECTION_COPY | MENU_SECTION_PASTE, FALSE);
  /* in the context menu every slot is named, so the entry without a number is
   * left out: there the numbered ones say the same thing better */
  terminal_window_add_slots_menu (window, context_menu, FALSE);

  xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SHOW_MENUBAR), G_OBJECT (window), gtk_widget_is_visible (window->menubar), GTK_MENU_SHELL (context_menu));
  xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_FULLSCREEN), G_OBJECT (window), window->is_fullscreen, GTK_MENU_SHELL (context_menu));
  xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_READ_ONLY), G_OBJECT (window), !terminal_screen_get_input_enabled (window->priv->active), GTK_MENU_SHELL (context_menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (context_menu));

  terminal_window_menu_add_section (window, context_menu, MENU_SECTION_ZOOM | MENU_SECTION_SIGNAL, TRUE);

  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SAVE_CONTENTS), G_OBJECT (window), GTK_MENU_SHELL (context_menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (context_menu));

  terminal_window_menu_add_section (window, context_menu, MENU_SECTION_SPLIT, FALSE);

  /* Start tmux: a detour from the window menu, and in the terminal context
   * menu that is where it is looked for. */
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_START_TMUX), G_OBJECT (window), GTK_MENU_SHELL (context_menu));

  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (context_menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PREFERENCES), G_OBJECT (window), GTK_MENU_SHELL (context_menu));

  /* hide accel labels */
  children = gtk_container_get_children (GTK_CONTAINER (context_menu));
  for (lp = children; lp != NULL; lp = lp->next)
    xfce_gtk_menu_item_set_accel_label (lp->data, NULL);
  g_list_free (children);

  return context_menu;
}



/* The title of the pane. */
static void
terminal_window_apply_title (TerminalWindow *window,
                             TerminalScreen *screen)
{
  gchar *title;

  if (screen == NULL)
    return;

  title = terminal_screen_get_title (screen);

  gtk_window_set_title (GTK_WINDOW (window), title);

  g_free (title);
}



static void
terminal_window_notify_title (TerminalScreen *screen,
                              GParamSpec *pspec,
                              TerminalWindow *window)
{
  /* update window title */
  if (screen == window->priv->active)
    terminal_window_apply_title (window, screen);
}



static void
terminal_window_action_set_encoding (GtkAction *action,
                                     const gchar *charset,
                                     TerminalWindow *window)
{
  const gchar *new;

  if (G_LIKELY (window->priv->active != NULL))
    {
      /* set the charset */
      terminal_screen_set_encoding (window->priv->active, charset);

      /* update menu */
      new = terminal_screen_get_encoding (window->priv->active);
      terminal_encoding_action_set_charset (action, new);
    }
}



static gchar *
terminal_window_get_working_directory (TerminalWindow *window)
{
  gchar *default_dir;
  gboolean use_default_dir;

  g_object_get (G_OBJECT (window->priv->preferences),
                "use-default-working-dir", &use_default_dir,
                "default-working-dir", &default_dir,
                NULL);

  if (use_default_dir && g_strcmp0 (default_dir, "") != 0)
    return default_dir;

  g_free (default_dir);

  if (G_LIKELY (window->priv->active != NULL))
    return g_strdup (terminal_screen_get_working_directory (window->priv->active));

  return NULL;
}



static gboolean
terminal_window_action_new_tab (TerminalWindow *window)
{
  TerminalScreen *terminal = TERMINAL_SCREEN (g_object_new (TERMINAL_TYPE_SCREEN, NULL));
  gchar *directory = terminal_window_get_working_directory (window);

  if (directory != NULL)
    {
      terminal_screen_set_working_directory (terminal, directory);
      g_free (directory);
    }

  terminal_window_add (window, terminal);
  terminal_screen_launch_child (terminal);

  return TRUE;
}



/* The terminal remembers the split layout of every tab and puts the panes back
 * the way
 * they were. The layout of a tab is a string built by terminal_panes_get_layout():
 * "." is a pane, "H50,.,." a horizontal split, "V30,.,." a vertical one.
 *
 * Every split anchors on a single pane, so replaying a layout is
 * just a sequence of terminal_panes_split_screen_at() calls: first split the
 * pane that is being restored, then grow its left side, then its right side. */
typedef struct
{
  /* where we are in the layout string of the tab */
  const gchar *cursor;

  /* the directories the panes were left in, one per pane of the layout; they
   * are handed out in the same order the panes are created in */
  gchar **cdirs;
  guint n_cdirs;
  guint next_cdir;
} TerminalPanesRestore;


/* the next remembered directory, or NULL when the record carries none left */
static const gchar *
terminal_panes_restore_next_cdir (TerminalPanesRestore *restore)
{
  if (restore == NULL || restore->next_cdir >= restore->n_cdirs)
    return NULL;

  return restore->cdirs[restore->next_cdir++];
}


static void
terminal_window_restore_panes_subtree (TerminalWindow     *window,
                                        TerminalPanes       *panes,
                                        TerminalScreen      *screen,
                                        TerminalPanesRestore *restore)
{
  const gchar *p;
  GtkOrientation orientation;
  gdouble fraction = 0.5;
  gint percent;
  TerminalScreen *new_screen;
  const gchar *cdir;

  if (screen == NULL || restore == NULL || restore->cursor == NULL)
    return;

  p = restore->cursor;

  /* a leaf is a single pane, nothing to restore */
  if (*p == '.')
    {
      restore->cursor = p + 1;
      return;
    }

  if (*p != 'H' && *p != 'V')
    {
      restore->cursor = NULL;
      return;
    }

  orientation = (*p == 'H') ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
  p++;

  if (*p == '(')
    p++;

  percent = 0;
  while (g_ascii_isdigit (*p))
    {
      percent = percent * 10 + (*p - '0');
      p++;
    }

  if (percent > 0)
    fraction = percent / 100.0;

  if (*p == ',')
    p++;

  /* the left side grows inside the pane we already have */
  restore->cursor = p;
  terminal_window_restore_panes_subtree (window, panes, screen, restore);
  if (restore->cursor == NULL)
    return;

  p = restore->cursor;
  if (*p == ',')
    p++;

  /* the right side is a new pane, split off with the stored divider position */
  new_screen = TERMINAL_SCREEN (g_object_new (TERMINAL_TYPE_SCREEN, NULL));
  terminal_window_setup_screen (window, new_screen);

  if (window->priv->font || window->priv->zoom != TERMINAL_ZOOM_LEVEL_DEFAULT)
    terminal_screen_update_font (new_screen);

  terminal_screen_widget_append_accels (new_screen, window->priv->accel_group);

  /* the shell of this pane starts in the directory it was left in */
  cdir = terminal_panes_restore_next_cdir (restore);
  if (cdir != NULL && *cdir != '\0' && g_file_test (cdir, G_FILE_TEST_IS_DIR))
    terminal_screen_set_working_directory (new_screen, cdir);

  terminal_panes_split_screen_at (panes, screen, orientation, new_screen, fraction);
  terminal_screen_launch_child (new_screen);

  /* the right side grows inside the new pane */
  restore->cursor = p;
  terminal_window_restore_panes_subtree (window, panes, new_screen, restore);
  if (restore->cursor == NULL)
    return;

  p = restore->cursor;
  if (*p == ')')
    p++;

  restore->cursor = p;
}



static void
terminal_window_restore_panes_layout (TerminalWindow *window,
                                       TerminalPanes   *panes,
                                       const gchar     *record)
{
  TerminalPanesRestore restore;
  TerminalScreen *screen;
  const gchar *bar;
  gchar *layout;

  if (record == NULL || *record == '\0')
    return;

  /* the record is the layout of the tab followed by the directory of every
   * pane; the layout grammar knows no '|', so the first one ends it */
  bar = strchr (record, '|');
  layout = g_strndup (record, bar != NULL ? (gsize) (bar - record) : strlen (record));

  if (*layout == '\0' || strcmp (layout, ".") == 0)
    {
      g_free (layout);
      return;
    }

  screen = terminal_panes_get_active (panes);
  if (screen == NULL)
    {
      GSList *screens = terminal_panes_get_screens (panes);
      screen = TERMINAL_SCREEN (screens->data);
      g_slist_free (screens);
    }

  if (screen != NULL)
    {
      restore.cdirs = terminal_panes_session_cdirs (record);
      restore.n_cdirs = restore.cdirs != NULL ? g_strv_length (restore.cdirs) : 0;
      /* the first pane got its directory when it was created, its shell has not
       * been started yet at this point */
      restore.next_cdir = 1;
      restore.cursor = layout;

      terminal_window_restore_panes_subtree (window, panes, screen, &restore);

      g_strfreev (restore.cdirs);
    }

  g_free (layout);
}


/* Put the commands back before the shells are started, so that a pane comes
 * up running what it ran instead of a shell that would have to be replaced a
 * moment later. The panes are in the same order the record lists them. */
static void
terminal_window_restore_panes_commands (TerminalPanes *panes,
                                        const gchar   *record)
{
  GPtrArray *commands;
  GSList *screens, *lp;
  guint n = 0;

  if (panes == NULL || record == NULL)
    return;

  commands = terminal_panes_session_commands (record);
  if (commands == NULL)
    return;

  screens = terminal_panes_get_screens (panes);
  for (lp = screens; lp != NULL && n < commands->len; lp = lp->next, n++)
    terminal_screen_set_session_command (TERMINAL_SCREEN (lp->data),
                                         g_ptr_array_index (commands, n));
  g_slist_free (screens);

  terminal_panes_session_commands_free (commands);
}


/* the directory the first pane of a tab was left in, newly allocated or NULL */
static gchar *
terminal_window_saved_pane_directory (const gchar *record,
                                      guint        nth)
{
  gchar **cdirs;
  gchar *directory;

  cdirs = terminal_panes_session_cdirs (record);
  if (cdirs == NULL || cdirs[nth] == NULL)
    {
      g_strfreev (cdirs);
      return NULL;
    }

  directory = g_strdup (cdirs[nth]);
  g_strfreev (cdirs);

  return directory;
}



/* store the layout of every tab, so the panes come back after a restart */
/* whether the split layout of the tabs is remembered and put back */
static gboolean
terminal_window_panes_layouts_enabled (TerminalWindow *window)
{
  gboolean enabled = TRUE;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), TRUE);

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-panes-layouts-enabled", &enabled, NULL);

  return enabled;
}



void
terminal_window_save_panes_layouts (TerminalWindow *window)
{
  GPtrArray *layouts;
  gint npages, i;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  /* the option is off: leave the stored record alone and do not update it, so
   * that nothing can be restored and the layout is still there if the option
   * is switched on again */
  if (!terminal_window_panes_layouts_enabled (window))
    return;

  npages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
  layouts = g_ptr_array_new_with_free_func (g_free);

  for (i = 0; i < npages; i++)
    {
      GtkWidget *child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), i);

      if (TERMINAL_IS_PANES (child))
        g_ptr_array_add (layouts,
                         terminal_panes_get_session_layout (TERMINAL_PANES (child)));
    }

  /* the property is a string vector, so the array has to be NULL terminated:
   * xfconf copies the strings with g_strdupv(), which walks the array until it
   * finds the terminator. Without it xfconf reads past the end of the array
   * (the memory there is arbitrary) and the process crashes. */
  g_ptr_array_add (layouts, NULL);

  g_object_set (G_OBJECT (window->priv->preferences), "misc-panes-layouts", layouts->pdata, NULL);
  g_ptr_array_unref (layouts);
}



/* Beside the split layout the session keeps what the user was looking at: the
 * tab in front, the pane in use inside it and the titles given to the tabs.
 * Every part has its own switch in the preferences, so that nothing is
 * restored that the user does not want back. */
static void
terminal_window_save_session_state (TerminalWindow *window)
{
  gboolean save_tab, save_pane;
  gint current;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-session-active-tab", &save_tab,
                "misc-session-active-pane", &save_pane, NULL);

  current = gtk_notebook_get_current_page (GTK_NOTEBOOK (window->priv->notebook));
  if (save_tab && current >= 0)
    g_object_set (G_OBJECT (window->priv->preferences),
                  "misc-session-active-tab-index", current, NULL);

  if (save_pane && current >= 0)
    {
      GtkWidget *child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), current);
      gint index = 0;

      if (TERMINAL_IS_PANES (child))
        index = g_slist_index (terminal_panes_get_screens (TERMINAL_PANES (child)),
                               terminal_panes_get_active (TERMINAL_PANES (child)));
      g_object_set (G_OBJECT (window->priv->preferences),
                    "misc-session-active-pane-index", index, NULL);
    }

}



/* The restored parts are applied while the tabs appear, the same way the split
 * layouts are. */
static void
terminal_window_restore_session_state (TerminalWindow *window)
{
  gboolean save_tab, save_pane;
  gint index = 0;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-session-active-tab", &save_tab,
                "misc-session-active-pane", &save_pane, NULL);

  if (save_tab)
    {
      g_object_get (G_OBJECT (window->priv->preferences),
                    "misc-session-active-tab-index", &index, NULL);
      if (index >= 0 && index < gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook)))
        gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook), index);
    }

  if (save_pane)
    {
      gint current = gtk_notebook_get_current_page (GTK_NOTEBOOK (window->priv->notebook));
      GtkWidget *child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), MAX (current, 0));

      g_object_get (G_OBJECT (window->priv->preferences),
                    "misc-session-active-pane-index", &index, NULL);
      if (TERMINAL_IS_PANES (child))
        {
          GSList *screens = terminal_panes_get_screens (TERMINAL_PANES (child));

          if (screens != NULL && index >= 0 && index < g_slist_length (screens))
            terminal_panes_set_active (TERMINAL_PANES (child),
                                       TERMINAL_SCREEN (g_slist_nth_data (screens, index)));
        }
    }
}



/* the saved layouts only belong to the tabs that exist at startup */
static gboolean
terminal_window_panes_layout_restore_idle (gpointer user_data)
{
  TerminalWindow *window = TERMINAL_WINDOW (user_data);

  window->priv->panes_layout_index = -1;
  window->priv->panes_layout_idle = 0;
  terminal_window_restore_session_state (window);

  return FALSE;
}



static gboolean
terminal_window_action_split (TerminalWindow *window,
                              GtkOrientation orientation)
{
  TerminalPanes *panes;
  TerminalScreen *active, *new_screen;
  gchar *directory;
  glong w, h;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  panes = terminal_window_get_current_panes (window);
  if (panes == NULL)
    return TRUE;

  active = terminal_panes_get_active (panes);
  if (active == NULL)
    return TRUE;

  /* create the new terminal pane */
  new_screen = TERMINAL_SCREEN (g_object_new (TERMINAL_TYPE_SCREEN, NULL));

  directory = g_strdup (terminal_screen_get_working_directory (active));
  if (directory != NULL)
    {
      terminal_screen_set_working_directory (new_screen, directory);
      g_free (directory);
    }

  terminal_window_setup_screen (window, new_screen);

  /* match zoom and font */
  if (window->priv->font || window->priv->zoom != TERMINAL_ZOOM_LEVEL_DEFAULT)
    terminal_screen_update_font (new_screen);

  /* match the size of the active screen */
  terminal_screen_get_size (active, &w, &h);
  terminal_screen_set_size (new_screen, w, h);

  /* update scrollbar visibility */
  if (window->priv->scrollbar_visibility != TERMINAL_VISIBILITY_DEFAULT)
    terminal_screen_update_scrolling_bar (new_screen);

  /* split the pane tree; this makes the new screen active */
  terminal_panes_split_screen (panes, active, orientation, new_screen);

  /* connect the accels */
  terminal_screen_widget_append_accels (new_screen, window->priv->accel_group);

  /* start the shell in the new pane */
  terminal_screen_launch_child (new_screen);

  terminal_window_save_panes_layouts (window);

  return TRUE;
}



static gboolean
terminal_window_action_split_horizontal (TerminalWindow *window)
{
  /* "Horizontal" names the line that appears, not the way the panes end up
   * lying: a horizontal split puts one pane above the other, which is what
   * GtkPaned calls vertical. The way round it was before read as "panes side
   * by side", which is what people mean by a vertical split. */
  return terminal_window_action_split (window, GTK_ORIENTATION_VERTICAL);
}



static gboolean
terminal_window_action_split_vertical (TerminalWindow *window)
{
  return terminal_window_action_split (window, GTK_ORIENTATION_HORIZONTAL);
}



guint
terminal_window_get_n_tabs (TerminalWindow *window)
{
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), 0);

  return gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
}



static void
terminal_window_add_fresh_tab (TerminalWindow *window)
{
  TerminalScreen *fresh;
  glong columns, rows;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  if (terminal_window_get_n_tabs (window) > 0)
    return;

  columns = window->priv->grid_width;
  rows = window->priv->grid_height;
  if (columns <= 0 || rows <= 0)
    {
      /* priv->grid_* is not set for drop-down windows (see
       * terminal_app_process()); the upstream defaults are used so that VTE
       * starts the shell.  terminal_window_add_panes() sizes the tab from the
       * monitor geometry right afterwards, exactly like the first tab. */
      columns = 80;
      rows = 24;
    }

  fresh = terminal_screen_new (terminal_tab_attr_new (), columns, rows);
  terminal_window_add (window, fresh);

  /* the shell is only started explicitly, see terminal_window_action_new_tab() */
  terminal_screen_launch_child (fresh);
}



static gboolean
terminal_window_add_fresh_tab_idle (gpointer user_data)
{
  TerminalWindow *window = TERMINAL_WINDOW (user_data);

  /* the window may have gone away in the meantime */
  if (window->priv->notebook != NULL
      && !gtk_widget_in_destruction (GTK_WIDGET (window)))
    {
      terminal_window_add_fresh_tab (window);

      /* When the shell of the last tab ends, the panel
       * slides away instead of staying on screen.  The process and the status
       * icon stay alive, and the next toggle shows the fresh tab. */
      gtk_widget_hide (GTK_WIDGET (window));
    }

  return G_SOURCE_REMOVE;
}



/* The line that puts a pane into tmux, built out of the settings. An empty
 * command field in the preferences means "build it", a line there is the
 * user's own and is used as it is. */
static gchar *
terminal_window_build_tmux_command (void)
{
  TerminalPreferences *preferences = terminal_preferences_get ();
  gchar *command = NULL;
  gchar *line;

  g_object_get (G_OBJECT (preferences), "misc-tmux-command", &command, NULL);
  line = terminal_tmux_build_command (command);
  g_free (command);

  return line;
}


static gboolean
terminal_window_action_start_tmux (TerminalWindow *window)
{
  TerminalScreen *active;
  gchar *cmd;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  active = window->priv->active;
  if (active == NULL)
    return TRUE;

  cmd = terminal_window_build_tmux_command ();
  terminal_screen_feed_text (active, cmd);
  terminal_screen_feed_text (active, "\n");
  g_free (cmd);

  return TRUE;
}



static gboolean
terminal_window_action_close_pane (TerminalWindow *window)
{
  TerminalPanes *panes;
  TerminalScreen *active;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  panes = terminal_window_get_current_panes (window);
  if (panes == NULL)
    return TRUE;

  /* do nothing when there is only one pane in the tab */
  if (terminal_panes_get_n_screens (panes) < 2)
    return TRUE;

  active = terminal_panes_get_active (panes);
  if (active == NULL)
    return TRUE;

  if (terminal_panes_remove_screen (panes, active))
    {
      TerminalScreen *new_active = terminal_panes_get_active (panes);

      if (new_active != NULL)
        {
          terminal_window_set_active_screen (window, panes, new_active);
          terminal_screen_focus (new_active);
        }

      terminal_window_save_panes_layouts (window);
    }

  return TRUE;
}



static gboolean
terminal_window_action_focus_direction (TerminalWindow *window,
                                        TerminalPanesDirection direction)
{
  TerminalPanes *panes;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  panes = terminal_window_get_current_panes (window);
  if (panes != NULL)
    terminal_panes_focus_direction (panes, direction);

  return TRUE;
}



static gboolean
terminal_window_action_focus_up (TerminalWindow *window)
{
  return terminal_window_action_focus_direction (window, TERMINAL_PANES_DIRECTION_UP);
}



static gboolean
terminal_window_action_focus_down (TerminalWindow *window)
{
  return terminal_window_action_focus_direction (window, TERMINAL_PANES_DIRECTION_DOWN);
}



static gboolean
terminal_window_action_focus_left (TerminalWindow *window)
{
  return terminal_window_action_focus_direction (window, TERMINAL_PANES_DIRECTION_LEFT);
}



static gboolean
terminal_window_action_focus_right (TerminalWindow *window)
{
  return terminal_window_action_focus_direction (window, TERMINAL_PANES_DIRECTION_RIGHT);
}



static gboolean
terminal_window_action_focus_next (TerminalWindow *window)
{
  TerminalPanes *panes;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  panes = terminal_window_get_current_panes (window);
  if (panes != NULL)
    terminal_panes_focus_next (panes);

  return TRUE;
}


static gboolean
terminal_window_action_focus_prev (TerminalWindow *window)
{
  TerminalPanes *panes;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  panes = terminal_window_get_current_panes (window);
  if (panes != NULL)
    terminal_panes_focus_previous (panes);

  return TRUE;
}



static gboolean
terminal_window_action_move_pane (TerminalWindow        *window,
                                  TerminalPanesDirection  direction)
{
  TerminalPanes *panes;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  panes = terminal_window_get_current_panes (window);
  if (panes != NULL)
    terminal_panes_move_pane (panes, terminal_panes_get_active (panes), direction);

  return TRUE;
}



static gboolean
terminal_window_action_move_pane_up (TerminalWindow *window)
{
  return terminal_window_action_move_pane (window, TERMINAL_PANES_DIRECTION_UP);
}



static gboolean
terminal_window_action_move_pane_down (TerminalWindow *window)
{
  return terminal_window_action_move_pane (window, TERMINAL_PANES_DIRECTION_DOWN);
}



static gboolean
terminal_window_action_move_pane_left (TerminalWindow *window)
{
  return terminal_window_action_move_pane (window, TERMINAL_PANES_DIRECTION_LEFT);
}



static gboolean
terminal_window_action_move_pane_right (TerminalWindow *window)
{
  return terminal_window_action_move_pane (window, TERMINAL_PANES_DIRECTION_RIGHT);
}



static gboolean
terminal_window_action_new_window (TerminalWindow *window)
{
  gchar *directory = terminal_window_get_working_directory (window);

  if (directory != NULL)
    {
      g_signal_emit (G_OBJECT (window), window_signals[NEW_WINDOW], 0, directory);
      g_free (directory);
    }

  return TRUE;
}



static gboolean
terminal_window_action_open_folder (TerminalWindow *window)
{
  GError *error = NULL;
  const gchar *directory = terminal_screen_get_working_directory (window->priv->active);

  if (directory != NULL)
    {
      gchar *dir = g_shell_quote (directory);
      gchar *cmd = g_strdup_printf ("xdg-open %s", dir);

      if (!g_spawn_command_line_async (cmd, &error))
        {
          xfce_dialog_show_error (GTK_WINDOW (window), error,
                                  _("Failed to open directory"));
          g_error_free (error);
        }

      g_free (cmd);
      g_free (dir);
    }

  return TRUE;
}



static gboolean
terminal_window_action_undo_close_tab (TerminalWindow *window)
{
  TerminalScreen *terminal;
  TerminalTabAttr *tab_attr;
  GtkWidget *page;

  if (G_UNLIKELY (g_queue_is_empty (window->priv->closed_tabs_list)))
    return TRUE;

  /* get info on the last closed tab and remove it from the list */
  tab_attr = g_queue_pop_tail (window->priv->closed_tabs_list);

  terminal = terminal_screen_new (tab_attr, window->priv->grid_width, window->priv->grid_height);
  terminal_window_add (window, terminal);
  page = terminal_window_screen_get_page (window, terminal);

  /* restore tab title color */
  if (tab_attr->color_title != NULL)
    terminal_screen_set_custom_title_color (terminal, tab_attr->color_title);

  /* restore tab position */
  if (page != NULL)
    gtk_notebook_reorder_child (GTK_NOTEBOOK (window->priv->notebook), page, tab_attr->position);

  /* restore tab focus if the unclosed one wasn't active when it was closed */
  if (!tab_attr->active)
    {
      page = terminal_window_screen_get_page (window, window->priv->active);
      if (page != NULL)
        {
          gint page_num = gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), page);
          gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook), page_num);
        }
    }

  /* free info */
  terminal_tab_attr_free (tab_attr);

  terminal_screen_launch_child (terminal);

  return TRUE;
}



static gboolean
terminal_window_action_detach_tab (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    {
      GtkWidget *page;

      page = terminal_window_screen_get_page (window, window->priv->active);
      if (page != NULL)
        terminal_window_notebook_create_window (GTK_NOTEBOOK (window->priv->notebook),
                                                page,
                                                -1, -1, window);
    }
  return TRUE;
}



static gboolean
terminal_window_action_close_tab (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    terminal_window_close_tab_request (window->priv->active, window);
  return TRUE;
}



/* Ghostty closes the tabs on the right of the current one; the tab that was in
 * focus stays open, so that the work is not thrown away with the rest. */
static gboolean
terminal_window_action_close_tabs_right (TerminalWindow *window)
{
  GtkNotebook *notebook = GTK_NOTEBOOK (window->priv->notebook);
  GtkWidget *page;
  gint current, n;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  page = terminal_window_screen_get_page (window, window->priv->active);
  if (page == NULL)
    return TRUE;

  current = gtk_notebook_page_num (notebook, page);
  if (current < 0)
    return TRUE;

  for (n = gtk_notebook_get_n_pages (notebook) - 1; n > current; n--)
    {
      TerminalPanes *panes = TERMINAL_PANES (gtk_notebook_get_nth_page (notebook, n));

      terminal_window_close_tab_request (terminal_panes_get_active (panes), window);
    }

  return TRUE;
}



/* Ghostty closes every tab, which leaves the window with nothing: it goes away
 * when it was the last window, as it does for "Close Window". */
static gboolean
terminal_window_action_close_all_tabs (TerminalWindow *window)
{
  GtkNotebook *notebook = GTK_NOTEBOOK (window->priv->notebook);
  gint n;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  for (n = gtk_notebook_get_n_pages (notebook) - 1; n >= 0; n--)
    {
      TerminalPanes *panes = TERMINAL_PANES (gtk_notebook_get_nth_page (notebook, n));

      terminal_window_close_tab_request (terminal_panes_get_active (panes), window);
    }

  return TRUE;
}



static gboolean
terminal_window_action_close_other_tabs (TerminalWindow *window)
{
  GtkNotebook *notebook = GTK_NOTEBOOK (window->priv->notebook);
  GtkWidget *page;
  gint npages, n;

  /* move current page to the beginning */
  page = terminal_window_screen_get_page (window, window->priv->active);
  if (page == NULL)
    return TRUE;
  gtk_notebook_reorder_child (notebook, page, 0);

  /* remove the others */
  npages = gtk_notebook_get_n_pages (notebook);
  for (n = npages - 1; n > 0; n--)
    {
      TerminalPanes *panes = TERMINAL_PANES (gtk_notebook_get_nth_page (notebook, n));

      terminal_window_close_tab_request (terminal_panes_get_active (panes), window);
    }

  return TRUE;
}



static gboolean
terminal_window_action_close_window (TerminalWindow *window)
{
  /* this will invoke the "delete-event" handler */
  gtk_window_close (GTK_WINDOW (window));
  return TRUE;
}



static gboolean
terminal_window_action_copy (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    terminal_screen_copy_clipboard (window->priv->active);
  return TRUE;
}



static gboolean
terminal_window_action_copy_html (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    terminal_screen_copy_clipboard_html (window->priv->active);
  return TRUE;
}



/* Vte offers no way to read the selection apart from putting it into the
 * clipboard, so the links of the selection are taken from there: the selection
 * is copied as text and read back. */
static gchar *
terminal_window_extract_links (const gchar *text)
{
  static GRegex *link_regex;
  GMatchInfo *match;
  GString *links;

  if (link_regex == NULL)
    link_regex = g_regex_new ("(https?|ftps?|file|ssh|sftp|git)://[^[:space:]\"'<>]+",
                              0, 0, NULL);

  links = g_string_new (NULL);
  g_regex_match (link_regex, text != NULL ? text : "", 0, &match);
  while (g_match_info_matches (match))
    {
      gchar *start = g_match_info_fetch (match, 0);
      gchar *end = g_match_info_fetch (match, 1);
      gsize length;

      length = (gsize) (end - start);
      /* a link at the end of a line is often followed by a dot or a bracket
       * that belongs to the sentence and not to the link */
      while (length > 0 && (strchr (".),", start[length - 1]) != NULL))
        length--;
      if (length > 0)
        {
          g_string_append_len (links, start, (gssize) length);
          g_string_append_c (links, '\n');
        }

      g_free (start);
      g_free (end);
      g_match_info_next (match, NULL);
    }
  g_match_info_free (match);

  if (links->len == 0)
    {
      g_string_free (links, TRUE);
      return NULL;
    }

  return g_string_free (links, FALSE);
}



static void
terminal_window_copy_links (TerminalWindow *window)
{
  GtkClipboard *clipboard;
  gchar *links;
  gchar *text;

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  terminal_screen_copy_clipboard (window->priv->active);
  text = gtk_clipboard_wait_for_text (clipboard);
  links = terminal_window_extract_links (text);
  g_free (text);

  if (links != NULL)
    {
      gtk_clipboard_set_text (clipboard, links, (gint) strlen (links));
      g_free (links);
    }
  else
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                                       "%s", _("No links in the selection"));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
        _("There is nothing that looks like a web address in the selected text, "
          "so it was copied as it is."));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
}



/* One dialog instead of a separate menu entry for every form of the clipboard:
 * the plain text and the HTML are in the menu as they always were, and the
 * links are the third thing this dialog offers. */
static gboolean
terminal_window_action_copy_as (TerminalWindow *window)
{
  GtkWidget *dialog, *content, *box, *combo, *label;
  gint response;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  if (!terminal_screen_has_selection (window->priv->active))
    return TRUE;

  dialog = gtk_dialog_new_with_buttons (_("Copy As"), GTK_WINDOW (window),
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        _("_Cancel"), GTK_RESPONSE_CANCEL,
                                        _("_Copy"), GTK_RESPONSE_ACCEPT,
                                        NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);
  gtk_container_add (GTK_CONTAINER (content), box);

  label = gtk_label_new_with_mnemonic (_("Put the _selection into the clipboard as:"));
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Plain text"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("HTML"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Links only"));
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_box_pack_start (GTK_BOX (box), combo, FALSE, FALSE, 0);

  gtk_widget_show_all (dialog);
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  if (response != GTK_RESPONSE_ACCEPT)
    return TRUE;

  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)))
    {
    case 1:
      terminal_screen_copy_clipboard_html (window->priv->active);
      break;

    case 2:
      terminal_window_copy_links (window);
      break;

    default:
      terminal_screen_copy_clipboard (window->priv->active);
      break;
    }

  return TRUE;
}





static gboolean
terminal_window_action_goto_bookmark (GtkMenuItem *item,
                                      TerminalWindow *window)
{
  const gchar *path;
  gchar *expanded;
  gchar *quoted;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  path = g_object_get_data (G_OBJECT (item), "bookmark-path");
  if (path == NULL || *path == '\0' || window->priv->active == NULL)
    return TRUE;

  expanded = path;
  if (expanded[0] == '~' && (expanded[1] == '/' || expanded[1] == '\0'))
    expanded = g_strdup_printf ("%s%s", g_get_home_dir (), expanded + 1);

  quoted = g_shell_quote (expanded);
  terminal_screen_feed_text (TERMINAL_SCREEN (window->priv->active), "cd ");
  terminal_screen_feed_text (TERMINAL_SCREEN (window->priv->active), quoted);
  terminal_screen_feed_text (TERMINAL_SCREEN (window->priv->active), "\n");
  g_free (quoted);
  if (expanded != path)
    g_free (expanded);

  return TRUE;
}


/* What a slot holds, in one line: the menu item says what it will paste, the
 * same way the clipboard manager does. Long text is cut, newlines become
 * spaces: a menu with a hundred rows in it helps nobody. */
static gchar *
terminal_window_slot_preview (const gchar *text)
{
  const gsize limit = 48;
  gchar *first_line;
  gchar *cut;
  gchar *result;

  if (text == NULL || *text == '\0')
    return NULL;

  first_line = g_strdup (text);
  for (cut = first_line; *cut != '\0'; cut++)
    if (*cut == '\n' || *cut == '\r' || *cut == '\t')
      *cut = ' ';

  if (g_utf8_strlen (first_line, -1) > limit)
    {
      gchar *end = g_utf8_offset_to_pointer (first_line, limit);
      *end = '\0';
      g_strlcat (first_line, "\xe2\x80\xa6", limit + 4); /* … */
    }

  result = g_strdup (first_line);
  g_free (first_line);

  return result;
}


/* The label of a slot in a menu, with what it holds: "Paste from Clipboard
 * Slot 2: ls -l". A slot with nothing in it says only its number. */
static gchar *
terminal_window_slot_label (gboolean paste,
                            guint index,
                            const gchar *contents)
{
  gchar *preview;
  gchar *label;

  preview = terminal_window_slot_preview (contents);
  if (preview != NULL)
    label = paste
              ? g_strdup_printf (_("Paste from Clipboard Slot %u: %s"), index, preview)
              : g_strdup_printf (_("Copy to Clipboard Slot %u: %s"), index, preview);
  else
    label = paste
              ? g_strdup_printf (_("Paste from Clipboard Slot %u"), index)
              : g_strdup_printf (_("Copy to Clipboard Slot %u"), index);
  g_free (preview);

  return label;
}


/* The entries to copy the selection into the slots: one per slot, the number
 * it goes to is in the label. */
static void
terminal_window_add_slot_copy_items (TerminalWindow *window,
                                     GtkWidget *menu)
{
  guint n;
  guint i;
  gboolean has_selection;

  n = terminal_window_clipboard_slots ();
  has_selection = window->priv->active != NULL
                  && terminal_screen_has_selection (window->priv->active);

  for (i = 0; i < n; i++)
    {
      gchar *label;
      GtkWidget *item;

      label = terminal_window_slot_label (FALSE, i + 1, NULL);
      item = gtk_menu_item_new_with_mnemonic (label);
      g_free (label);
      g_object_set_data (G_OBJECT (item), "clipboard-slot", GINT_TO_POINTER ((gint) i));
      g_signal_connect (G_OBJECT (item), "activate",
                        G_CALLBACK (terminal_window_action_slot_item), window);
      gtk_widget_set_sensitive (item, has_selection);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }
}


/* The entries to paste out of the slots: one per slot, with what the slot
 * holds in the label, so that the wanted one can be told from the others. */
static void
terminal_window_add_slot_paste_items (TerminalWindow *window,
                                      GtkWidget *menu)
{
  guint n;
  guint i;

  n = terminal_window_clipboard_slots ();

  for (i = 0; i < n; i++)
    {
      gchar *label;
      GtkWidget *item;
      gboolean holds_text;

      holds_text = clipboard_slots[i] != NULL;

      label = terminal_window_slot_label (TRUE, i + 1, clipboard_slots[i]);
      item = gtk_menu_item_new_with_mnemonic (label);
      g_free (label);
      g_object_set_data (G_OBJECT (item), "clipboard-slot", GINT_TO_POINTER ((gint) i));
      g_object_set_data (G_OBJECT (item), "clipboard-slot-paste", GINT_TO_POINTER (1));
      g_signal_connect (G_OBJECT (item), "activate",
                        G_CALLBACK (terminal_window_action_slot_item), window);
      gtk_widget_set_sensitive (item, holds_text
                                        && window->priv->active != NULL
                                        && terminal_screen_get_input_enabled (window->priv->active));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }
}


/* The numbered slots for any menu that wants them: the context menu
 * of the terminal and the "Edit" menu are both built from here, so the two
 * cannot drift apart. One entry per slot by default; the nesting setting puts
 * them under a submenu, the way they were before. */
static void
terminal_window_add_slots_menu (TerminalWindow *window,
                                GtkWidget *menu,
                                gboolean with_hotkey)
{
  GtkWidget *item;
  gboolean has_selection;

  /* off means off: no separator, no entry, as if there were none */
  if (!terminal_window_clipboard_slots_enabled ())
    return;

  has_selection = window->priv->active != NULL
                  && terminal_screen_has_selection (window->priv->active);

  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));

  /* the hotkey that walks the slots one press at a time: it is in the menu
   * with its accelerator. In the context menu it is left out: there the slots
   * are named one by one, and a second way of saying the same thing above
   * them is only noise. */
  if (with_hotkey)
    {
      const XfceGtkActionEntry *entry = get_action_entry (TERMINAL_WINDOW_ACTION_COPY_TO_SLOT);

      item = xfce_gtk_menu_item_new_from_action_entry (entry, G_OBJECT (window),
                                                       GTK_MENU_SHELL (menu));
      gtk_widget_set_sensitive (item, has_selection);
    }

  if (terminal_window_clipboard_slots_nested ())
    {
      /* the slots sit under a submenu of their own, one per direction: with
       * many slots an entry per slot makes the menu long */
      GtkWidget *copy_menu = gtk_menu_new ();
      GtkWidget *paste_menu = gtk_menu_new ();

      terminal_window_add_slot_copy_items (window, copy_menu);
      terminal_window_add_slot_paste_items (window, paste_menu);

      item = gtk_menu_item_new_with_mnemonic (_("Copy to Clipboard _Slot"));
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), copy_menu);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

      item = gtk_menu_item_new_with_mnemonic (_("Paste from Clipboard _Slot"));
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), paste_menu);
      gtk_widget_set_sensitive (item, window->priv->active != NULL
                                        && terminal_screen_get_input_enabled (window->priv->active));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }
  else
    {
      terminal_window_add_slot_copy_items (window, menu);
      terminal_window_add_slot_paste_items (window, menu);
    }

  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
}


/* Copying into a slot is a copy through the system clipboard: Vte offers no
 * other way to read the selection. The form the slot keeps is one setting for
 * all of them: a slot holds one string, so a text and an HTML form of the
 * same selection would fight over the same cell. */
static gchar *
terminal_window_slot_clipboard_html (GtkClipboard *clipboard)
{
  GtkSelectionData *selection_data;
  const guchar *bytes;
  gint length;
  gchar *html = NULL;

  /* Vte puts the HTML form under a target of its own, which is not the text
   * that gtk_clipboard_wait_for_text () reads back */
  selection_data = gtk_clipboard_wait_for_contents (clipboard,
                                                    gdk_atom_intern_static_string ("text/html"));
  if (selection_data == NULL)
    return NULL;

  bytes = gtk_selection_data_get_data_with_length (selection_data, &length);
  if (bytes != NULL && length > 0)
    {
      /* the bytes are not terminated, so one more is copied in */
      html = g_malloc ((gsize) length + 1);
      memcpy (html, bytes, (gsize) length);
      html[length] = '\0';
    }

  gtk_selection_data_free (selection_data);

  return html;
}



/* Returns %TRUE when the slot was written. A selection that has nothing in the
 * form the setting asks for leaves the slot as it was: a slot is not a place
 * to lose a string to a failed copy. */
static gboolean
terminal_window_slot_copy (TerminalWindow *window,
                           guint           slot)
{
  GtkClipboard *clipboard;
  TerminalPreferences *preferences;
  gchar *format = NULL;
  gchar *text;

  if (slot >= terminal_window_clipboard_slots () || !terminal_screen_has_selection (window->priv->active))
    return FALSE;

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  preferences = terminal_preferences_get ();
  g_object_get (G_OBJECT (preferences), "misc-clipboard-slots-format", &format, NULL);
  g_object_unref (preferences);

  if (g_strcmp0 (format, "html") == 0)
    {
      terminal_screen_copy_clipboard_html (window->priv->active);
      text = terminal_window_slot_clipboard_html (clipboard);

      /* a selection Vte refuses to turn into HTML is worth a slot as text
       * rather than no slot at all */
      if (text == NULL)
        {
          terminal_screen_copy_clipboard (window->priv->active);
          text = gtk_clipboard_wait_for_text (clipboard);
        }
    }
  else
    {
      terminal_screen_copy_clipboard (window->priv->active);
      text = gtk_clipboard_wait_for_text (clipboard);

      if (g_strcmp0 (format, "links") == 0)
        {
          /* a selection without a link gives NULL and the slot stays as it
           * was */
          gchar *links = terminal_window_extract_links (text);
          g_free (text);
          text = links;
        }
    }

  g_free (format);

  if (text == NULL || *text == '\0')
    {
      g_free (text);
      return FALSE;
    }

  g_free (clipboard_slots[slot]);
  clipboard_slots[slot] = text;

  return TRUE;
}



static gboolean
terminal_window_action_copy_to_slot (TerminalWindow *window)
{
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  if (!terminal_window_clipboard_slots_enabled ())
    return TRUE;

  /* the number moves on only for a copy that happened: otherwise the next
   * press would land in a slot the user never saw filled */
  if (terminal_window_slot_copy (window, clipboard_slot))
    {
      clipboard_slot_last = clipboard_slot;
      clipboard_slot = (clipboard_slot + 1) % terminal_window_clipboard_slots ();
    }

  return TRUE;
}


/* The same two things from the context menu, where the slot is picked by name:
 * "Copy to Clipboard Slot 2" and "Paste from Clipboard Slot 2" say what they
 * will do, which is what the cycling hotkeys cannot do. */
static gboolean
terminal_window_action_slot_item (GtkMenuItem *item,
                                  TerminalWindow *window)
{
  gint slot;
  gboolean is_paste;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  if (!terminal_window_clipboard_slots_enabled ())
    return TRUE;

  slot = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "clipboard-slot"));
  if (slot < 0 || (guint) slot >= terminal_window_clipboard_slots ())
    return TRUE;

  is_paste = g_object_get_data (G_OBJECT (item), "clipboard-slot-paste") != NULL;

  if (is_paste)
    {
      if (clipboard_slots[slot] != NULL)
        {
          /* the slot the user picked is the one the paste accelerator takes
           * from: the string that was just pasted is the one to have again */
          clipboard_slot_last = (guint) slot;
          terminal_window_slot_paste (window, clipboard_slots[slot]);
        }
    }
  else
    {
      /* the copy goes where it was asked to go, and the paste accelerator
       * takes from there from now on */
      if (terminal_window_slot_copy (window, (guint) slot))
        clipboard_slot_last = (guint) slot;
    }

  return TRUE;
}



/* Pasting from a slot puts the string into the system clipboard and pastes it
 * from there, which is the same path as an ordinary paste. */
static void
terminal_window_slot_paste (TerminalWindow *window,
                            const gchar *text)
{
  if (window->priv->active == NULL)
    return;

  gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
                          text, (gint) strlen (text));
  terminal_screen_paste_clipboard (window->priv->active);
}


/* The paste accelerator takes from the slot that was used last: the one the
 * selection was copied to, or the one that was pasted out of. */
static gboolean
terminal_window_action_paste_from_slot (TerminalWindow *window)
{
  const gchar *text;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  if (!terminal_window_clipboard_slots_enabled ())
    return TRUE;

  text = clipboard_slot_last < terminal_window_clipboard_slots ()
           ? clipboard_slots[clipboard_slot_last]
           : NULL;
  if (text == NULL)
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                                       "%s", _("The clipboard slot is empty"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      return TRUE;
    }

  terminal_window_slot_paste (window, text);

  return TRUE;
}



static gboolean
terminal_window_action_paste (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    terminal_screen_paste_clipboard (window->priv->active);
  return TRUE;
}



static gboolean
terminal_window_action_paste_selection (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    terminal_screen_paste_primary (window->priv->active);
  return TRUE;
}



static gboolean
terminal_window_action_select_all (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    terminal_screen_select_all (window->priv->active);
  return TRUE;
}



static void
copy_input_popover_close (GtkWidget *popover,
                          TerminalWindow *window)
{
  /* need for hiding on focus */
  if (window->priv->drop_down)
    terminal_util_activate_window (GTK_WINDOW (window));

  /* close the dialog */
  gtk_widget_destroy (popover);

  /* focus the terminal */
  if (TERMINAL_IS_SCREEN (window->priv->active))
    terminal_screen_focus (window->priv->active);
}



static void
copy_input_popover_do_copy (GtkWidget *popover,
                            GtkWidget *entry)
{
  TerminalWindow *window = TERMINAL_WINDOW (gtk_widget_get_toplevel (popover));
  GtkNotebook *notebook = GTK_NOTEBOOK (window->priv->notebook);
  gint n, npages = gtk_notebook_get_n_pages (notebook);

  /* copy the input to all tabs, including every pane of each tab */
  for (n = 0; n < npages; n++)
    {
      TerminalPanes *panes;
      GSList *screens, *lp;

      panes = TERMINAL_PANES (gtk_notebook_get_nth_page (notebook, n));
      screens = terminal_panes_get_screens (panes);

      for (lp = screens; lp != NULL; lp = lp->next)
        terminal_screen_feed_text (TERMINAL_SCREEN (lp->data), gtk_entry_get_text (GTK_ENTRY (entry)));

      g_slist_free (screens);
    }
}



static gboolean
terminal_window_action_copy_input (TerminalWindow *window)
{
  GtkWidget *popover, *button, *box, *label, *entry;

  popover = gtk_popover_new (GTK_WIDGET (window->menubar));
  gtk_popover_set_position (GTK_POPOVER (popover), GTK_POS_BOTTOM);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (box), 6);
  gtk_container_add (GTK_CONTAINER (popover), box);

  label = gtk_label_new_with_mnemonic (_("Copy _Input:"));
  gtk_container_add (GTK_CONTAINER (box), label);

  entry = gtk_entry_new ();
  gtk_widget_set_tooltip_text (entry, _("Enter the text to be copied to all tabs"));
  gtk_container_add (GTK_CONTAINER (box), entry);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  g_signal_connect (G_OBJECT (entry), "activate", G_CALLBACK (copy_input_popover_do_copy), entry);

  button = gtk_button_new_from_icon_name ("edit-copy-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text (button, _("Copy input"));
  gtk_container_add (GTK_CONTAINER (box), button);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (copy_input_popover_do_copy), entry);

  g_signal_connect (G_OBJECT (popover), "closed", G_CALLBACK (copy_input_popover_close), window);

  gtk_widget_show_all (popover);

  return TRUE;
}



static gboolean
terminal_window_action_prefs (TerminalWindow *window)
{
  if (window->priv->preferences_dialog == NULL)
    {
      /* the drop-down page is always shown: it holds the setting that makes a
       * window a drop-down in the first place, which has to be reachable from
       * a normal window too */
      window->priv->preferences_dialog = terminal_preferences_dialog_new (TRUE, window->priv->drop_down);
      if (G_LIKELY (window->priv->preferences_dialog != NULL))
        {
          window->priv->n_child_windows++;
          g_object_weak_ref (G_OBJECT (window->priv->preferences_dialog),
                             terminal_window_action_prefs_died, window);
        }
    }

  if (window->priv->preferences_dialog != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (window->priv->preferences_dialog), GTK_WINDOW (window));
      gtk_window_present (GTK_WINDOW (window->priv->preferences_dialog));
    }

  return TRUE;
}



static void
hide_menubar (GtkMenuShell *menubar)
{
  g_signal_handlers_disconnect_by_func (menubar, hide_menubar, NULL);
  gtk_widget_hide (GTK_WIDGET (menubar));
}



static gboolean
terminal_window_action_toggle_menubar (TerminalWindow *window)
{
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  if (!gtk_widget_get_visible (window->menubar))
    {
      gboolean shortcuts_no_menukey;
      g_object_get (G_OBJECT (window->priv->preferences),
                    "shortcuts-no-menukey", &shortcuts_no_menukey,
                    NULL);
      if (!shortcuts_no_menukey)
        {
          GList *items = gtk_container_get_children (GTK_CONTAINER (window->menubar));
          gtk_widget_show (window->menubar);
          gtk_menu_shell_select_first (GTK_MENU_SHELL (window->menubar), TRUE);
          g_signal_connect (window->menubar, "deactivate", G_CALLBACK (hide_menubar), NULL);
          g_list_free (items);
          return TRUE;
        }
    }

  return FALSE;
}



static gboolean
terminal_window_action_show_menubar (TerminalWindow *window)
{
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  terminal_window_size_push (window);

  if (!gtk_widget_is_visible (window->menubar))
    gtk_widget_show (window->menubar);
  else
    gtk_widget_hide (window->menubar);

  terminal_window_size_pop (window);

  return TRUE;
}



static gboolean
terminal_window_action_toggle_toolbar (TerminalWindow *window)
{
  terminal_window_size_push (window);

  if (!gtk_widget_is_visible (window->toolbar))
    gtk_widget_show (window->toolbar);
  else
    gtk_widget_hide (window->toolbar);

  terminal_window_size_pop (window);
  return TRUE;
}



static gboolean
terminal_window_action_toggle_borders (TerminalWindow *window)
{
  gtk_window_set_decorated (GTK_WINDOW (window), !gtk_window_get_decorated (GTK_WINDOW (window)));
  return TRUE;
}



static gboolean
terminal_window_action_fullscreen (TerminalWindow *window)
{
  if (gtk_widget_get_visible (GTK_WIDGET (window)))
    {
      window->is_fullscreen = !window->is_fullscreen;
      if (window->is_fullscreen)
        gtk_window_fullscreen (GTK_WINDOW (window));
      else
        gtk_window_unfullscreen (GTK_WINDOW (window));
    }
  return TRUE;
}



static gboolean
terminal_window_action_readonly (TerminalWindow *window)
{
  g_return_val_if_fail (window->priv->active != NULL, FALSE);

  terminal_screen_set_input_enabled (window->priv->active, !terminal_screen_get_input_enabled (window->priv->active));
  return TRUE;
}



static gboolean
terminal_window_action_scroll_on_output (TerminalWindow *window)
{
  g_return_val_if_fail (window->priv->active != NULL, FALSE);

  terminal_screen_set_scroll_on_output (window->priv->active, !terminal_screen_get_scroll_on_output (window->priv->active));
  return TRUE;
}



static gboolean
terminal_window_action_zoom_in (TerminalWindow *window)
{
  g_return_val_if_fail (window->priv->active != NULL, FALSE);

  if (window->priv->zoom < TERMINAL_ZOOM_LEVEL_MAXIMUM)
    {
      ++window->priv->zoom;
      terminal_window_zoom_update_screens (window);
    }
  return TRUE;
}



static gboolean
terminal_window_action_zoom_out (TerminalWindow *window)
{
  g_return_val_if_fail (window->priv->active != NULL, FALSE);

  if (window->priv->zoom > TERMINAL_ZOOM_LEVEL_MINIMUM)
    {
      --window->priv->zoom;
      terminal_window_zoom_update_screens (window);
    }
  return TRUE;
}



static gboolean
terminal_window_action_zoom_reset (TerminalWindow *window)
{
  g_return_val_if_fail (window->priv->active != NULL, FALSE);

  if (window->priv->zoom != TERMINAL_ZOOM_LEVEL_DEFAULT)
    {
      window->priv->zoom = TERMINAL_ZOOM_LEVEL_DEFAULT;
      terminal_window_zoom_update_screens (window);
    }
  return TRUE;
}



static gboolean
terminal_window_action_prev_tab (TerminalWindow *window)
{
  if (terminal_window_can_go_left (window))
    {
      terminal_window_switch_tab (GTK_NOTEBOOK (window->priv->notebook), TRUE);
      return TRUE;
    }
  else
    return FALSE;
}



static gboolean
terminal_window_action_next_tab (TerminalWindow *window)
{
  if (terminal_window_can_go_right (window))
    {
      terminal_window_switch_tab (GTK_NOTEBOOK (window->priv->notebook), FALSE);
      return TRUE;
    }
  else
    return FALSE;
}



static gboolean
terminal_window_action_last_active_tab (TerminalWindow *window)
{
  if (window->priv->last_active != NULL)
    {
      GtkNotebook *notebook = GTK_NOTEBOOK (window->priv->notebook);
      gint page_num = gtk_notebook_page_num (notebook, GTK_WIDGET (window->priv->last_active));
      gtk_notebook_set_current_page (notebook, page_num);
    }

  return TRUE;
}



static gboolean
terminal_window_action_move_tab_left (TerminalWindow *window)
{
  if (terminal_window_can_go_left (window))
    {
      terminal_window_move_tab (GTK_NOTEBOOK (window->priv->notebook), TRUE);
      return TRUE;
    }
  else
    return FALSE;
}



static gboolean
terminal_window_action_move_tab_right (TerminalWindow *window)
{
  if (terminal_window_can_go_right (window))
    {
      terminal_window_move_tab (GTK_NOTEBOOK (window->priv->notebook), FALSE);
      return TRUE;
    }
  else
    return FALSE;
}



static gboolean
terminal_window_action_goto_tab (GtkRadioAction *action,
                                 GtkNotebook *notebook)
{
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_return_val_if_fail (GTK_IS_NOTEBOOK (notebook), FALSE);
  g_return_val_if_fail (GTK_IS_RADIO_ACTION (action), FALSE);

  if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)))
    {
      /* switch to the new page */
      gint page = gtk_radio_action_get_current_value (action);
      gtk_notebook_set_current_page (notebook, page);
    }
  G_GNUC_END_IGNORE_DEPRECATIONS

  return TRUE;
}



static void
title_popover_close (GtkWidget *popover,
                     TerminalWindow *window)
{
  g_return_if_fail (window->priv->title_popover != NULL);

  /* need for hiding on focus */
  if (window->priv->drop_down)
    terminal_util_activate_window (GTK_WINDOW (window));

  /* close the dialog */
  g_clear_pointer (&window->priv->title_popover, gtk_widget_destroy);

  /* focus the terminal: bug #13754 */
  if (TERMINAL_IS_SCREEN (window->priv->active))
    terminal_screen_focus (window->priv->active);
}



static void
title_popover_help (GtkWidget *popover,
                    TerminalWindow *window)
{
  /* open the "Set Title" paragraph in the "Usage" section */
  xfce_dialog_show_help (GTK_WINDOW (window), "terminal", "usage#to_change_the_terminal_title", NULL);
}



static void
title_popover_clear (GtkWidget *entry,
                     GtkEntryIconPosition icon_pos)
{
  if (icon_pos == GTK_ENTRY_ICON_SECONDARY)
    gtk_entry_set_text (GTK_ENTRY (entry), "");
}



static gboolean
terminal_window_action_set_title (TerminalWindow *window)
{
  GtkWidget *button;
  GtkWidget *box;
  GtkWidget *label;
  GtkWidget *entry;

  g_return_val_if_fail (window->priv->active != NULL, FALSE);

  if (window->priv->title_popover == NULL)
    {
      if (gtk_notebook_get_show_tabs (GTK_NOTEBOOK (window->priv->notebook)))
        {
          GtkWidget *page = terminal_window_screen_get_page (window, window->priv->active);

          window->priv->title_popover =
            gtk_popover_new (gtk_notebook_get_tab_label (GTK_NOTEBOOK (window->priv->notebook),
                                                         page != NULL ? page : GTK_WIDGET (window->priv->active)));
        }
      else
        {
          window->priv->title_popover = gtk_popover_new (GTK_WIDGET (window->menubar));
          gtk_popover_set_position (GTK_POPOVER (window->priv->title_popover), GTK_POS_BOTTOM);
        }

      box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_container_set_border_width (GTK_CONTAINER (box), 6);
      gtk_container_add (GTK_CONTAINER (window->priv->title_popover), box);

      label = gtk_label_new_with_mnemonic (_("_Title:"));
      gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);

      entry = gtk_entry_new ();
      gtk_widget_set_tooltip_text (entry, _("Enter the title for the current terminal tab"));
      gtk_box_pack_start (GTK_BOX (box), entry, TRUE, TRUE, 0);
      gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
      gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
      gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry), GTK_ENTRY_ICON_SECONDARY, "edit-clear");
      gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry), GTK_ENTRY_ICON_SECONDARY, _("Reset"));
      g_signal_connect (G_OBJECT (entry), "icon-release", G_CALLBACK (title_popover_clear), NULL);
      g_signal_connect (G_OBJECT (entry), "activate", G_CALLBACK (title_popover_close), window);

      g_object_bind_property (G_OBJECT (window->priv->active), "custom-title",
                              G_OBJECT (entry), "text",
                              G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

      button = gtk_button_new_from_icon_name ("help-browser", GTK_ICON_SIZE_BUTTON);
      gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
      gtk_widget_set_tooltip_text (button, _("Help"));
      g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (title_popover_help), window);
      gtk_box_pack_start (GTK_BOX (box), button, TRUE, TRUE, 0);

      button = gtk_button_new_from_icon_name ("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
      gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
      gtk_widget_set_tooltip_text (button, _("Close"));
      g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (title_popover_close), window);
      gtk_box_pack_start (GTK_BOX (box), button, TRUE, TRUE, 0);

      g_signal_connect (G_OBJECT (window->priv->title_popover), "closed",
                        G_CALLBACK (title_popover_close), window);
    }

  gtk_widget_show_all (window->priv->title_popover);

  return TRUE;
}



static gboolean
terminal_window_action_set_title_color (TerminalWindow *window)
{
  GtkWidget *dialog;
  gchar *color_string;
  GdkRGBA color;
  int response;

  dialog = gtk_color_chooser_dialog_new (_("Choose title color"), GTK_WINDOW (window));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Reset"), GTK_RESPONSE_NO);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_OK)
    {
      gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (dialog), &color);
      color_string = gdk_rgba_to_string (&color);
      terminal_screen_set_custom_title_color (window->priv->active, color_string);
      g_free (color_string);
    }
  else if (response == GTK_RESPONSE_NO)
    terminal_screen_set_custom_title_color (window->priv->active, NULL);

  gtk_widget_destroy (dialog);

  return TRUE;
}



static void
terminal_window_action_search_response (GtkWidget *dialog,
                                        gint response_id,
                                        TerminalWindow *window)
{
  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (TERMINAL_IS_SEARCH_DIALOG (dialog));
  g_return_if_fail (TERMINAL_IS_SCREEN (window->priv->active));
  g_return_if_fail (window->priv->search_dialog == dialog);

  if (response_id == TERMINAL_RESPONSE_SEARCH_NEXT)
    terminal_window_action_search_next (window);
  else if (response_id == TERMINAL_RESPONSE_SEARCH_PREV)
    terminal_window_action_search_prev (window);
  else
    {
      /* need for hiding on focus */
      if (window->priv->drop_down)
        terminal_util_activate_window (GTK_WINDOW (window));

      /* hide dialog */
      window->priv->n_child_windows--;
      gtk_widget_hide (dialog);
    }
}



static gboolean
terminal_window_action_search (TerminalWindow *window)
{
  if (window->priv->search_dialog == NULL)
    {
      window->priv->search_dialog = terminal_search_dialog_new (GTK_WINDOW (window));
      g_signal_connect (G_OBJECT (window->priv->search_dialog), "response",
                        G_CALLBACK (terminal_window_action_search_response), window);
      g_signal_connect (G_OBJECT (window->priv->search_dialog), "delete-event",
                        G_CALLBACK (gtk_widget_hide_on_delete), NULL);
    }

  /* increase child counter */
  if (!gtk_widget_get_visible (window->priv->search_dialog))
    window->priv->n_child_windows++;

  terminal_search_dialog_present (TERMINAL_SEARCH_DIALOG (window->priv->search_dialog));

  return TRUE;
}



static gboolean
prepare_regex (TerminalWindow *window)
{
  VteRegex *regex;
  GError *error = NULL;
  gboolean wrap_around;

  /* may occur if next/prev actions are activated by keyboard shortcut */
  if (window->priv->search_dialog == NULL)
    return FALSE;

  regex = terminal_search_dialog_get_regex (TERMINAL_SEARCH_DIALOG (window->priv->search_dialog), &error);
  if (G_LIKELY (error == NULL))
    {
      wrap_around = terminal_search_dialog_get_wrap_around (TERMINAL_SEARCH_DIALOG (window->priv->search_dialog));
      terminal_screen_search_set_gregex (window->priv->active, regex, wrap_around);
      if (regex != NULL)
        vte_regex_unref (regex);

      return TRUE;
    }

  xfce_dialog_show_error (GTK_WINDOW (window->priv->search_dialog), error,
                          _("Failed to create the regular expression"));
  g_error_free (error);

  return FALSE;
}



static gboolean
terminal_window_action_search_next (TerminalWindow *window)
{
  if (prepare_regex (window))
    terminal_screen_search_find_next (window->priv->active);
  return TRUE;
}



static gboolean
terminal_window_action_search_prev (TerminalWindow *window)
{
  if (prepare_regex (window))
    terminal_screen_search_find_previous (window->priv->active);
  return TRUE;
}



static gboolean
terminal_window_action_save_contents (TerminalWindow *window)
{
  GtkWidget *dialog;
  GFile *file;
  GOutputStream *stream;
  GError *error = NULL;
  gchar *filename_uri;
  gint response;

  g_return_val_if_fail (window->priv->active != NULL, FALSE);

  dialog = gtk_file_chooser_dialog_new (_("Save contents..."),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_SAVE, _("_Cancel"),
                                        GTK_RESPONSE_CANCEL, _("_Save"),
                                        GTK_RESPONSE_ACCEPT,
                                        NULL);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

  /* save to current working directory */
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),
                                       terminal_screen_get_working_directory (TERMINAL_SCREEN (window->priv->active)));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);

  gtk_widget_show_all (dialog);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_ACCEPT)
    {
      gtk_widget_destroy (dialog);
      return TRUE;
    }

  filename_uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));
  gtk_widget_destroy (dialog);

  if (filename_uri == NULL)
    return TRUE;

  file = g_file_new_for_uri (filename_uri);
  stream = G_OUTPUT_STREAM (g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error));
  if (stream)
    {
      terminal_screen_save_contents (TERMINAL_SCREEN (window->priv->active), stream, error);
      g_object_unref (stream);
    }

  if (error)
    {
      xfce_dialog_show_error (GTK_WINDOW (window), error, _("Failed to save terminal contents"));
      g_error_free (error);
    }

  g_object_unref (file);
  g_free (filename_uri);
  return TRUE;
}



static gboolean
terminal_window_action_reset (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    terminal_screen_reset (window->priv->active, FALSE);
  return TRUE;
}



static gboolean
terminal_window_action_reset_and_clear (TerminalWindow *window)
{
  if (G_LIKELY (window->priv->active != NULL))
    {
      terminal_screen_reset (window->priv->active, TRUE);
    }
  return TRUE;
}



static void
terminal_window_action_send_signal (SendSignalData *data)
{
  if (G_LIKELY (data->window->priv->active != NULL))
    terminal_screen_send_signal (data->window->priv->active, data->signal);
}



static gboolean
terminal_window_action_contents (TerminalWindow *window)
{
  /* don't hide the drop-down terminal */
  if (TERMINAL_IS_WINDOW_DROPDOWN (window))
    terminal_window_dropdown_ignore_next_focus_out_event (TERMINAL_WINDOW_DROPDOWN (window));

  /* open the Terminal user manual */
  xfce_dialog_show_help (GTK_WINDOW (window), "terminal", NULL, NULL);
  return TRUE;
}



static gboolean
terminal_window_action_about (TerminalWindow *window)
{
  /* don't hide the drop-down terminal */
  if (TERMINAL_IS_WINDOW_DROPDOWN (window))
    terminal_window_dropdown_ignore_next_focus_out_event (TERMINAL_WINDOW_DROPDOWN (window));

  /* display the about dialog */
  terminal_util_show_about_dialog (GTK_WINDOW (window));

  return TRUE;
}



/**
 * terminal_window_action_do_nothing:
 *
 * Used as a dummy value for go-to tab accelerators XfceGtkActionEntries.
 * If an entry doesn't have a Callback it won't appear in the XfceShortcutsEditor.
 * go-to tab accelerator entries don't have an actual tab because all their logic still uses
 * GtkActionEntries. Until that is dealt with we use their XfceGtkActionEntries only for changing the shortcuts via the editor.
 **/
static gboolean
terminal_window_action_do_nothing (TerminalWindow *window)
{
  return FALSE;
}



static void
terminal_window_zoom_update_screens (TerminalWindow *window)
{
  gint npages, n;

  g_return_if_fail (GTK_IS_NOTEBOOK (window->priv->notebook));

  /* walk the tabs, including every pane of each tab */
  npages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
  for (n = 0; n < npages; n++)
    {
      TerminalPanes *panes;
      GSList *screens, *lp;

      panes = TERMINAL_PANES (gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), n));
      screens = terminal_panes_get_screens (panes);

      for (lp = screens; lp != NULL; lp = lp->next)
        terminal_screen_update_font (TERMINAL_SCREEN (lp->data));

      g_slist_free (screens);
    }
}



static void
terminal_window_switch_tab (GtkNotebook *notebook,
                            gboolean switch_left)
{
  gint page_num;
  gint n_pages;

  page_num = gtk_notebook_get_current_page (notebook);
  n_pages = gtk_notebook_get_n_pages (notebook);
  gtk_notebook_set_current_page (notebook,
                                 (switch_left ? page_num - 1 : page_num + 1) % n_pages);
}



static void
terminal_window_move_tab (GtkNotebook *notebook,
                          gboolean move_left)
{
  gint page_num;
  gint last_page;
  GtkWidget *page;

  page_num = gtk_notebook_get_current_page (notebook);
  last_page = gtk_notebook_get_n_pages (notebook) - 1;
  page = gtk_notebook_get_nth_page (notebook, page_num);

  if (move_left)
    gtk_notebook_reorder_child (notebook, page,
                                page_num == 0 ? last_page : page_num - 1);
  else
    gtk_notebook_reorder_child (notebook, page,
                                page_num == last_page ? 0 : page_num + 1);
}



static void
terminal_window_do_close_tab (TerminalScreen *screen,
                              TerminalWindow *window)
{
  TerminalPanes *panes;
  GtkNotebook *notebook;

  g_return_if_fail (TERMINAL_IS_SCREEN (screen));
  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  panes = terminal_window_screen_get_panes (screen);
  if (panes == NULL)
    return;

  notebook = GTK_NOTEBOOK (window->priv->notebook);

  /* store attrs of the tab being closed */
  {
    TerminalTabAttr *tab_attr = terminal_tab_attr_new ();
    tab_attr->active = (screen == window->priv->active);
    tab_attr->position = gtk_notebook_page_num (notebook, GTK_WIDGET (panes));
    tab_attr->directory = g_strdup (terminal_screen_get_working_directory (screen));
    if (IS_STRING (terminal_screen_get_custom_title (screen)))
      tab_attr->title = g_strdup (terminal_screen_get_custom_title (screen));
    if (IS_STRING (terminal_screen_get_custom_fg_color (screen)))
      tab_attr->color_text = g_strdup (terminal_screen_get_custom_fg_color (screen));
    if (IS_STRING (terminal_screen_get_custom_bg_color (screen)))
      tab_attr->color_bg = g_strdup (terminal_screen_get_custom_bg_color (screen));
    if (IS_STRING (terminal_screen_get_custom_title_color (screen)))
      tab_attr->color_title = g_strdup (terminal_screen_get_custom_title_color (screen));
    g_queue_push_tail (window->priv->closed_tabs_list, tab_attr);
  }

  /* switch to the previously active tab */
  if (screen == window->priv->active && window->priv->last_active != NULL)
    {
      GtkWidget *last_page;

      last_page = terminal_window_screen_get_page (window, window->priv->last_active);
      if (last_page != NULL)
        {
          gint page_num = gtk_notebook_page_num (notebook, last_page);
          gtk_notebook_set_current_page (notebook, page_num);
        }
    }

  /* reconnect the accels of the active terminal */
  if (screen != window->priv->active)
    terminal_screen_widget_append_accels (window->priv->active, window->priv->accel_group);

  gtk_widget_destroy (GTK_WIDGET (panes));
}



static gint
terminal_window_get_workspace (TerminalWindow *window)
{
#ifdef ENABLE_X11
  GdkWindow *gdk_window;
  gint workspace = -1;
  GdkDisplay *gdk_display;
  Display *display;
  Window xwin;
  Atom property;
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char *data_p = NULL;

  gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
  if (gdk_window == NULL)
    return -1;
  gdk_display = gtk_widget_get_display (GTK_WIDGET (window));

  if (GDK_IS_X11_DISPLAY (gdk_display))
    {
      gdk_x11_display_error_trap_push (gdk_display);

      display = gdk_x11_display_get_xdisplay (gdk_display);
      xwin = gdk_x11_window_get_xid (gdk_window);
      property = XInternAtom (display, "_NET_WM_DESKTOP", False);

      if (XGetWindowProperty (display, xwin, property, 0, 1, False, XA_CARDINAL, &actual_type,
                              &actual_format, &nitems, &bytes_after, &data_p)
          == Success)
        {
          if (actual_type == XA_CARDINAL && actual_format == 32 && nitems == 1 && data_p != NULL)
            {
              workspace = *(unsigned int *) (gpointer) data_p;
            }

          if (data_p != NULL)
            XFree (data_p);
        }

      gdk_x11_display_error_trap_pop_ignored (gdk_display);

      return workspace;
    }
  else
#endif
    {
      return -1;
    }
}



/**
 * terminal_window_new:
 * @fullscreen: Whether to set the window to fullscreen.
 * @menubar   : Visibility setting for the menubar.
 * @borders   : Visibility setting for the window borders.
 * @toolbar   : Visibility setting for the toolbar.
 *
 * Return value:
 **/
GtkWidget *
terminal_window_new (const gchar *role,
                     gboolean fullscreen,
                     TerminalVisibility menubar,
                     TerminalVisibility borders,
                     TerminalVisibility toolbar)
{
  TerminalWindow *window;
  gboolean show_menubar;
  gboolean show_toolbar;
  gboolean show_borders;

  window = g_object_new (TERMINAL_TYPE_WINDOW, "role", role, NULL);

  /* read default preferences */
  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-menubar-default", &show_menubar,
                "misc-toolbar-default", &show_toolbar,
                "misc-borders-default", &show_borders,
                NULL);

  /* setup menubar visibility */
  if (G_LIKELY (menubar != TERMINAL_VISIBILITY_DEFAULT))
    show_menubar = (menubar == TERMINAL_VISIBILITY_SHOW);
  gtk_widget_set_visible (window->menubar, show_menubar);

  /* setup toolbar visibility */
  if (G_LIKELY (toolbar != TERMINAL_VISIBILITY_DEFAULT))
    show_toolbar = (toolbar == TERMINAL_VISIBILITY_SHOW);
  gtk_widget_set_visible (window->toolbar, show_toolbar);

  /* setup borders visibility */
  if (G_LIKELY (borders != TERMINAL_VISIBILITY_DEFAULT))
    show_borders = (borders == TERMINAL_VISIBILITY_SHOW);
  gtk_window_set_decorated (GTK_WINDOW (window), show_borders);

  /* setup full screen */
  window->is_fullscreen = fullscreen && window->fullscreen_supported;

  /* property that is not suitable for init */
  g_object_bind_property (G_OBJECT (window->priv->preferences), "misc-tab-position",
                          G_OBJECT (window->priv->notebook), "tab-pos",
                          G_BINDING_SYNC_CREATE);

  return GTK_WIDGET (window);
}



/**
 * terminal_window_add:
 * @window  : A #TerminalWindow.
 * @screen  : A #TerminalScreen.
 *
 * Wraps @screen in a new pane tree and adds it to the notebook
 * as a new tab.
 **/
void
terminal_window_add (TerminalWindow *window,
                     TerminalScreen *screen)
{
  TerminalPanes *panes;
  gint page, position = -1;
  gboolean adjacent;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (TERMINAL_IS_SCREEN (screen));

  /* show the terminal screen first: see bug #13263*/
  gtk_widget_show (GTK_WIDGET (screen));

  /* create the pane tree that wraps the screen */
  panes = terminal_panes_new_with_screen (screen);

  terminal_window_add_panes (window, panes);

  /* determine the tab position */
  g_object_get (G_OBJECT (window->priv->preferences), "misc-new-tab-adjacent", &adjacent, NULL);
  if (G_UNLIKELY (adjacent))
    {
      GtkWidget *active_page;

      active_page = terminal_window_screen_get_page (window, window->priv->active);
      if (active_page != NULL)
        position = gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), active_page) + 1;
    }

  page = gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), GTK_WIDGET (panes));
  if (position >= 0 && page != position)
    gtk_notebook_reorder_child (GTK_NOTEBOOK (window->priv->notebook), GTK_WIDGET (panes), position);

  /* switch to the new tab */
  page = gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), GTK_WIDGET (panes));
  gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook), page);

  /* focus the terminal */
  terminal_screen_focus (screen);
}



/**
 * terminal_window_add_panes:
 * @window  : A #TerminalWindow.
 * @panes   : A #TerminalPanes pane tree.
 *
 * Adds an existing pane tree (which may hold a split layout) to
 * @window as a new tab.
 **/
void
terminal_window_add_panes (TerminalWindow *window,
                           TerminalPanes *panes)
{
  GtkWidget *label;
  gint page;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));
  g_return_if_fail (TERMINAL_IS_PANES (panes));

  /* the "active-screen-changed" handler is (dis)connected by the
   * notebook page-added/page-removed handlers */

  /* create the tab label */
  label = terminal_window_create_panes_label (window, panes);

  /* show the pane tree first, see bug #13263 (the notebook does not show the
   * page itself and window init already ran before the tab was added) */
  gtk_widget_show (GTK_WIDGET (panes));

  page = gtk_notebook_insert_page (GTK_NOTEBOOK (window->priv->notebook), GTK_WIDGET (panes), label, -1);
  gtk_container_child_set (GTK_CONTAINER (window->priv->notebook), GTK_WIDGET (panes), "tab-expand", TRUE, NULL);
  gtk_container_child_set (GTK_CONTAINER (window->priv->notebook), GTK_WIDGET (panes), "tab-fill", TRUE, NULL);

  /* allow tab sorting and dnd */
  gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (window->priv->notebook), GTK_WIDGET (panes), TRUE);
  gtk_notebook_set_tab_detachable (GTK_NOTEBOOK (window->priv->notebook), GTK_WIDGET (panes), TRUE);

  /* update scrollbar visibility */
  if (window->priv->scrollbar_visibility != TERMINAL_VISIBILITY_DEFAULT)
    {
      GSList *screens, *lp;

      screens = terminal_panes_get_screens (panes);
      for (lp = screens; lp != NULL; lp = lp->next)
        terminal_screen_update_scrolling_bar (TERMINAL_SCREEN (lp->data));
      g_slist_free (screens);
    }

  /* switch to the new tab */
  gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook), page);
}



/**
 * terminal_window_get_active:
 * @window : a #TerminalWindow.
 *
 * Returns the active #TerminalScreen for @window
 * or %NULL.
 *
 * Return value: the active #TerminalScreen for @window.
 **/
TerminalScreen *
terminal_window_get_active (TerminalWindow *window)
{
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), NULL);
  return window->priv->active;
}



/**
 * terminal_window_notebook_show_tabs:
 * @window  : A #TerminalWindow.
 **/
void
terminal_window_notebook_show_tabs (TerminalWindow *window)
{
  GtkNotebook *notebook = GTK_NOTEBOOK (window->priv->notebook);
  gboolean show_tabs = TRUE;
  gint npages;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  /* check preferences */
  npages = gtk_notebook_get_n_pages (notebook);
  if (npages < 2)
    {
      g_object_get (G_OBJECT (window->priv->preferences),
                    window->priv->drop_down ? "dropdown-always-show-tabs" : "misc-always-show-tabs",
                    &show_tabs,
                    NULL);
    }

  /* set the visibility of the tabs */
  if (gtk_notebook_get_show_tabs (notebook) != show_tabs)
    {
      /* store size */
      terminal_window_size_push (window);

      /* show or hide the tabs */
      gtk_notebook_set_show_tabs (notebook, show_tabs);

      /* update the window geometry */
      terminal_window_size_pop (window);
    }
}



/**
 * terminal_window_get_restart_command:
 * @window  : A #TerminalWindow.
 *
 * Return value: A list of strings, which are required to
 *               restart the window properly with all tabs
 *               and settings. The strings and the list itself
 *               need to be freed afterwards.
 **/
GSList *
terminal_window_get_restart_command (TerminalWindow *window)
{
  const gchar *role;
  GdkScreen *gscreen;
  GList *children, *lp;
  GSList *result = NULL;
  glong w;
  glong h;
  gint workspace;

  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), NULL);

  if (G_LIKELY (window->priv->active != NULL))
    {
      terminal_screen_get_size (window->priv->active, &w, &h);
      result = g_slist_prepend (result, g_strdup_printf ("--geometry=%ldx%ld", w, h));
    }

  gscreen = gtk_window_get_screen (GTK_WINDOW (window));
  if (G_LIKELY (gscreen != NULL))
    {
      result = g_slist_prepend (result, g_strdup ("--display"));
      result = g_slist_prepend (result, g_strdup (gdk_display_get_name (gdk_screen_get_display (gscreen))));
    }

  role = gtk_window_get_role (GTK_WINDOW (window));
  if (G_LIKELY (role != NULL))
    result = g_slist_prepend (result, g_strdup_printf ("--role=%s", role));

  workspace = terminal_window_get_workspace (window);
  if (workspace != -1)
    {
      result = g_slist_prepend (result, g_strdup ("--workspace"));
      result = g_slist_prepend (result, g_strdup_printf ("%d", workspace));
    }

  if (window->is_fullscreen)
    result = g_slist_prepend (result, g_strdup ("--fullscreen"));

  if (gtk_widget_is_visible (window->menubar))
    result = g_slist_prepend (result, g_strdup ("--show-menubar"));
  else
    result = g_slist_prepend (result, g_strdup ("--hide-menubar"));

  if (gtk_window_get_decorated (GTK_WINDOW (window)))
    result = g_slist_prepend (result, g_strdup ("--show-borders"));
  else
    result = g_slist_prepend (result, g_strdup ("--hide-borders"));

  if (gtk_widget_is_visible (window->toolbar))
    result = g_slist_prepend (result, g_strdup ("--show-toolbar"));
  else
    result = g_slist_prepend (result, g_strdup ("--hide-toolbar"));

  if (window->priv->zoom != TERMINAL_ZOOM_LEVEL_DEFAULT)
    result = g_slist_prepend (result, g_strdup_printf ("--zoom=%d", window->priv->zoom));
  if (window->priv->font != NULL)
    result = g_slist_prepend (result, g_strdup_printf ("--font=%s", window->priv->font));

  /* set restart commands of the tabs */
  children = gtk_container_get_children (GTK_CONTAINER (window->priv->notebook));
  for (lp = children; lp != NULL; lp = lp->next)
    {
      if (lp != children)
        result = g_slist_prepend (result, g_strdup ("--tab"));
      if (window->priv->active == lp->data)
        result = g_slist_prepend (result, g_strdup ("--active-tab"));
      result = g_slist_concat (terminal_screen_get_restart_command (lp->data), result);
    }
  g_list_free (children);

  return g_slist_reverse (result);
}



/**
 * terminal_window_set_grid_size:
 * @window  : A #TerminalWindow.
 * @width   : Window width.
 * @height  : Window height.
 **/
void
terminal_window_set_grid_size (TerminalWindow *window,
                               glong width,
                               glong height)
{
  window->priv->grid_width = width;
  window->priv->grid_height = height;
}



/**
 * terminal_window_has_children:
 * @window  : A #TerminalWindow.
 **/
gboolean
terminal_window_has_children (TerminalWindow *window)
{
  return window->priv->n_child_windows != 0;
}



/**
 * terminal_window_get_preferences:
 * @window  : A #TerminalWindow.
 **/
GObject *
terminal_window_get_preferences (TerminalWindow *window)
{
  return G_OBJECT (window->priv->preferences);
}



/**
 * terminal_window_get_vbox:
 * @window  : A #TerminalWindow.
 **/
GtkWidget *
terminal_window_get_vbox (TerminalWindow *window)
{
  return window->priv->vbox;
}



/**
 * terminal_window_get_notebook:
 * @window  : A #TerminalWindow.
 **/
GtkWidget *
terminal_window_get_notebook (TerminalWindow *window)
{
  return window->priv->notebook;
}



/**
 * terminal_window_get_preferences_dialog:
 * @window  : A #TerminalWindow.
 **/
GtkWidget *
terminal_window_get_preferences_dialog (TerminalWindow *window)
{
  return window->priv->preferences_dialog;
}



/**
 * terminal_window_get_font:
 * @window  : A #TerminalWindow.
 **/
const gchar *
terminal_window_get_font (TerminalWindow *window)
{
  return window->priv->font;
}



/**
 * terminal_window_set_font:
 * @window  : A #TerminalWindow.
 **/
void
terminal_window_set_font (TerminalWindow *window,
                          const gchar *font)
{
  g_return_if_fail (font != NULL);
  g_free (window->priv->font);
  window->priv->font = g_strdup (font);
}



/**
 * terminal_window_get_scrollbar_visibility:
 * @window  : A #TerminalWindow.
 **/
TerminalVisibility
terminal_window_get_scrollbar_visibility (TerminalWindow *window)
{
  return window->priv->scrollbar_visibility;
}



/**
 * terminal_window_set_scrollbar_visibility:
 * @window    : A #TerminalWindow.
 * @scrollbar : Scrollbar visibility.
 **/
void
terminal_window_set_scrollbar_visibility (TerminalWindow *window,
                                          TerminalVisibility scrollbar)
{
  window->priv->scrollbar_visibility = scrollbar;
}



/**
 * terminal_window_get_zoom_level:
 * @window  : A #TerminalWindow.
 **/
TerminalZoomLevel
terminal_window_get_zoom_level (TerminalWindow *window)
{
  return window->priv->zoom;
}



/**
 * terminal_window_set_zoom_level:
 * @window  : A #TerminalWindow.
 * @zoom    : Zoom level.
 **/
void
terminal_window_set_zoom_level (TerminalWindow *window,
                                TerminalZoomLevel zoom)
{
  window->priv->zoom = zoom;
}



/**
 * terminal_window_is_drop_down:
 * @window  : A #TerminalWindow.
 **/
gboolean
terminal_window_is_drop_down (TerminalWindow *window)
{
  return window->priv->drop_down != 0;
}



/**
 * terminal_window_set_drop_down:
 * @window    : A #TerminalWindow.
 * @drop_down : Whether the window is drop-down.
 **/
void
terminal_window_set_drop_down (TerminalWindow *window,
                               gboolean drop_down)
{
  window->priv->drop_down = drop_down;
}



/**
 * terminal_window_get_menubar_height:
 * @window  : A #TerminalWindow.
 **/
gint
terminal_window_get_menubar_height (TerminalWindow *window)
{
  GtkRequisition req;

  req.height = 0;

  if (window->menubar != NULL && gtk_widget_get_visible (window->menubar))
    gtk_widget_get_preferred_size (window->menubar, &req, NULL);

  return req.height;
}



/**
 * terminal_window_get_toolbar_height:
 * @window  : A #TerminalWindow.
 **/
gint
terminal_window_get_toolbar_height (TerminalWindow *window)
{
  GtkRequisition req;

  req.height = 0;

  if (window->toolbar != NULL && gtk_widget_get_visible (window->toolbar))
    gtk_widget_get_preferred_size (window->toolbar, &req, NULL);

  return req.height;
}



static void
terminal_window_create_menu (TerminalWindow *window,
                             TerminalWindowAction action,
                             GCallback cb_update_menu)
{
  GtkWidget *item;
  GtkWidget *submenu;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (action), G_OBJECT (window), GTK_MENU_SHELL (window->menubar));

  submenu = g_object_new (GTK_TYPE_MENU, NULL);
  gtk_menu_set_accel_group (GTK_MENU (submenu), window->priv->accel_group);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (submenu));
  g_signal_connect_swapped (G_OBJECT (submenu), "show", G_CALLBACK (cb_update_menu), window);

  if (action == TERMINAL_WINDOW_ACTION_TABS_MENU)
    {
      window->priv->tabs_menu = submenu;
      g_object_add_weak_pointer (G_OBJECT (submenu), (gpointer *) &window->priv->tabs_menu);
    }
}



static void
terminal_window_menu_clean (GtkMenu *menu)
{
  GList *children, *lp;
  GtkWidget *submenu;

  children = gtk_container_get_children (GTK_CONTAINER (menu));
  for (lp = children; lp != NULL; lp = lp->next)
    {
      submenu = gtk_menu_item_get_submenu (lp->data);
      if (submenu != NULL)
        gtk_widget_destroy (submenu);
      gtk_container_remove (GTK_CONTAINER (menu), lp->data);
    }
  g_list_free (children);
}



static void
terminal_window_menu_add_section (TerminalWindow *window,
                                  GtkWidget *menu,
                                  MenuSections sections,
                                  gboolean as_submenu)
{
#define AS_SUBMENU(text) \
  if (as_submenu) \
    { \
      item = gtk_menu_item_new_with_label (text); \
      submenu = g_object_new (GTK_TYPE_MENU, NULL); \
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (submenu)); \
      insert_to_menu = GTK_WIDGET (submenu); \
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item); \
    }

  GtkWidget *item;
  GtkWidget *insert_to_menu;
  GtkMenu *submenu;
  static const gint n_signal_names = 31;

  insert_to_menu = menu;

  if (sections & MENU_SECTION_SIGNAL)
    {
      AS_SUBMENU (_("Send Signal"))
      for (int i = 1; i < n_signal_names; i++)
        {
          gchar *label;
          SendSignalData *p = g_new (SendSignalData, 1);
          p->window = window;
          p->signal = i;
          label = g_strdup_printf ("%i - %s", i, signal_names[i]);
          item = gtk_menu_item_new_with_mnemonic (label);
          g_free (label);
          g_signal_connect_data (G_OBJECT (item), "activate", G_CALLBACK (terminal_window_action_send_signal), p, terminal_util_free_data, G_CONNECT_SWAPPED);
          gtk_menu_shell_append (GTK_MENU_SHELL (insert_to_menu), item);
        }
      xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
    }

  if (sections & MENU_SECTION_ZOOM)
    {
      AS_SUBMENU (_("Zoom"));

      item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_ZOOM_IN), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      gtk_widget_set_sensitive (item, window->priv->zoom != TERMINAL_ZOOM_LEVEL_MAXIMUM);
      item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_ZOOM_OUT), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      gtk_widget_set_sensitive (item, window->priv->zoom != TERMINAL_ZOOM_LEVEL_MINIMUM);
      xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_ZOOM_RESET), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
    }

  if (sections & MENU_SECTION_COPY)
    {
      AS_SUBMENU (_("Copy"));

      item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_COPY), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      gtk_widget_set_sensitive (item, terminal_screen_has_selection (window->priv->active));
      if (sections & MENU_SECTION_COPY_AS)
        {
          item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_COPY_HTML), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
          gtk_widget_set_sensitive (item, terminal_screen_has_selection (window->priv->active));
          item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_COPY_AS), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
          gtk_widget_set_sensitive (item, terminal_screen_has_selection (window->priv->active));
        }
      xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
    }

  if (sections & MENU_SECTION_PASTE)
    {
      AS_SUBMENU (_("Paste"));

      item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PASTE), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      gtk_widget_set_sensitive (item, terminal_screen_get_input_enabled (window->priv->active));
      item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PASTE_SELECTION), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      gtk_widget_set_sensitive (item, terminal_screen_get_input_enabled (window->priv->active));

    }

  if (sections & MENU_SECTION_SPLIT)
    {
      AS_SUBMENU (_("Split"))

      xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SPLIT_HORIZONTAL), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SPLIT_VERTICAL), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
      xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CLOSE_PANE), G_OBJECT (window), GTK_MENU_SHELL (insert_to_menu));
    }

  if (sections & MENU_SECTION_VIEW)
    {
      AS_SUBMENU (_("View Options"));

      xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SHOW_MENUBAR), G_OBJECT (window), gtk_widget_is_visible (window->menubar), GTK_MENU_SHELL (insert_to_menu));
      xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SHOW_TOOLBAR), G_OBJECT (window), gtk_widget_is_visible (window->toolbar), GTK_MENU_SHELL (insert_to_menu));
      xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SHOW_BORDERS), G_OBJECT (window), gtk_window_get_decorated (GTK_WINDOW (window)), GTK_MENU_SHELL (insert_to_menu));
      xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_FULLSCREEN), G_OBJECT (window), window->is_fullscreen, GTK_MENU_SHELL (insert_to_menu));
      xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
    }

#undef AS_SUBMENU
}



static void
terminal_window_update_file_menu (TerminalWindow *window,
                                  GtkWidget *menu)
{
  GtkWidget *item;
  gint n_pages;
  gchar *bookmarks;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  /* "Detach Tab" and "Close Other Tabs" are sensitive if we have at least two pages.
   * "Undo Close" is sensitive if there is a tab to unclose. */

  /* determine the number of pages */
  n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-directory-bookmarks", &bookmarks,
                NULL);
  g_object_set_data_full (G_OBJECT (window), "directory-bookmarks", bookmarks, g_free);

  terminal_window_menu_clean (GTK_MENU (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEW_TAB), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEW_WINDOW), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_OPEN_FOLDER), G_OBJECT (window), GTK_MENU_SHELL (menu));
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_UNDO_CLOSE_TAB), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, !g_queue_is_empty (window->priv->closed_tabs_list));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_DETACH_TAB), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, n_pages > 1);
  /* the bookmarks of the settings, one flat item per directory: a submenu of
   * our own here only brings the trouble that comes with it, and with a couple
   * of bookmarks the flat list is the shorter way round anyway */
  bookmarks = g_object_get_data (G_OBJECT (window), "directory-bookmarks");
  if (bookmarks != NULL && *bookmarks != '\0')
    {
      gchar **dirs = g_strsplit (bookmarks, ":", -1);
      guint i;
      gboolean any = FALSE;

      for (i = 0; dirs[i] != NULL; i++)
        {
          gchar *label;

          if (*dirs[i] == '\0')
            continue;

          label = g_strdup_printf ("_cd %s", dirs[i]);
          item = gtk_menu_item_new_with_mnemonic (label);
          g_free (label);

          g_object_set_data_full (G_OBJECT (item), "bookmark-path",
                                  g_strdup (dirs[i]), g_free);
          g_signal_connect (G_OBJECT (item), "activate",
                            G_CALLBACK (terminal_window_action_goto_bookmark), window);
          gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
          any = TRUE;
        }
      g_strfreev (dirs);

      if (any)
        xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
    }

  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CLOSE_TAB), G_OBJECT (window), GTK_MENU_SHELL (menu));
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CLOSE_OTHER_TABS), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, n_pages > 1);
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CLOSE_TABS_RIGHT), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, n_pages > 1 && terminal_window_can_go_right (window));
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CLOSE_ALL_TABS), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, n_pages > 1);
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CLOSE_WINDOW), G_OBJECT (window), GTK_MENU_SHELL (menu));

  gtk_widget_show_all (GTK_WIDGET (menu));
}



static void
terminal_window_update_edit_menu (TerminalWindow *window,
                                  GtkWidget *menu)
{
  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  terminal_window_menu_clean (GTK_MENU (menu));
  terminal_window_menu_add_section (window, menu, MENU_SECTION_COPY | MENU_SECTION_COPY_AS | MENU_SECTION_PASTE, FALSE);
  /* in the "Edit" menu the slots follow the hotkey that walks them */
  terminal_window_add_slots_menu (window, menu, TRUE);
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SELECT_ALL), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_COPY_INPUT), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PREFERENCES), G_OBJECT (window), GTK_MENU_SHELL (menu));

  gtk_widget_show_all (GTK_WIDGET (menu));
}



static void
terminal_window_update_view_menu (TerminalWindow *window,
                                  GtkWidget *menu)
{
  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  terminal_window_menu_clean (GTK_MENU (menu));
  terminal_window_menu_add_section (window, menu, MENU_SECTION_VIEW, FALSE);
  terminal_window_menu_add_section (window, menu, MENU_SECTION_ZOOM, FALSE);

  gtk_widget_show_all (GTK_WIDGET (menu));
}



static void
terminal_window_update_terminal_menu (TerminalWindow *window,
                                      GtkWidget *menu)
{
  GtkWidget *item;
  gboolean can_search;

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  terminal_window_menu_clean (GTK_MENU (menu));
  terminal_window_menu_add_section (window, menu, MENU_SECTION_SPLIT, TRUE);
  /* tmux stands beside the split section, not inside it: in one menu it is
   * looked for by itself, and every entry must occur once */
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_START_TMUX), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SET_TITLE), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SET_TITLE_COLOR), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  can_search = terminal_screen_search_has_gregex (window->priv->active);
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SEARCH), G_OBJECT (window), GTK_MENU_SHELL (menu));
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SEARCH_NEXT), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, can_search);
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SEARCH_PREV), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, can_search);
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));

  /* Set Encoding uses the TerminalAction, GtkAction, therefore it is deprecated */
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_action_create_menu_item (window->priv->encoding_action));
  G_GNUC_END_IGNORE_DEPRECATIONS
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_READ_ONLY), G_OBJECT (window), !terminal_screen_get_input_enabled (window->priv->active), GTK_MENU_SHELL (menu));
  xfce_gtk_toggle_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SCROLL_ON_OUTPUT), G_OBJECT (window), terminal_screen_get_scroll_on_output (window->priv->active), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_SAVE_CONTENTS), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));

  terminal_window_menu_add_section (window, menu, MENU_SECTION_SIGNAL, TRUE);
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_RESET), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_RESET_AND_CLEAR), G_OBJECT (window), GTK_MENU_SHELL (menu));

  gtk_widget_show_all (GTK_WIDGET (menu));
}



static void
terminal_window_update_tabs_menu (TerminalWindow *window,
                                  GtkWidget *menu)
{
  GtkWidget *item;
  gint n_pages;
  gboolean can_go_left;
  gboolean can_go_right;

  /* go-to menu */
  gint n;
  GtkWidget *page;
  GSList *group = NULL;
  GtkRadioAction *radio_action = NULL;
  GtkAccelKey key = { 0 };
  gchar name[50], buf[100];

  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));

  can_go_left = terminal_window_can_go_left (window);
  can_go_right = terminal_window_can_go_right (window);

  terminal_window_menu_clean (GTK_MENU (menu));
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_PREV_TAB), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, can_go_left);
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_NEXT_TAB), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, can_go_right);
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_LAST_ACTIVE_TAB), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, window->priv->last_active != NULL);
  xfce_gtk_menu_append_separator (GTK_MENU_SHELL (menu));
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_MOVE_TAB_LEFT), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, can_go_left);
  item = xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_MOVE_TAB_RIGHT), G_OBJECT (window), GTK_MENU_SHELL (menu));
  gtk_widget_set_sensitive (item, can_go_right);

  /* go-to menu */
  for (n = 0; n < n_pages; n++)
    {
      page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (window->priv->notebook), n);

      g_snprintf (name, sizeof (name), "goto-tab-%d", n + 1);

      /* create action */
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      radio_action = gtk_radio_action_new (name, NULL, NULL, NULL, n);
      gtk_action_set_sensitive (GTK_ACTION (radio_action), n_pages > 1);
      g_object_bind_property (G_OBJECT (page), "title",
                              G_OBJECT (radio_action), "label",
                              G_BINDING_SYNC_CREATE);
      gtk_radio_action_set_group (radio_action, group);
      group = gtk_radio_action_get_group (radio_action);
      gtk_action_set_accel_group (GTK_ACTION (radio_action), window->priv->accel_group);
      G_GNUC_END_IGNORE_DEPRECATIONS

      g_signal_connect (G_OBJECT (radio_action), "activate",
                        G_CALLBACK (terminal_window_action_goto_tab), window->priv->notebook);

      /* connect action to the page, so we can activate it when a tab is switched */
      g_object_set_qdata_full (G_OBJECT (page), tabs_menu_action_quark, radio_action, g_object_unref);

      /* set an accelerator path */
      g_snprintf (buf, sizeof (buf), "<Actions>/terminal-window/%s", name);
      if (gtk_accel_map_lookup_entry (buf, &key) && key.accel_key != 0)
        {
          G_GNUC_BEGIN_IGNORE_DEPRECATIONS
          gtk_action_set_accel_path (GTK_ACTION (radio_action), buf);
          G_GNUC_END_IGNORE_DEPRECATIONS
        }

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      /* add action in the menu */
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_action_create_menu_item (GTK_ACTION (radio_action)));
      G_GNUC_END_IGNORE_DEPRECATIONS

      /* store */
      window->priv->tabs_menu_actions = g_slist_prepend (window->priv->tabs_menu_actions, radio_action);
    }

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  if (n_pages > 1)
    gtk_radio_action_set_current_value (radio_action, gtk_notebook_get_current_page (GTK_NOTEBOOK (window->priv->notebook)));
  G_GNUC_END_IGNORE_DEPRECATIONS

  gtk_widget_show_all (GTK_WIDGET (menu));
}



/**
 * terminal_window_update_goto_accels:
 * @window  : A #TerminalWindow.
 *
 * Calls terminal_window_update_tabs_menu to create the go-to actions and their accelerators.
 **/
void
terminal_window_update_goto_accels (TerminalWindow *window)
{
  if (window->priv->tabs_menu != NULL)
    terminal_window_update_tabs_menu (window, window->priv->tabs_menu);
}



static void
terminal_window_update_help_menu (TerminalWindow *window,
                                  GtkWidget *menu)
{
  g_return_if_fail (TERMINAL_IS_WINDOW (window));

  terminal_window_menu_clean (GTK_MENU (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_CONTENTS), G_OBJECT (window), GTK_MENU_SHELL (menu));
  xfce_gtk_menu_item_new_from_action_entry (get_action_entry (TERMINAL_WINDOW_ACTION_ABOUT), G_OBJECT (window), GTK_MENU_SHELL (menu));

  gtk_widget_show_all (GTK_WIDGET (menu));
}



static gboolean
terminal_window_can_go_left (TerminalWindow *window)
{
  gint page_num;
  gint n_pages;
  gboolean cycle_tabs;
  GtkWidget *page;


  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
  page = terminal_window_screen_get_page (window, window->priv->active);
  page_num = (page != NULL)
               ? gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), page)
               : -1;

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-cycle-tabs", &cycle_tabs,
                NULL);

  return (cycle_tabs && n_pages > 1) || (page_num > 0);
}



static gboolean
terminal_window_can_go_right (TerminalWindow *window)
{
  gint page_num;
  gint n_pages;
  gboolean cycle_tabs;
  GtkWidget *page;


  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), FALSE);

  n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook));
  page = terminal_window_screen_get_page (window, window->priv->active);
  page_num = (page != NULL)
               ? gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook), page)
               : -1;

  g_object_get (G_OBJECT (window->priv->preferences),
                "misc-cycle-tabs", &cycle_tabs,
                NULL);

  return (cycle_tabs && n_pages > 1) || (page_num < n_pages - 1);
}



XfceGtkActionEntry *
terminal_window_get_action_entry (TerminalWindow *window,
                                  TerminalWindowAction action)
{
  return (XfceGtkActionEntry *) get_action_entry (action);
}



XfceGtkActionEntry *
terminal_window_get_action_entries (void)
{
  return action_entries;
}
