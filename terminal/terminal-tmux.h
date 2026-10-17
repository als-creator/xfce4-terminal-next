/*
 * terminal-tmux.h
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

#ifndef __TERMINAL_TMUX_H__
#define __TERMINAL_TMUX_H__

#include <glib.h>

G_BEGIN_DECLS

gchar *terminal_tmux_build_command (const gchar *command,
                                    const gchar *session,
                                    gboolean      true_color,
                                    gboolean      mouse);

G_END_DECLS

#endif /* __TERMINAL_TMUX_H__ */
