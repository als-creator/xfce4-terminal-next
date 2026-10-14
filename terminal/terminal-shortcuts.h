/*-
 * Copyright (c) 2026 The Xfce Terminal Local contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
 * Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef TERMINAL_SHORTCUTS_H
#define TERMINAL_SHORTCUTS_H

#include <glib.h>

G_BEGIN_DECLS

/* The desktop registers application shortcuts in the xfce4-keyboard-shortcuts
 * channel: /commands/custom/<combination> holds the command line and an
 * optional /startup-notify flag below it.  The terminal keeps no global key
 * grab of its own (that is X11-only and upstream has none), the panel is
 * shown and hidden by running the drop-down option, which the user (or the
 * preferences dialog) assigns as a system shortcut. */

#define TERMINAL_SHORTCUT_CHANNEL "xfce4-keyboard-shortcuts"
#define TERMINAL_SHORTCUT_COMMAND "xfce4-terminal-local --drop-down"

gchar *
terminal_shortcuts_get_drop_down_binding (void);

gchar *
terminal_shortcuts_get_conflict (const gchar *binding);

gboolean
terminal_shortcuts_set_drop_down_binding (const gchar *binding);

void
terminal_shortcuts_remove_drop_down_binding (void);

G_END_DECLS

#endif /* !TERMINAL_SHORTCUTS_H */
