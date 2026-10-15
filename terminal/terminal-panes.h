/*-
 * Copyright (c) 2026 als-creator
 *
 * Split-pane container for xfce4-terminal.
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

#ifndef TERMINAL_PANES_H
#define TERMINAL_PANES_H

#include <gtk/gtk.h>

#include "terminal-screen.h"

G_BEGIN_DECLS

#define TERMINAL_TYPE_PANES (terminal_panes_get_type ())
G_DECLARE_FINAL_TYPE (TerminalPanes, terminal_panes, TERMINAL, PANES, GtkBin)

typedef enum
{
  TERMINAL_PANES_DIRECTION_LEFT,
  TERMINAL_PANES_DIRECTION_RIGHT,
  TERMINAL_PANES_DIRECTION_UP,
  TERMINAL_PANES_DIRECTION_DOWN
} TerminalPanesDirection;

enum
{
  TERMINAL_PANES_ACTIVE_SCREEN_CHANGED,
  TERMINAL_PANES_N_SIGNALS
};

TerminalPanes *
terminal_panes_new_with_screen (TerminalScreen *screen);

/**
 * Splits @screen inside @panes. The existing screen stays on one side,
 * @new_screen is placed on the other side and becomes the active pane.
 */
void
terminal_panes_split_screen (TerminalPanes *panes,
                             TerminalScreen *screen,
                             GtkOrientation orientation,
                             TerminalScreen *new_screen);

/**
 * Same as terminal_panes_split_screen(), but puts the divider at @fraction
 * (0.0 - 1.0) of the split instead of the middle.
 */
void
terminal_panes_split_screen_at (TerminalPanes *panes,
                                TerminalScreen *screen,
                                GtkOrientation orientation,
                                TerminalScreen *new_screen,
                                gdouble fraction);

/**
 * Shifts the divider of the split the given pane lives in, the way Guake moves
 * and resizes a split with its move-terminal-split-* hotkeys.
 */
void
terminal_panes_move_pane (TerminalPanes *panes,
                          TerminalScreen *screen,
                          TerminalPanesDirection direction);

/**
 * Returns the pane layout as a newly allocated string, in the format that the
 * window replays when it restores the panes of a tab. Free with g_free().
 */
gchar *
terminal_panes_get_layout (TerminalPanes *panes);

/**
 * Same as terminal_panes_get_layout(), but every pane also contributes the
 * directory it was last in, one per pane in the same order as the layout:
 * "H50,.,.)|/home/user/src|/tmp".
 *
 * A directory can contain anything, so the count of the fields is taken from
 * the layout and not from the separators; an empty field means that no
 * directory was remembered for that pane. Records without any field (written
 * by an older version) stay valid: they simply carry no directories.
 */
gchar *
terminal_panes_get_session_layout (TerminalPanes *panes);

/**
 * Splits a record written by terminal_panes_get_session_layout() into its
 * directories. Returns a newly allocated NULL-terminated array to be freed with
 * g_strfreev(), or NULL when the record carries no directories at all.
 */
gchar **
terminal_panes_session_cdirs (const gchar *record);

/**
 * Removes @screen from the pane tree, collapsing the split.
 *
 * Returns %FALSE (and does nothing) if @screen is the only pane left; in that
 * case the caller should close the whole tab instead. Returns %TRUE when the
 * screen was removed and destroyed, in which case @screen must not be used
 * again.
 */
gboolean
terminal_panes_remove_screen (TerminalPanes *panes,
                              TerminalScreen *screen);

TerminalScreen *
terminal_panes_get_active (TerminalPanes *panes);

void
terminal_panes_set_active (TerminalPanes *panes,
                           TerminalScreen *screen);

/**
 * Returns a newly allocated list (free with g_slist_free) of all
 * #TerminalScreen widgets in the tree, in left-to-right/top-to-bottom order.
 */
GSList *
terminal_panes_get_screens (TerminalPanes *panes);

gboolean
terminal_panes_contains (TerminalPanes *panes,
                        TerminalScreen *screen);

guint
terminal_panes_get_n_screens (TerminalPanes *panes);

/**
 * Moves the pane focus to the next/previous pane in reading order
 * (left-to-right, then top-to-bottom), wrapping around.
 */
void
terminal_panes_focus_next (TerminalPanes *panes);

void
terminal_panes_focus_previous (TerminalPanes *panes);

/**
 * terminal_panes_equalize:
 * @panes: a #TerminalPanes
 *
 * Puts every divider in the middle, so that after a series of resizes all the
 * panes are of equal size again.
 */
void
terminal_panes_equalize (TerminalPanes *panes);

/**
 * terminal_panes_focus_index:
 * @panes: a #TerminalPanes
 * @index: zero based number of the pane, in the order panes are walked
 *
 * Puts the focus into the pane of that number, as tmux and kitty do with their
 * numbered panes. Nothing happens when there is no such pane.
 */
void
terminal_panes_focus_index (TerminalPanes *panes,
                            guint     index);

/**
 * terminal_panes_rotate:
 * @panes: a #TerminalPanes
 *
 * Turns every divider of the split onto the other axis, which is what tmux
 * calls rotate-layout.
 */
void
terminal_panes_rotate (TerminalPanes *panes);

/**
 * terminal_panes_swap_screens:
 * @panes: a #TerminalPanes
 * @first: one of the screens of @panes
 * @second: another one of the screens of @panes
 *
 * Puts the two screens in each others places, so that the processes of both
 * keep running while they change what is on the screen. The focus goes to
 * @second, as in tmux swap-pane.
 */
void
terminal_panes_swap_screens (TerminalPanes *panes,
                             TerminalScreen *first,
                             TerminalScreen *second);

/**
 * Moves the pane focus in the given direction based on pane geometry.
 */
void
terminal_panes_focus_direction (TerminalPanes *panes,
                                TerminalPanesDirection direction);

G_END_DECLS

#endif /* !TERMINAL_PANES_H */