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

/* The modifier keys that are held down right now, as far as we know: the
 * layout switch may have eaten one of them out of the event state, see
 * terminal_keys_key_press(). The bits are %GDK_SHIFT_MASK, %GDK_CONTROL_MASK
 * and %GDK_MOD1_MASK. */
guint terminal_keys_modifiers_held (void);

/* Put back the modifier that the layout switch ate out of the state of the
 * key press, and keep track of the keys that are down. */
void terminal_keys_key_press (GdkEventKey *event);

/* Keep track of the keys that are up again. */
void terminal_keys_key_release (GdkEventKey *event);

/* The modifier keys may have been pressed while another window had the
 * focus: ask for them when the focus comes in. */
void terminal_keys_focus_in (void);

/* The modifier keys may be released while another window has the focus:
 * forget them when the focus goes out. */
void terminal_keys_focus_out (void);

/* The modifiers of a key press that the layout switch swallowed: they are
 * held down but missing from the state of the event. */
guint terminal_keys_swallowed_modifiers (GdkEventKey *event,
                                         guint        mods_held);

gboolean terminal_keys_normalize_layout_accel (GdkEventKey  *event,
                                               guint32      *keyval,
                                               guint        *mods);

/* Internal: the body of the function above with the state of the modifier
 * keys passed in, so the tests can cover a swallowed Ctrl+Shift or Alt+Shift
 * layout switch without a running X server. */
gboolean terminal_keys_normalize_layout_accel_ext (GdkEventKey  *event,
                                                   guint         mods_held,
                                                   guint32      *keyval,
                                                   guint        *mods);

gboolean terminal_keys_accel_matches (const gchar *accel,
                                      guint32      keyval,
                                      guint        mods);

G_END_DECLS

#endif /* TERMINAL_KEYS_H */
