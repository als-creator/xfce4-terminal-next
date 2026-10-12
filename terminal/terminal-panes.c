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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include "terminal-panes.h"



struct _TerminalPanes
{
  GtkBin parent_instance;

  /* root of the pane tree: either a #TerminalScreen or a #GtkPaned */
  GtkWidget *root;

  /* the currently focused terminal inside the tree */
  TerminalScreen *active;

  /* TRUE while remove_screen() is tearing a pane down on purpose, so the
   * screen's "destroy" handler does not run the collapse/normalise path twice */
  gboolean removing;
};



/* data key used to set the pane position to half only once after allocation */
#define PANES_OUR_POSITION "panes-our-position"



enum
{
  PROP_TITLE = 1,
  N_PROPERTIES
};

static GParamSpec *panes_properties[N_PROPERTIES];

static guint panes_signals[TERMINAL_PANES_N_SIGNALS];



static void
panes_emit_active_changed (TerminalPanes *panes,
                           TerminalScreen *screen)
{
  g_signal_emit (panes, panes_signals[TERMINAL_PANES_ACTIVE_SCREEN_CHANGED], 0,
                 screen);
  g_object_notify (G_OBJECT (panes), "title");
}



static void
terminal_panes_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  TerminalPanes *panes = TERMINAL_PANES (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      if (panes->active != NULL)
        g_value_take_string (value, terminal_screen_get_title (panes->active));
      else
        g_value_set_string (value, NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
terminal_panes_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}



G_DEFINE_TYPE (TerminalPanes, terminal_panes, GTK_TYPE_BIN)



static void
terminal_panes_class_init (TerminalPanesClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = terminal_panes_get_property;
  gobject_class->set_property = terminal_panes_set_property;

  /**
   * TerminalPanes:title:
   *
   * Title of the active terminal inside the pane tree.
   **/
  panes_properties[PROP_TITLE] =
    g_param_spec_string ("title", "title", "Title of the active terminal",
                         NULL,
                         G_PARAM_READWRITE);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, panes_properties);

  /**
   * TerminalPanes::active-screen-changed:
   *
   * Emitted when the focused terminal inside the pane tree changed.
   **/
  panes_signals[TERMINAL_PANES_ACTIVE_SCREEN_CHANGED] =
    g_signal_new (I_ ("active-screen-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, TERMINAL_TYPE_SCREEN);
}



static void
terminal_panes_init (TerminalPanes *panes)
{
  panes->root = NULL;
  panes->active = NULL;
}



/* returns TRUE when the widget could be identified as a screen */
static gboolean
panes_tree_has_screen (GtkWidget *node,
                       TerminalScreen *screen)
{
  if (node == NULL)
    return FALSE;

  if (TERMINAL_IS_SCREEN (node))
    return node == GTK_WIDGET (screen);

  if (GTK_IS_PANED (node))
    {
      GtkPaned *paned = GTK_PANED (node);

      if (panes_tree_has_screen (gtk_paned_get_child1 (paned), screen))
        return TRUE;
      if (panes_tree_has_screen (gtk_paned_get_child2 (paned), screen))
        return TRUE;
    }

  return FALSE;
}



static void
panes_collect_screens (GtkWidget *node,
                       GSList **screens)
{
  if (node == NULL)
    return;

  if (TERMINAL_IS_SCREEN (node))
    {
      *screens = g_slist_append (*screens, node);
      return;
    }

  if (GTK_IS_PANED (node))
    {
      GtkPaned *paned = GTK_PANED (node);

      panes_collect_screens (gtk_paned_get_child1 (paned), screens);
      panes_collect_screens (gtk_paned_get_child2 (paned), screens);
    }
}



/* returns the first screen in the subtree, or NULL */
static TerminalScreen *
panes_tree_first_screen (GtkWidget *node)
{
  GSList *screens = NULL;
  TerminalScreen *screen;

  panes_collect_screens (node, &screens);

  screen = (screens != NULL) ? TERMINAL_SCREEN (screens->data) : NULL;

  g_slist_free (screens);

  return screen;
}



static gboolean
panes_screen_focus_in (GtkWidget *widget,
                       GdkEventFocus *event G_GNUC_UNUSED,
                       TerminalPanes *panes)
{
  terminal_panes_set_active (panes, TERMINAL_SCREEN (widget));

  return FALSE;
}



/* focus-in on the embedded terminal; @screen is the user data */
static gboolean
panes_terminal_focus_in (GtkWidget *widget G_GNUC_UNUSED,
                         GdkEventFocus *event G_GNUC_UNUSED,
                         TerminalScreen *screen)
{
  TerminalPanes *panes = g_object_get_data (G_OBJECT (screen), "terminal-panes");

  if (panes != NULL)
    terminal_panes_set_active (panes, screen);

  return FALSE;
}



static void
panes_screen_title_changed (TerminalScreen *screen,
                            GParamSpec *pspec G_GNUC_UNUSED,
                            TerminalPanes *panes)
{
  /* only re-emit the title change when the screen is the active pane */
  if (panes->active == screen)
    g_object_notify (G_OBJECT (panes), "title");
}



/* The notebook page is the pane tree, not the screen (unlike upstream, where
 * the screen itself is the page and destroying it drops the tab). When a
 * process exits, terminal-screen.c destroys its TerminalScreen directly, so
 * the pane tree has to react, otherwise the page survives with no child and
 * renders as an empty grey rectangle. */
static gboolean
panes_destroy_idle (gpointer user_data)
{
  TerminalPanes *panes = TERMINAL_PANES (user_data);

  if (TERMINAL_IS_PANES (panes))
    gtk_widget_destroy (GTK_WIDGET (panes));
  g_object_unref (panes);
  return G_SOURCE_REMOVE;
}



/* Collapse every paned that is left with a single child (or none) after a
 * screen was destroyed. GTK detaches the screen from its paned before the
 * "destroy" signal is emitted, so the collapsing can no longer be driven from
 * the screen's former parent; the tree is normalised instead. Returns the node
 * that should take @node's place in its parent (NULL when the subtree is
 * empty). */
static GtkWidget *
panes_collapse_single_child_paneds (TerminalPanes *panes,
                                    GtkWidget     *node)
{
  GtkWidget *child1, *child2;

  if (node == NULL || !GTK_IS_PANED (node))
    return node;

  child1 = gtk_paned_get_child1 (GTK_PANED (node));
  child2 = gtk_paned_get_child2 (GTK_PANED (node));

  child1 = panes_collapse_single_child_paneds (panes, child1);
  child2 = panes_collapse_single_child_paneds (panes, child2);

  /* still a real split */
  if (child1 != NULL && child2 != NULL)
    return node;

  /* nothing survives below this paned */
  if (child1 == NULL && child2 == NULL)
    {
      gtk_widget_destroy (node);
      return NULL;
    }

  /* single child: replace the paned by the surviving child */
  {
    GtkWidget *keep = (child1 != NULL) ? child1 : child2;
    GtkWidget *parent = gtk_widget_get_parent (node);

    g_object_ref (keep);
    g_object_ref (node);

    gtk_container_remove (GTK_CONTAINER (node), keep);

    if (parent == GTK_WIDGET (panes))
      {
        gtk_container_remove (GTK_CONTAINER (panes), node);
        panes->root = keep;
        gtk_container_add (GTK_CONTAINER (panes), keep);
        gtk_widget_show (keep);
      }
    else if (GTK_IS_PANED (parent))
      {
        GtkPaned *grand = GTK_PANED (parent);
        gboolean was_child2 = (gtk_paned_get_child2 (grand) == node);

        gtk_container_remove (GTK_CONTAINER (grand), node);
        if (was_child2)
          gtk_paned_pack2 (grand, keep, TRUE, TRUE);
        else
          gtk_paned_pack1 (grand, keep, TRUE, TRUE);
        gtk_widget_show (keep);
      }

    gtk_widget_destroy (node);
    g_object_unref (keep);
    g_object_unref (node);

    return keep;
  }
}



static void
panes_normalize_after_screen_destroy (TerminalPanes *panes)
{
  GtkWidget *new_root;

  if (panes->root == NULL)
    return;

  new_root = panes_collapse_single_child_paneds (panes, panes->root);
  panes->root = new_root;

  /* no screens left: the tab has to disappear as a whole */
  if (panes->root == NULL)
    {
      g_object_ref (panes);
      g_idle_add (panes_destroy_idle, panes);
      return;
    }

  /* the active screen may have been the destroyed one */
  if (panes->active == NULL
      || !panes_tree_has_screen (panes->root, panes->active))
    {
      TerminalScreen *new_active = panes_tree_first_screen (panes->root);

      panes->active = new_active;
      panes_emit_active_changed (panes, new_active);

      /* the focused terminal was the destroyed pane; move the focus to the
       * survivor so the keyboard keeps working */
      if (new_active != NULL)
        terminal_screen_focus (new_active);
    }

  /* the survivor must fill the whole pane tree again */
  {
    GtkAllocation pa;
    gint w, h;

    gtk_widget_get_allocation (GTK_WIDGET (panes), &pa);
    w = pa.width;
    h = pa.height;

    gtk_widget_queue_resize (GTK_WIDGET (panes));

    if (w > 1 && h > 1)
      {
        GtkAllocation full = { 0, 0, w, h };

        gtk_widget_size_allocate (GTK_WIDGET (panes), &full);
      }
  }
}



static void
panes_screen_destroyed (GtkWidget *screen,
                        gpointer   user_data)
{
  TerminalPanes *panes = TERMINAL_PANES (user_data);

  /* the whole tree is going away anyway: closing a tab destroys the pane
   * tree, which in turn disposes the screen and lands here again */
  if (gtk_widget_in_destruction (GTK_WIDGET (panes)))
    return;

  /* remove_screen() already collapsed the tree and will fix the active screen
   * itself; do not run the normalisation a second time */
  if (panes->removing)
    return;

  if (panes->root == screen)
    {
      /* last pane of the tab: drop the whole tree so the notebook removes the
       * page and the tab closes instead of showing an empty grey area. Defer
       * the destroy because we are inside the screen's own destruction. */
      g_object_ref (panes);
      g_idle_add (panes_destroy_idle, panes);
      return;
    }

  /* a split lost one of its panes: collapse it, repair root/active */
  panes_normalize_after_screen_destroy (panes);
}



static void
panes_watch_screen (TerminalPanes *panes,
                    TerminalScreen *screen)
{
  GtkWidget *terminal = terminal_screen_get_terminal_widget (screen);

  /* the screen may be destroyed on its own when its child process exits */
  g_signal_connect (G_OBJECT (screen), "destroy",
                    G_CALLBACK (panes_screen_destroyed), panes);

  /* remember which pane tree owns this screen so the terminal's focus
   * handler can find it again */
  g_object_set_data (G_OBJECT (screen), "terminal-panes", panes);

  /* the focus actually lands on the embedded terminal, so watch it there;
   * focus-in events are delivered to the focused widget, not to parents */
  if (terminal != NULL)
    g_signal_connect (G_OBJECT (terminal), "focus-in-event",
                      G_CALLBACK (panes_terminal_focus_in), screen);
  g_signal_connect (G_OBJECT (screen), "focus-in-event",
                    G_CALLBACK (panes_screen_focus_in), panes);
  g_signal_connect (G_OBJECT (screen), "notify::title",
                    G_CALLBACK (panes_screen_title_changed), panes);
}



static void
panes_paned_size_allocate (GtkWidget *widget,
                           GtkAllocation *allocation,
                           gpointer user_data G_GNUC_UNUSED)
{
  GtkOrientation orientation;
  gint size, half, current, ours;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (widget));
  size = (orientation == GTK_ORIENTATION_HORIZONTAL)
           ? allocation->width
           : allocation->height;

  /* ignore degenerate allocations (e.g. a 1x1 size during construction) */
  if (size <= 1)
    return;

  half = MAX (1, size / 2);
  current = gtk_paned_get_position (GTK_PANED (widget));
  ours = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
                                             PANES_OUR_POSITION));

  /* keep the divider centered on every allocation (which also fixes up the
   * position after a transient construction-time size), but stop as soon as
   * the user has dragged the divider somewhere else */
  if (current == 0 || current == ours)
    {
      gtk_paned_set_position (GTK_PANED (widget), half);
      g_object_set_data (G_OBJECT (widget), PANES_OUR_POSITION,
                         GINT_TO_POINTER (half));
    }
}



static void
panes_paned_position_half (GtkWidget *paned)
{
  /* wait for an allocation so the position can be computed from the real
   * pane size; the handler keeps the divider centered until the user moves it */
  g_signal_connect (paned, "size-allocate",
                    G_CALLBACK (panes_paned_size_allocate), NULL);
}



TerminalPanes *
terminal_panes_new_with_screen (TerminalScreen *screen)
{
  TerminalPanes *panes;

  g_return_val_if_fail (TERMINAL_IS_SCREEN (screen), NULL);

  panes = g_object_new (TERMINAL_TYPE_PANES, NULL);

  panes->root = GTK_WIDGET (screen);
  panes->active = screen;

  gtk_container_add (GTK_CONTAINER (panes), GTK_WIDGET (screen));
  gtk_widget_show (GTK_WIDGET (screen));

  panes_watch_screen (panes, screen);

  return panes;
}



void
terminal_panes_split_screen (TerminalPanes *panes,
                             TerminalScreen *screen,
                             GtkOrientation orientation,
                             TerminalScreen *new_screen)
{
  GtkWidget *parent;
  GtkWidget *paned;
  gboolean screen_was_child2 = FALSE;

  g_return_if_fail (TERMINAL_IS_PANES (panes));
  g_return_if_fail (TERMINAL_IS_SCREEN (screen));
  g_return_if_fail (TERMINAL_IS_SCREEN (new_screen));

  /* the screen must be part of this pane tree */
  if (!panes_tree_has_screen (panes->root, screen))
    {
      g_warning ("TerminalPanes: screen to split is not part of the pane tree");
      return;
    }

  parent = gtk_widget_get_parent (GTK_WIDGET (screen));

  paned = gtk_paned_new (orientation);

  /* GTK refuses to reparent a widget that still has a parent, so detach the
   * screen from its current container first. Keep a reference across the
   * removal, which may otherwise drop the last reference and destroy it. */
  g_object_ref (screen);

  /* remember where the screen sat in its parent paned before detaching */
  if (GTK_IS_PANED (parent))
    screen_was_child2 = (gtk_paned_get_child2 (GTK_PANED (parent)) == GTK_WIDGET (screen));

  if (parent != NULL)
    gtk_container_remove (GTK_CONTAINER (parent), GTK_WIDGET (screen));

  gtk_paned_pack1 (GTK_PANED (paned), GTK_WIDGET (screen), TRUE, TRUE);
  gtk_paned_pack2 (GTK_PANED (paned), GTK_WIDGET (new_screen), TRUE, TRUE);

  gtk_widget_show (GTK_WIDGET (new_screen));
  gtk_widget_show (paned);

  if (parent == GTK_WIDGET (panes))
    {
      /* the screen was the root leaf: the paned becomes the new root */
      panes->root = paned;
      gtk_container_add (GTK_CONTAINER (panes), paned);
    }
  else if (GTK_IS_PANED (parent))
    {
      /* replace the screen's former position in the parent paned */
      GtkPaned *parent_paned = GTK_PANED (parent);

      if (screen_was_child2)
        gtk_paned_pack2 (parent_paned, paned, TRUE, TRUE);
      else
        gtk_paned_pack1 (parent_paned, paned, TRUE, TRUE);
    }
  else
    {
      g_warning ("TerminalPanes: unexpected parent widget for screen");
      g_object_unref (screen);
      return;
    }

  g_object_unref (screen);

  panes_watch_screen (panes, new_screen);
  panes_paned_position_half (paned);

  /* make the new pane active */
  panes->active = new_screen;
  panes_emit_active_changed (panes, new_screen);
  terminal_screen_focus (new_screen);
}



gboolean
terminal_panes_remove_screen (TerminalPanes *panes,
                              TerminalScreen *screen)
{
  GtkWidget *parent;
  GtkWidget *other;
  GtkWidget *paned_parent;

  g_return_val_if_fail (TERMINAL_IS_PANES (panes), FALSE);
  g_return_val_if_fail (TERMINAL_IS_SCREEN (screen), FALSE);

  /* last remaining pane: the caller has to close the whole tab instead */
  if (panes->root == GTK_WIDGET (screen))
    return FALSE;

  if (!panes_tree_has_screen (panes->root, screen))
    {
      g_warning ("TerminalPanes: screen to remove is not part of the pane tree");
      return FALSE;
    }

  parent = gtk_widget_get_parent (GTK_WIDGET (screen));
  if (!GTK_IS_PANED (parent))
    {
      g_warning ("TerminalPanes: unexpected parent widget for screen");
      return FALSE;
    }

  {
    GtkPaned *paned = GTK_PANED (parent);
    gboolean was_active = (panes->active == screen);
    gboolean paned_was_child2 = FALSE;

    /* the sibling stays, the paned is collapsed away */
    other = (gtk_paned_get_child1 (paned) == GTK_WIDGET (screen))
              ? gtk_paned_get_child2 (paned)
              : gtk_paned_get_child1 (paned);

    /* keep a ref so the sibling survives the detaching below */
    g_object_ref (other);

    paned_parent = gtk_widget_get_parent (GTK_WIDGET (paned));

    /* hold a reference: detaching the paned from its parent may otherwise
     * drop its last reference and finalize it before we are done with it */
    g_object_ref (paned);

    if (paned_parent == GTK_WIDGET (panes))
      {
        /* detach the collapsed paned from the bin, then the sibling from it */
        gtk_container_remove (GTK_CONTAINER (panes), parent);
        gtk_container_remove (GTK_CONTAINER (paned), other);

        /* promote the sibling to the root */
        panes->root = other;
        gtk_container_add (GTK_CONTAINER (panes), other);
        gtk_widget_show (other);
      }
    else if (GTK_IS_PANED (paned_parent))
      {
        /* replace the collapsed paned by its surviving child */
        GtkPaned *grand = GTK_PANED (paned_parent);

        /* remember the collapsed paned's slot, then detach it and the sibling */
        paned_was_child2 = (gtk_paned_get_child2 (grand) == GTK_WIDGET (paned));

        gtk_container_remove (GTK_CONTAINER (grand), parent);
        gtk_container_remove (GTK_CONTAINER (paned), other);

        if (paned_was_child2)
          gtk_paned_pack2 (grand, other, TRUE, TRUE);
        else
          gtk_paned_pack1 (grand, other, TRUE, TRUE);
        gtk_widget_show (other);
      }
    else
      {
        g_warning ("TerminalPanes: unexpected parent widget for paned");
        g_object_unref (other);
        return FALSE;
      }

    /* destroy the screen first (removes it from the paned), then the paned.
     * Flag the removal so panes_screen_destroyed() leaves the tree alone. */
    panes->removing = TRUE;
    gtk_widget_destroy (GTK_WIDGET (screen));
    gtk_widget_destroy (GTK_WIDGET (paned));
    panes->removing = FALSE;

    /* The survivor was the size of one half of the collapsed paned, but it is
     * now the only child of @panes and must fill it. queue_resize() alone is
     * not enough: when @panes already had its allocation, GTK does not shrink
     * or grow the child unless the container is told to recompute. Force an
     * allocation pass from the current size of @panes. */
    {
      GtkAllocation pa;
      gint w, h;

      gtk_widget_get_allocation (GTK_WIDGET (panes), &pa);
      w = pa.width;
      h = pa.height;

      gtk_widget_queue_resize (GTK_WIDGET (panes));

      /* give GTK a synchronous allocation with the current size */
      if (w > 1 && h > 1)
        {
          GtkAllocation full = { 0, 0, w, h };

          gtk_widget_size_allocate (GTK_WIDGET (panes), &full);
        }
    }

    g_object_unref (paned);
    g_object_unref (other);

    /* fix the active screen if we just removed it (checked before destroy,
     * since @screen may have been finalized by now) */
    if (was_active)
      {
        TerminalScreen *new_active = NULL;

        if (TERMINAL_IS_SCREEN (other))
          new_active = TERMINAL_SCREEN (other);
        else if (GTK_IS_PANED (other))
          new_active = panes_tree_first_screen (other);

        panes->active = new_active;

        if (new_active != NULL)
          {
            panes_emit_active_changed (panes, new_active);
            terminal_screen_focus (new_active);
          }
      }
  }

  return TRUE;
}



TerminalScreen *
terminal_panes_get_active (TerminalPanes *panes)
{
  g_return_val_if_fail (TERMINAL_IS_PANES (panes), NULL);

  return panes->active;
}



void
terminal_panes_set_active (TerminalPanes *panes,
                           TerminalScreen *screen)
{
  g_return_if_fail (TERMINAL_IS_PANES (panes));

  if (screen != NULL && !panes_tree_has_screen (panes->root, screen))
    return;

  if (G_UNLIKELY (panes->active == screen))
    return;

  panes->active = screen;

  panes_emit_active_changed (panes, screen);
}



GSList *
terminal_panes_get_screens (TerminalPanes *panes)
{
  GSList *screens = NULL;

  g_return_val_if_fail (TERMINAL_IS_PANES (panes), NULL);

  panes_collect_screens (panes->root, &screens);

  return screens;
}



guint
terminal_panes_get_n_screens (TerminalPanes *panes)
{
  GSList *screens;
  guint n;

  g_return_val_if_fail (TERMINAL_IS_PANES (panes), 0);

  screens = terminal_panes_get_screens (panes);
  n = g_slist_length (screens);
  g_slist_free (screens);

  return n;
}



gboolean
terminal_panes_contains (TerminalPanes *panes,
                         TerminalScreen *screen)
{
  gboolean found;

  g_return_val_if_fail (TERMINAL_IS_PANES (panes), FALSE);
  g_return_val_if_fail (TERMINAL_IS_SCREEN (screen), FALSE);

  found = panes_tree_has_screen (panes->root, screen);

  return found;
}



/* translates the allocation of @widget into coordinates relative to @panes */
static void
panes_get_rect (TerminalPanes *panes,
                GtkWidget *widget,
                GtkAllocation *rect)
{
  GtkAllocation alloc;
  gint x = 0, y = 0;

  gtk_widget_get_allocation (widget, &alloc);

  /* translate to pane-relative coordinates; summing the parent allocations
   * would be wrong for GtkPaned children, whose allocation is not offset by
   * the divider position */
  if (!gtk_widget_translate_coordinates (widget, GTK_WIDGET (panes), 0, 0, &x, &y))
    {
      /* not anchored (e.g. not realized yet): fall back to the raw offset */
      x = alloc.x;
      y = alloc.y;
    }

  rect->x = x;
  rect->y = y;
  rect->width = alloc.width;
  rect->height = alloc.height;
}



static TerminalScreen *
panes_find_neighbor (TerminalPanes *panes,
                     TerminalScreen *current,
                     TerminalPanesDirection direction)
{
  GSList *screens;
  GSList *lp;
  GtkAllocation cur;
  gdouble cur_cx, cur_cy;
  TerminalScreen *best = NULL;
  gdouble best_primary = 0.0;
  gdouble best_secondary = 0.0;

  panes_get_rect (panes, GTK_WIDGET (current), &cur);
  cur_cx = cur.x + cur.width / 2.0;
  cur_cy = cur.y + cur.height / 2.0;

  screens = terminal_panes_get_screens (panes);

  for (lp = screens; lp != NULL; lp = lp->next)
    {
      TerminalScreen *screen = TERMINAL_SCREEN (lp->data);
      GtkAllocation rect;
      gdouble cx, cy;
      gboolean candidate = FALSE;
      gdouble primary = 0.0;
      gdouble secondary = 0.0;

      if (screen == current)
        continue;

      panes_get_rect (panes, GTK_WIDGET (screen), &rect);
      cx = rect.x + rect.width / 2.0;
      cy = rect.y + rect.height / 2.0;

      switch (direction)
        {
        case TERMINAL_PANES_DIRECTION_LEFT:
          if (rect.x + rect.width <= cur.x)
            {
              candidate = TRUE;
              primary = -(rect.x + rect.width);
              secondary = ABS (cy - cur_cy);
            }
          break;

        case TERMINAL_PANES_DIRECTION_RIGHT:
          if (rect.x >= cur.x + cur.width)
            {
              candidate = TRUE;
              primary = rect.x;
              secondary = ABS (cy - cur_cy);
            }
          break;

        case TERMINAL_PANES_DIRECTION_UP:
          if (rect.y + rect.height <= cur.y)
            {
              candidate = TRUE;
              primary = -(rect.y + rect.height);
              secondary = ABS (cx - cur_cx);
            }
          break;

        case TERMINAL_PANES_DIRECTION_DOWN:
          if (rect.y >= cur.y + cur.height)
            {
              candidate = TRUE;
              primary = rect.y;
              secondary = ABS (cx - cur_cx);
            }
          break;
        }

      if (candidate &&
          (best == NULL ||
           primary > best_primary ||
           (primary == best_primary && secondary < best_secondary)))
        {
          best = screen;
          best_primary = primary;
          best_secondary = secondary;
        }
    }

  g_slist_free (screens);

  return best;
}



static void
panes_focus_relative (TerminalPanes *panes,
                      gint offset)
{
  GSList *screens;
  GSList *lp;
  gint index = -1;
  gint current = -1;
  gint n;

  g_return_if_fail (TERMINAL_IS_PANES (panes));

  screens = terminal_panes_get_screens (panes);
  n = g_slist_length (screens);

  if (n < 2)
    {
      g_slist_free (screens);
      return;
    }

  for (lp = screens, index = 0; lp != NULL; lp = lp->next, index++)
    if (TERMINAL_SCREEN (lp->data) == panes->active)
      {
        current = index;
        break;
      }

  if (current < 0)
    current = 0;

  index = (current + offset) % n;
  if (index < 0)
    index += n;

  {
    TerminalScreen *target = TERMINAL_SCREEN (g_slist_nth_data (screens, index));

    terminal_panes_set_active (panes, target);
    terminal_screen_focus (target);
  }

  g_slist_free (screens);
}



void
terminal_panes_focus_next (TerminalPanes *panes)
{
  panes_focus_relative (panes, 1);
}



void
terminal_panes_focus_previous (TerminalPanes *panes)
{
  panes_focus_relative (panes, -1);
}



void
terminal_panes_focus_direction (TerminalPanes *panes,
                                TerminalPanesDirection direction)
{
  TerminalScreen *neighbor;

  g_return_if_fail (TERMINAL_IS_PANES (panes));

  if (panes->active == NULL)
    return;

  neighbor = panes_find_neighbor (panes, panes->active, direction);

  if (neighbor != NULL)
    {
      terminal_panes_set_active (panes, neighbor);
      terminal_screen_focus (neighbor);
    }
}