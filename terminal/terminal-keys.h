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

/* Layout independent accelerators for xfce4-terminal. */

#ifndef TERMINAL_KEYS_H
#define TERMINAL_KEYS_H

#include <glib.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

/* Whether the layout switch swallowed the shift of this key press; ask the X
 * server about the real state of the physical keys. Returns %TRUE when the
 * shift is held down but missing from the event. */
gboolean terminal_keys_swallowed_shift (GdkEventKey *event);
gboolean terminal_keys_swallowed_ctrl (GdkEventKey *event);

gboolean terminal_keys_normalize_layout_accel (GdkEventKey  *event,
                                               guint32      *keyval,
                                               guint        *mods);

/* Internal: the body of the function above with the state of the shift key
 * passed in, so the tests can cover a swallowed Ctrl+Shift layout switch
 * without a running X server. */
gboolean terminal_keys_normalize_layout_accel_ext (GdkEventKey  *event,
                                                   gboolean      shift_held,
                                                   guint32      *keyval,
                                                   guint        *mods);

gboolean terminal_keys_accel_matches (const gchar *accel,
                                      guint32      keyval,
                                      guint        mods);

G_END_DECLS

#endif /* TERMINAL_KEYS_H */
