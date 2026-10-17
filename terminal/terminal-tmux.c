/*
 * The line that puts a pane into tmux.
 *
 * The preferences hand over four answers — a session name and three
 * checkboxes — and this builds the command out of them. It is kept apart from
 * the window so that it can be tested without a display: tmux 3.8 gives no way
 * to tell from the outside whether an option was set by us or is just the
 * server default (mouse is on by default there), so the only honest place to
 * check the line is the code that builds it.
 *
 * Copyright (c) 2026 als-creator
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
 * Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include "terminal-tmux.h"

/* The settings go after the session command because tmux applies them to the
 * session it has just attached to: that is where passthrough (24-bit colour)
 * and the window size are wanted, and where the mouse belongs too — though
 * only when it is asked for, since it takes the mouse away from selecting text
 * in the terminal.
 *
 * "\\;" is the shell quoting of tmux's own command separator: the shell must
 * hand both commands to tmux instead of running the second one itself.
 *
 * An empty @command means "build it here"; a line in @command is the user's
 * own and replaces the whole thing, options included.
 *
 * Returns a newly allocated string. */
gchar *
terminal_tmux_build_command (const gchar *command,
                             const gchar *session,
                             gboolean      true_color,
                             gboolean      mouse)
{
  GString *line;

  if (command != NULL && *command != '\0')
    return g_strdup (command);

  if (session == NULL || *session == '\0')
    session = "main";

  line = g_string_new ("TERM=tmux-256color tmux new-session -A -s ");
  g_string_append (line, session);

  if (true_color)
    g_string_append (line, " \\; set -g allow-passthrough on");
  g_string_append (line, " \\; set -g window-size latest");
  if (mouse)
    g_string_append (line, " \\; set -g mouse on");

  return g_string_free (line, FALSE);
}
