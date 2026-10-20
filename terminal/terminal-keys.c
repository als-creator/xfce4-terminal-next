/*-
 * Copyright (c) 2026 als-creator
 *
 * Layout independent accelerators for xfce4-terminal.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "terminal-keys.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#endif

/* Whether one of the keys is held down right now, asked of the X server
 * directly (see terminal_keys_shift_held() below for why). */
static gboolean
terminal_keys_keys_held (const guint32 *keyvals,
                         guint          n_keyvals)
{
#ifdef GDK_WINDOWING_X11
  GdkDisplay *display;
  Display *xdisplay;
  GdkKeymap *keymap;
  guchar keys[32];
  gboolean held = FALSE;
  guint i;

  display = gdk_display_get_default ();
  if (display == NULL || !GDK_IS_X11_DISPLAY (display))
    return FALSE;

  keymap = gdk_keymap_get_for_display (display);
  xdisplay = gdk_x11_display_get_xdisplay (display);
  if (keymap == NULL || xdisplay == NULL)
    return FALSE;

  XQueryKeymap (xdisplay, (gchar *) keys);

  for (i = 0; i < n_keyvals && !held; i++)
    {
      GdkKeymapKey *entries = NULL;
      gint n_entries = 0;
      gint j;

      if (!gdk_keymap_get_entries_for_keyval (keymap, keyvals[i], &entries, &n_entries)
          || entries == NULL)
        continue;

      for (j = 0; j < n_entries; j++)
        if ((keys[entries[j].keycode >> 3] & (1 << (entries[j].keycode & 7))) != 0)
          {
            held = TRUE;
            break;
          }

      g_free (entries);
    }

  return held;
#else
  (void) keyvals;
  (void) n_keyvals;

  return FALSE;
#endif
}

/* Whether the shift key is held down right now.
 *
 * When Ctrl+Shift is the layout switch (grp:ctrl_shift_toggle) the group
 * change consumes the shift: it never reaches event->state, and the key press
 * looks exactly like Ctrl+<key> without shift. The layout switch sits below
 * GDK, but the X server still knows the real state of the physical keys, so
 * ask it directly. Wayland has no such query here, the fallback keeps the old
 * behaviour. */
static gboolean
terminal_keys_shift_held (void)
{
  static const guint32 keyvals[2] = { GDK_KEY_Shift_L, GDK_KEY_Shift_R };

  return terminal_keys_keys_held (keyvals, G_N_ELEMENTS (keyvals));
}

/* Whether the control key is held down right now; the same question as
 * terminal_keys_shift_held() asked about the other half of the switch. With
 * the shift pressed first the group change consumes the control instead, and
 * the key press arrives with shift alone in the state. */
static gboolean
terminal_keys_control_held (void)
{
  static const guint32 keyvals[2] = { GDK_KEY_Control_L, GDK_KEY_Control_R };

  return terminal_keys_keys_held (keyvals, G_N_ELEMENTS (keyvals));
}

/* The latin symbol of a physical key, asked from the X server itself.
 *
 * GDK keeps its own copy of the keymap and refreshes it while it handles
 * events; ask it outside of that and it answers from the copy it had before
 * the layout switch moved to the other group — a key press then looks latin
 * where it should look cyrillic, and the accelerators sit idle until the
 * language is switched back and forth once. The X server refreshes its own map
 * on every group change, so the first group of the keycode asked of it is
 * always the latin one.
 *
 * Returns %TRUE if the keycode has a symbol in the first group.
 */
static gboolean
terminal_keys_latin_symbol (guint   keycode,
                            guint32 *symbol)
{
#ifdef GDK_WINDOWING_X11
  GdkDisplay *display;
  Display *xdisplay;
  KeySym keysym;

  display = gdk_display_get_default ();
  if (display == NULL || !GDK_IS_X11_DISPLAY (display))
    return FALSE;

  xdisplay = GDK_DISPLAY_XDISPLAY (display);
  if (xdisplay == NULL)
    return FALSE;

  /* group 0, level 0: the latin symbol of the key, neither shifted nor
   * switched to another group */
  keysym = XkbKeycodeToKeysym (xdisplay, (KeyCode) keycode, 0, 0);
  if (keysym == NoSymbol)
    return FALSE;

  *symbol = (guint32) keysym;

  return TRUE;
#else
  (void) keycode;
  (void) symbol;

  return FALSE;
#endif
}


/* Whether the keysym is one of the modifier keys themselves: a lone modifier
 * press is no accelerator, so there is nothing to restore for it. */
static gboolean
terminal_keys_keyval_is_modifier (guint32 keyval)
{
  switch (keyval)
    {
    case GDK_KEY_Shift_L:
    case GDK_KEY_Shift_R:
    case GDK_KEY_Control_L:
    case GDK_KEY_Control_R:
    case GDK_KEY_Alt_L:
    case GDK_KEY_Alt_R:
    case GDK_KEY_Meta_L:
    case GDK_KEY_Meta_R:
    case GDK_KEY_Super_L:
    case GDK_KEY_Super_R:
    case GDK_KEY_Hyper_L:
    case GDK_KEY_Hyper_R:
    case GDK_KEY_Caps_Lock:
    case GDK_KEY_Shift_Lock:
    case GDK_KEY_Num_Lock:
    case GDK_KEY_ISO_Level3_Shift:
    case GDK_KEY_ISO_Level3_Latch:
    case GDK_KEY_ISO_Level3_Lock:
    case GDK_KEY_Mode_switch:
    /* with Ctrl+Shift as the layout switch the key press of shift itself
     * arrives as a group switch keysym, not as Shift_L/Shift_R */
    case GDK_KEY_ISO_Next_Group:
    case GDK_KEY_ISO_Prev_Group:
    case GDK_KEY_ISO_First_Group:
    case GDK_KEY_ISO_Last_Group:
    case GDK_KEY_ISO_Group_Latch:
    case GDK_KEY_ISO_Group_Lock:
      return TRUE;
    default:
      return FALSE;
    }
}

/* Whether the shift key is worth asking about: a press that carries another
 * modifier but no shift. Plain typing stays free of a server round trip.
 *
 * Every such press needs the question, not only the ones with a latin symbol:
 * with tab, arrows and function keys nothing but the modifier is missing, and
 * with the latin letters of the layout the switch may have sent the key press
 * over to the latin group. */
static gboolean
terminal_keys_shift_may_be_swallowed (const GdkEventKey *event)
{
  if ((event->state & GDK_SHIFT_MASK) != 0)
    return FALSE;

  if ((event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) == 0)
    return FALSE;

  return !terminal_keys_keyval_is_modifier (event->keyval);
}

/**
 * terminal_keys_swallowed_shift:
 * @event: a key press event
 *
 * Whether the layout switch swallowed the shift of @event (see
 * terminal_keys_shift_held()). The caller can then either normalize the key
 * press with terminal_keys_normalize_layout_accel_ext() — which takes the state
 * of the shift as a parameter — or put %GDK_SHIFT_MASK back into the event so
 * that every other consumer of the key press (the tab handler, the menu
 * bindings, the terminal widget) sees the press the user really made. A key
 * without any latin symbol (Ctrl+Shift+Tab, Ctrl+Shift+Left) needs the latter,
 * since nothing but the modifier is missing for it.
 *
 * Returns %TRUE if the shift is held down but missing from the event.
 */
gboolean
terminal_keys_swallowed_shift (GdkEventKey *event)
{
  g_return_val_if_fail (event != NULL, FALSE);

  return terminal_keys_shift_may_be_swallowed (event) && terminal_keys_shift_held ();
}

/* Whether the layout switch swallowed the control of @event (see
 * terminal_keys_control_held()). When Shift was pressed before Control,
 * grp:ctrl_shift_toggle consumes the control modifier and the event carries
 * only %GDK_SHIFT_MASK; the X server still knows Control is held down. */
gboolean
terminal_keys_swallowed_ctrl (GdkEventKey *event)
{
  g_return_val_if_fail (event != NULL, FALSE);

  if ((event->state & GDK_CONTROL_MASK) != 0)
    return FALSE;

  if ((event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) == 0)
    return FALSE;

  return !terminal_keys_keyval_is_modifier (event->keyval) && terminal_keys_control_held ();
}

/**
 * terminal_keys_normalize_layout_accel:
 * @event: a key press event
 * @keyval: (out): the latin keysym of the physical key
 * @mods: (out): the modifiers of the key press
 *
 * Layout independent normalization of the accelerators.
 *
 * Under a non-latin layout a key press such as Ctrl+Shift+O produces a cyrillic
 * keysym. The accelerators of the window are stored as latin keysyms, so they
 * never match and they look dead. This maps the key press back to the latin
 * symbol of the same physical key.
 *
 * Returns %TRUE if the key press could be normalized, %FALSE if it is not
 * layout dependent and should be handled by the accelerators as they are.
 */
gboolean
terminal_keys_normalize_layout_accel (GdkEventKey  *event,
                                      guint32      *keyval,
                                      guint        *mods)
{
  gboolean shift_held;

  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (keyval != NULL, FALSE);
  g_return_val_if_fail (mods != NULL, FALSE);

  shift_held = terminal_keys_swallowed_shift (event);

  return terminal_keys_normalize_layout_accel_ext (event, shift_held, keyval, mods);
}

/**
 * terminal_keys_normalize_layout_accel_ext:
 * @event: a key press event
 * @shift_held: whether the shift key is really held down
 * @keyval: (out): the latin keysym of the physical key
 * @mods: (out): the modifiers of the key press
 *
 * The body of terminal_keys_normalize_layout_accel() with the state of the
 * shift key passed in. An accelerator of the window may be stored with shift
 * while the layout switch (Ctrl+Shift, grp:ctrl_shift_toggle) swallowed it and
 * the event carries no %GDK_SHIFT_MASK at all; the caller looks up the real
 * state of the key and hands it over, so the tests can cover the case without
 * a running X server. The switch may also turn the group over to the latin one
 * first, then the key press already carries the latin symbol and only the
 * modifier is missing.
 *
 * Returns %TRUE if the key press could be normalized.
 */
gboolean
terminal_keys_normalize_layout_accel_ext (GdkEventKey  *event,
                                         gboolean      shift_held,
                                         guint32      *keyval,
                                         guint        *mods)
{
  GdkKeymap *keymap;
  guint keycode;
  guint32 symbol;
  guint state;

  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (keyval != NULL, FALSE);
  g_return_val_if_fail (mods != NULL, FALSE);

  state = event->state;
  gboolean ctrl_held = terminal_keys_swallowed_ctrl (event);
  if (shift_held)
    state |= GDK_SHIFT_MASK;
  if (ctrl_held)
    state |= GDK_CONTROL_MASK;

  /* a key press that does not depend on the layout at all: the accelerators
   * handle it as it is. With a swallowed control it does depend: the event
   * carries no %GDK_CONTROL_MASK and no accelerator of ctrl+shift would match
   * it, so the press has to be normalized */
  if (event->keyval <= 0x7e && !shift_held && !ctrl_held)
    return FALSE;

  keymap = gdk_keymap_get_for_display (gdk_display_get_default ());
  if (keymap == NULL)
    return FALSE;

  /* X11 reports the real keycode, but the Wayland backend and synthetic events
   * (XTest, xdotool) may leave it unset; then find the key through the keysym
   * instead, which works on both backends because GDK answers keymap queries
   * through xkbcommon there */
  keycode = event->hardware_keycode;
  if (keycode == 0)
    {
      GdkKeymapKey *sym_entries;
      gint n_sym_entries;

      sym_entries = NULL;
      n_sym_entries = 0;

      if (gdk_keymap_get_entries_for_keyval (keymap, event->keyval, &sym_entries, &n_sym_entries)
          && sym_entries != NULL && n_sym_entries > 0)
        keycode = sym_entries[0].keycode;

      g_free (sym_entries);

      if (keycode == 0)
        return FALSE;
    }

  /* translate the key press back to the latin symbol of the physical key: the
   * first group of the layout (latin on every layout that matters) and level 0,
   * i.e. the symbol does not depend on the group the layout switch moved to nor
   * on the shift it swallowed. The server is asked, not the copy GDK keeps */
  symbol = 0;
  if (!terminal_keys_latin_symbol (keycode, &symbol))
    if (!gdk_keymap_translate_keyboard_state (keymap, keycode, state & ~GDK_SHIFT_MASK, 0,
                                              &symbol, NULL, NULL, NULL))
      return FALSE;

  /* a key without a latin symbol in the first group (tab, arrows, function
   * keys) does not depend on the layout at all: with an unswallowed shift
   * nothing is to normalize, with a swallowed one only the modifier was missing
   * and the symbol of the physical key is the one to look up (a left tab
   * translates to a plain tab, so the shift binding needs no special case) */
  if (symbol == 0 || symbol > 0x7e)
    {
      if (!shift_held && !ctrl_held)
        return FALSE;

      *keyval = symbol == 0 ? event->keyval : symbol;
      *mods = state & gtk_accelerator_get_default_mod_mask ();

      return TRUE;
    }

  /* the key press already carries the latin symbol of the physical key and the
   * modifiers of the event: there is nothing to normalize */
  if (symbol == event->keyval && !shift_held && !ctrl_held)
    return FALSE;

  *keyval = symbol;
  *mods = state & gtk_accelerator_get_default_mod_mask ();

  return TRUE;
}



/**
 * terminal_keys_accel_matches:
 * @accel: an accelerator string, e.g. "<control><shift>o"
 * @keyval: a keysym
 * @mods: the modifiers of a key press
 *
 * Checks whether @keyval with @mods activates @accel.
 *
 * Returns %TRUE if the accelerator matches.
 */
gboolean
terminal_keys_accel_matches (const gchar *accel,
                             guint32      keyval,
                             guint        mods)
{
  guint entry_key = 0;
  GdkModifierType entry_mods = 0;
  guint mask;

  if (accel == NULL || *accel == '\0')
    return FALSE;

  mask = gtk_accelerator_get_default_mod_mask ();

  /* gtk_accelerator_parse() returns void in newer gtk releases */
  gtk_accelerator_parse (accel, &entry_key, &entry_mods);

  return entry_key == keyval
         && entry_key != 0
         && (entry_mods & mask) == (mods & mask);
}
