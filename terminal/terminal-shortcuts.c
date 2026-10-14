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

#include <xfconf/xfconf.h>

#include "terminal-shortcuts.h"

#define BASE "/commands/custom"

static XfconfChannel *
terminal_shortcuts_channel (void)
{
  return xfconf_channel_get (TERMINAL_SHORTCUT_CHANNEL);
}

/* Property names may not contain '/' and '\' unescaped, and a GtkAccel name
 * ("<Primary><Alt>t", "F1") is spelled exactly like the property name in the
 * xfce4-keyboard-shortcuts channel, so only the two separators need care. */
static gchar *
terminal_shortcuts_property (const gchar *binding)
{
  GString *property;
  const gchar *p;

  property = g_string_new (BASE "/");
  for (p = binding; *p != '\0'; p++)
    {
      if (*p == '/' || *p == '\\')
        g_string_append_c (property, '\\');
      g_string_append_c (property, *p);
    }

  return g_string_free (property, FALSE);
}

gboolean
terminal_shortcuts_is_xfce_session (void)
{
  const gchar *desktop = g_getenv ("XDG_CURRENT_DESKTOP");

  if (desktop == NULL || *desktop == '\0')
    desktop = g_getenv ("XDG_SESSION_DESKTOP");

  if (desktop == NULL)
    return FALSE;

  return g_strrstr (desktop, "XFCE") != NULL || g_strrstr (desktop, "Xfce") != NULL;
}



gchar *
terminal_shortcuts_get_drop_down_binding (void)
{
  XfconfChannel *channel;
  GHashTable *properties;
  GHashTableIter iter;
  gpointer key, value;
  gchar *binding = NULL;

  channel = terminal_shortcuts_channel ();
  if (G_UNLIKELY (channel == NULL))
    return NULL;

  properties = xfconf_channel_get_properties (channel, BASE);
  if (G_LIKELY (properties != NULL))
    {
      g_hash_table_iter_init (&iter, properties);
      while (binding == NULL && g_hash_table_iter_next (&iter, &key, &value))
        {
          if (G_VALUE_HOLDS_STRING (value)
              && g_strcmp0 (g_value_get_string (value), TERMINAL_SHORTCUT_COMMAND) == 0)
            {
              /* the property list hands out the names relative to the base,
               * while properties that came from the daemon keep the full path */
              const gchar *name = key;

              if (g_str_has_prefix (name, BASE "/"))
                name += strlen (BASE) + 1;
              else if (*name == '/')
                name++;
              binding = g_strdup (name);
            }
        }
      g_hash_table_unref (properties);
    }

  return binding;
}

/* The command a combination is already bound to, or NULL when it is free.
 * Callers use it to warn before overwriting someone else's shortcut. */
gchar *
terminal_shortcuts_get_conflict (const gchar *binding)
{
  XfconfChannel *channel;
  gchar *property;
  gchar *command;

  g_return_val_if_fail (binding != NULL && *binding != '\0', NULL);

  channel = terminal_shortcuts_channel ();
  if (G_UNLIKELY (channel == NULL))
    return NULL;

  property = terminal_shortcuts_property (binding);
  command = xfconf_channel_get_string (channel, property, NULL);
  g_free (property);

  if (g_strcmp0 (command, TERMINAL_SHORTCUT_COMMAND) == 0)
    g_clear_pointer (&command, g_free);

  return command;
}

gboolean
terminal_shortcuts_set_drop_down_binding (const gchar *binding)
{
  XfconfChannel *channel;
  gchar *property;
  gboolean result;

  g_return_val_if_fail (binding != NULL && *binding != '\0', FALSE);

  channel = terminal_shortcuts_channel ();
  if (G_UNLIKELY (channel == NULL))
    return FALSE;

  property = terminal_shortcuts_property (binding);
  result = xfconf_channel_set_string (channel, property, TERMINAL_SHORTCUT_COMMAND);
  if (result)
    {
      gchar *notify = g_strconcat (property, "/startup-notify", NULL);

      xfconf_channel_set_bool (channel, notify, TRUE);
      g_free (notify);
    }
  g_free (property);

  return result;
}

void
terminal_shortcuts_remove_drop_down_binding (void)
{
  XfconfChannel *channel;
  gchar *binding;
  gchar *property;

  binding = terminal_shortcuts_get_drop_down_binding ();
  if (binding == NULL)
    return;

  channel = terminal_shortcuts_channel ();
  if (G_LIKELY (channel != NULL))
    {
      property = terminal_shortcuts_property (binding);
      xfconf_channel_reset_property (channel, property, TRUE);
      g_free (property);
    }
  g_free (binding);
}
