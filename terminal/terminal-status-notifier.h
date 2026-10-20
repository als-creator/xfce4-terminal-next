/*-
 * Copyright (c) 2026 als-creator
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* StatusNotifierItem of the drop-down panel. */

#ifndef TERMINAL_STATUS_NOTIFIER_H
#define TERMINAL_STATUS_NOTIFIER_H

#include <glib-object.h>

G_BEGIN_DECLS

/* StatusNotifierItem: the tray item that replaced GtkStatusIcon.  It is a
 * plain D-Bus interface (org.kde.StatusNotifierItem), so it works in an X11
 * and in a Wayland session and needs no KDE library.  The desktop needs a
 * StatusNotifierWatcher to show it, which is what terminal_status_notifier_new()
 * looks for; without one there is no tray icon, exactly as before. */
#define TERMINAL_SNI_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define TERMINAL_SNI_WATCHER_PATH "/StatusNotifierWatcher"

typedef struct _TerminalStatusNotifier TerminalStatusNotifier;

typedef struct
{
  const gchar *id;
  const gchar *label;
  void (*activate) (gpointer user_data);
} TerminalStatusNotifierMenuItem;

typedef struct
{
  const gchar *id;
  const gchar *icon_name;
  const gchar *title;
  const gchar *tooltip;
  /* left click on the icon */
  void (*activate) (gpointer user_data);
  /* right click on the icon */
  void (*context_menu) (gpointer user_data);
  /* the menu of the icon (com.canonical.dbusmenu, drawn by the host) */
  const TerminalStatusNotifierMenuItem *menu_items;
  guint n_menu_items;
  gpointer user_data;
} TerminalStatusNotifierInfo;

/* Whether a StatusNotifierWatcher is on the bus, that is whether the desktop
 * can show a tray icon at all. */
gboolean
terminal_status_notifier_is_supported (void);

TerminalStatusNotifier *
terminal_status_notifier_new (const TerminalStatusNotifierInfo *info);

void
terminal_status_notifier_free (TerminalStatusNotifier *notifier);

void
terminal_status_notifier_set_icon (TerminalStatusNotifier *notifier,
                                   const gchar *icon_name);

void
terminal_status_notifier_set_tooltip (TerminalStatusNotifier *notifier,
                                      const gchar *tooltip);

void
terminal_status_notifier_menu_set_label (TerminalStatusNotifier *notifier,
                                         guint item_index,
                                         const gchar *label);

G_END_DECLS

#endif /* !TERMINAL_STATUS_NOTIFIER_H */
