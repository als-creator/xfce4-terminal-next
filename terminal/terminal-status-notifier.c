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

#include <gio/gio.h>
#include <string.h>
#include <unistd.h>

#include "terminal-status-notifier.h"

#define ITEM_PATH "/StatusNotifierItem"
#define MENU_PATH "/MenuBar"

struct _TerminalStatusNotifier
{
  GDBusConnection *connection;
  gchar *bus_name;
  gchar *id;
  gchar *icon_name;
  gchar *title;
  gchar *tooltip;
  gchar **menu_labels;
  /* a copy of the caller's items: the array itself may live on its stack */
  TerminalStatusNotifierMenuItem *menu_items;
  guint n_menu_items;
  TerminalStatusNotifierInfo info;
  guint owner_id;
  guint registration_id;
  guint menu_registration_id;
  guint watcher_id;
  guint menu_revision;
  gboolean name_acquired;
  gboolean watcher_present;
  gboolean registered;
};



static const gchar terminal_status_notifier_xml[] =
  "<node>"
  "  <interface name='org.kde.StatusNotifierItem'>"
  "    <property name='Category' type='s' access='read'/>"
  "    <property name='Id' type='s' access='read'/>"
  "    <property name='Title' type='s' access='read'/>"
  "    <property name='Status' type='s' access='read'/>"
  "    <property name='IconName' type='s' access='read'/>"
  "    <property name='IconThemePath' type='s' access='read'/>"
  "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
  "    <property name='Menu' type='o' access='read'/>"
  "    <property name='ItemIsMenu' type='b' access='read'/>"
  "    <signal name='NewIcon'/>"
  "    <signal name='NewTitle'/>"
  "    <signal name='NewToolTip'/>"
  "    <signal name='NewMenu'/>"
  "    <signal name='NewStatus'>"
  "      <arg name='status' type='s'/>"
  "    </signal>"
  "    <method name='Activate'>"
  "      <arg name='x' type='i'/>"
  "      <arg name='y' type='i'/>"
  "    </method>"
  "    <method name='SecondaryActivate'>"
  "      <arg name='x' type='i'/>"
  "      <arg name='y' type='i'/>"
  "    </method>"
  "    <method name='ContextMenu'>"
  "      <arg name='x' type='i'/>"
  "      <arg name='y' type='i'/>"
  "    </method>"
  "    <method name='Scroll'>"
  "      <arg name='delta' type='i'/>"
  "      <arg name='orientation' type='s'/>"
  "    </method>"
  "  </interface>"
  "  <interface name='com.canonical.dbusmenu'>"
  "    <property name='Version' type='u' access='read'/>"
  "    <property name='TextDirection' type='s' access='read'/>"
  "    <property name='Status' type='s' access='read'/>"
  "    <property name='IconThemePath' type='as' access='read'/>"
  "    <method name='GetLayout'>"
  "      <arg name='parentId' type='i' direction='in'/>"
  "      <arg name='recursionDepth' type='i' direction='in'/>"
  "      <arg name='propertyNames' type='as' direction='in'/>"
  "      <arg name='revision' type='u' direction='out'/>"
  "      <arg name='layout' type='(ia{sv}av)' direction='out'/>"
  "    </method>"
  "    <method name='GetGroupProperties'>"
  "      <arg name='ids' type='ai' direction='in'/>"
  "      <arg name='propertyNames' type='as' direction='in'/>"
  "      <arg name='properties' type='a(ia{sv})' direction='out'/>"
  "    </method>"
  "    <method name='GetProperty'>"
  "      <arg name='id' type='i' direction='in'/>"
  "      <arg name='name' type='s' direction='in'/>"
  "      <arg name='value' type='v' direction='out'/>"
  "    </method>"
  "    <method name='Event'>"
  "      <arg name='id' type='i' direction='in'/>"
  "      <arg name='eventId' type='s' direction='in'/>"
  "      <arg name='data' type='v' direction='in'/>"
  "      <arg name='timestamp' type='u' direction='in'/>"
  "    </method>"
  "    <method name='EventGroup'>"
  "      <arg name='events' type='a(isvu)' direction='in'/>"
  "      <arg name='idErrors' type='ai' direction='out'/>"
  "    </method>"
  "    <method name='AboutToShow'>"
  "      <arg name='ids' type='ai' direction='in'/>"
  "      <arg name='needUpdate' type='b' direction='out'/>"
  "    </method>"
  "    <method name='AboutToShowGroup'>"
  "      <arg name='ids' type='ai' direction='in'/>"
  "      <arg name='updatesNeeded' type='ai' direction='out'/>"
  "      <arg name='idErrors' type='ai' direction='out'/>"
  "    </method>"
  "    <signal name='ItemsPropertiesUpdated'>"
  "      <arg name='updatedProps' type='a(ia{sv})'/>"
  "      <arg name='removedProps' type='a(ia{sv})'/>"
  "    </signal>"
  "    <signal name='LayoutUpdated'>"
  "      <arg name='revision' type='u'/>"
  "      <arg name='parent' type='i'/>"
  "    </signal>"
  "    <signal name='ItemActivationRequested'>"
  "      <arg name='id' type='i'/>"
  "      <arg name='timestamp' type='u'/>"
  "    </signal>"
  "  </interface>"
  "</node>";



static void
terminal_status_notifier_emit (TerminalStatusNotifier *notifier,
                               const gchar *signal)
{
  if (notifier->registration_id != 0)
    g_dbus_connection_emit_signal (notifier->connection, NULL, ITEM_PATH,
                                   "org.kde.StatusNotifierItem", signal, NULL, NULL);
}



static void
terminal_status_notifier_register (TerminalStatusNotifier *notifier)
{
  GError *error = NULL;
  GVariant *reply;

  if (notifier->registered || notifier->bus_name == NULL)
    return;

  reply = g_dbus_connection_call_sync (notifier->connection,
                                       TERMINAL_SNI_WATCHER_NAME, TERMINAL_SNI_WATCHER_PATH,
                                       "org.kde.StatusNotifierWatcher",
                                       "RegisterStatusNotifierItem",
                                       g_variant_new ("(s)", notifier->bus_name),
                                       NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
  if (reply == NULL)
    {
      g_warning ("Failed to register the status notifier item: %s", error->message);
      g_clear_error (&error);
      return;
    }

  g_variant_unref (reply);
  notifier->registered = TRUE;
}



static void
terminal_status_notifier_name_acquired (GDBusConnection *connection,
                                        const gchar     *name,
                                        gpointer         user_data)
{
  TerminalStatusNotifier *notifier = user_data;

  g_free (notifier->bus_name);
  notifier->bus_name = g_strdup (name);
  notifier->name_acquired = TRUE;

  /* the watcher may have shown up before us, or only now */
  if (notifier->watcher_present)
    terminal_status_notifier_register (notifier);
}



static void
terminal_status_notifier_watcher_appeared (GDBusConnection *connection,
                                           const gchar     *name,
                                           const gchar     *name_appeared,
                                           gpointer         user_data)
{
  TerminalStatusNotifier *notifier = user_data;

  notifier->watcher_present = TRUE;
  terminal_status_notifier_register (notifier);
}



static void
terminal_status_notifier_watcher_vanished (GDBusConnection *connection,
                                           const gchar     *name,
                                           gpointer         user_data)
{
  TerminalStatusNotifier *notifier = user_data;

  /* the item is gone from the tray as well: a watcher that went away does not
   * remember the items it had, so a new one has to register us again */
  notifier->watcher_present = FALSE;
  notifier->registered = FALSE;
}



static GVariant *
terminal_status_notifier_get_property (GDBusConnection  *connection,
                                       const gchar      *sender,
                                       const gchar      *object_path,
                                       const gchar      *interface_name,
                                       const gchar      *property_name,
                                       GError          **error,
                                       gpointer          user_data)
{
  TerminalStatusNotifier *notifier = user_data;
  GVariant *value = NULL;

  if (g_strcmp0 (property_name, "Category") == 0)
    value = g_variant_new_string ("ApplicationStatus");
  else if (g_strcmp0 (property_name, "Id") == 0)
    value = g_variant_new_string (notifier->id != NULL ? notifier->id : "");
  else if (g_strcmp0 (property_name, "Title") == 0)
    value = g_variant_new_string (notifier->title != NULL ? notifier->title : "");
  else if (g_strcmp0 (property_name, "Status") == 0)
    value = g_variant_new_string ("Active");
  else if (g_strcmp0 (property_name, "IconName") == 0)
    value = g_variant_new_string (notifier->icon_name != NULL ? notifier->icon_name : "");
  else if (g_strcmp0 (property_name, "IconThemePath") == 0)
    value = g_variant_new_string ("");
  else if (g_strcmp0 (property_name, "ToolTip") == 0)
    {
      /* The specification type is (sa(iiay)ss): the icon name, the icon data
       * (empty: the hosts take the icon from IconName), the tooltip and the
       * title, which is the text the tooltip shows. */
      GVariantBuilder icons;

      /* the icon array stays empty: the host takes the icon from IconName */
      g_variant_builder_init (&icons, g_variant_type_checked_ ("a(iiay)"));
      value = g_variant_new ("(sa(iiay)ss)", "", &icons,
                             notifier->tooltip != NULL ? notifier->tooltip : "",
                             notifier->title != NULL ? notifier->title : "");
    }
  else if (g_strcmp0 (property_name, "Menu") == 0)
    value = g_variant_new_object_path (MENU_PATH);
  else if (g_strcmp0 (property_name, "ItemIsMenu") == 0)
    value = g_variant_new_boolean (FALSE);

  if (value == NULL)
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                   "Unknown property '%s'", property_name);
      return NULL;
    }

  return value;
}



static void
terminal_status_notifier_method_call (GDBusConnection       *connection,
                                      const gchar           *sender,
                                      const gchar           *object_path,
                                      const gchar           *interface_name,
                                      const gchar           *method_name,
                                      GVariant              *parameters,
                                      GDBusMethodInvocation *invocation,
                                      gpointer               user_data)
{
  TerminalStatusNotifier *notifier = user_data;

  if (g_getenv ("TERMINAL_DEBUG_MENU") != NULL)
    g_printerr ("item call: %s(%s)\n", method_name,
                parameters != NULL ? g_variant_print (parameters, TRUE) : "");

  if (g_strcmp0 (method_name, "Activate") == 0
      || g_strcmp0 (method_name, "SecondaryActivate") == 0)
    {
      if (notifier->info.activate != NULL)
        notifier->info.activate (notifier->info.user_data);
    }
  else if (g_strcmp0 (method_name, "ContextMenu") == 0)
    {
      if (notifier->info.context_menu != NULL)
        notifier->info.context_menu (notifier->info.user_data);
    }
  else if (g_strcmp0 (method_name, "Scroll") == 0)
    {
      /* nothing to scroll */
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method '%s'", method_name);
      return;
    }

  g_dbus_method_invocation_return_value (invocation, NULL);
}





/* ---- com.canonical.dbusmenu -------------------------------------------------
 *
 * A flat menu: the host builds it from GetLayout and reports a click with
 * Event().  A menu drawn by the host is the one thing that needs no X11 and
 * no position guess, so it is also the only one that works under Wayland. */

static void
terminal_status_notifier_menu_item_properties (TerminalStatusNotifier *notifier,
                                               gint id,
                                               GVariantBuilder *properties);

static GVariant *
terminal_status_notifier_menu_layout_item (TerminalStatusNotifier *notifier,
                                           guint index)
{
  GVariantBuilder properties;
  GVariantBuilder children;

  g_variant_builder_init (&properties, g_variant_type_checked_ ("a{sv}"));
  terminal_status_notifier_menu_item_properties (notifier, (gint) index + 1, &properties);

  g_variant_builder_init (&children, g_variant_type_checked_ ("av"));

  /* (id, properties, children), the ids are 1..n and 0 is the root */
  return g_variant_new ("(ia{sv}av)", (gint) index + 1, &properties, &children);
}



static GVariant *
terminal_status_notifier_menu_layout (TerminalStatusNotifier *notifier,
                                      gint parent_id,
                                      gint recursion_depth)
{
  GVariantBuilder properties;
  GVariantBuilder children;
  guint i;

  g_variant_builder_init (&properties, g_variant_type_checked_ ("a{sv}"));
  g_variant_builder_init (&children, g_variant_type_checked_ ("av"));

  /* the items are children of the root (0); anything else has no children */
  if (parent_id == 0 && recursion_depth != 0)
    {
      for (i = 0; i < notifier->n_menu_items; i++)
        /* the children of an "av" array are variants, not bare layouts */
        g_variant_builder_add_value (&children,
                                     g_variant_new_variant (
                                       terminal_status_notifier_menu_layout_item (notifier, i)));
    }

  return g_variant_new ("(ia{sv}av)", parent_id, &properties, &children);
}



/* The properties of a menu item: a host reads them with GetLayout and then
 * asks again with GetGroupProperties, so both have to answer the same. */
static void
terminal_status_notifier_menu_item_properties (TerminalStatusNotifier *notifier,
                                               gint id,
                                               GVariantBuilder *properties)
{
  g_variant_builder_add (properties, "{sv}", "enabled", g_variant_new_boolean (TRUE));
  g_variant_builder_add (properties, "{sv}", "visible", g_variant_new_boolean (TRUE));
  g_variant_builder_add (properties, "{sv}", "type", g_variant_new_string ("standard"));

  if (id >= 1 && (guint) id <= notifier->n_menu_items)
    g_variant_builder_add (properties, "{sv}", "label",
                           g_variant_new_string (notifier->menu_labels[id - 1]));
}



static GVariant *
terminal_status_notifier_menu_get_property (GDBusConnection  *connection,
                                            const gchar      *sender,
                                            const gchar      *object_path,
                                            const gchar      *interface_name,
                                            const gchar      *property_name,
                                            GError          **error,
                                            gpointer          user_data)
{
  if (g_strcmp0 (property_name, "Version") == 0)
    return g_variant_new_uint32 (3);

  if (g_strcmp0 (property_name, "TextDirection") == 0)
    return g_variant_new_string ("ltr");

  if (g_strcmp0 (property_name, "Status") == 0)
    return g_variant_new_string ("normal");

  if (g_strcmp0 (property_name, "IconThemePath") == 0)
    {
      GVariantBuilder paths;

      /* An empty as has to be built: "@as" asks for a ready made variant and
       * aborts the process on NULL.  A tray host reads this property when it
       * registers the item, so the failure happened right at startup. */
      g_variant_builder_init (&paths, g_variant_type_checked_ ("as"));
      return g_variant_new ("as", &paths);
    }

  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
               "Unknown property '%s'", property_name);

  return NULL;
}



static void
terminal_status_notifier_menu_method_call (GDBusConnection       *connection,
                                           const gchar           *sender,
                                           const gchar           *object_path,
                                           const gchar           *interface_name,
                                           const gchar           *method_name,
                                           GVariant              *parameters,
                                           GDBusMethodInvocation *invocation,
                                           gpointer               user_data)
{
  TerminalStatusNotifier *notifier = user_data;

  /* What a tray host really sends is worth seeing: the modules differ in
   * whether a click on a menu item arrives as dbusmenu Event or as the plain
   * ContextMenu of the item.  The log is written only when asked for, so that
   * the panel can be started with TERMINAL_DEBUG_MENU=1 and the calls read off
   * the terminal. */
  if (g_getenv ("TERMINAL_DEBUG_MENU") != NULL)
    g_printerr ("menu call: %s(%s)\n", method_name,
                parameters != NULL ? g_variant_print (parameters, TRUE) : "");

  if (g_strcmp0 (method_name, "GetLayout") == 0)
    {
      gint parent_id, depth;

      g_variant_get (parameters, "(ii^as)", &parent_id, &depth, NULL);
      notifier->menu_revision++;

      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("(u@(ia{sv}av))",
                                                            notifier->menu_revision,
                                                            terminal_status_notifier_menu_layout (
                                                              notifier, parent_id, depth)));
    }
  else if (g_strcmp0 (method_name, "GetGroupProperties") == 0)
    {
      GVariantBuilder items;
      GVariantBuilder properties;
      GVariant *ids_variant;
      gsize n_ids, i;

      /* the signature is (aias): the ids come as a variant, the requested
       * property names are ignored because every item gets the full set */
      ids_variant = g_variant_get_child_value (parameters, 0);
      n_ids = g_variant_n_children (ids_variant);

      g_variant_builder_init (&items, g_variant_type_checked_ ("a(ia{sv})"));
      for (i = 0; i < n_ids; i++)
        {
          GVariant *item;
          gint id = g_variant_get_int32 (g_variant_get_child_value (ids_variant, i));

          g_variant_builder_init (&properties, g_variant_type_checked_ ("a{sv}"));
          terminal_status_notifier_menu_item_properties (notifier, id, &properties);
          item = g_variant_new ("(ia{sv})", id, &properties);
          g_variant_builder_add_value (&items, item);
        }
      g_variant_unref (ids_variant);

      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a(ia{sv}))", &items));
    }
  else if (g_strcmp0 (method_name, "GetProperty") == 0)
    {
      gint id = 0;
      const gchar *name = NULL;

      g_variant_get (parameters, "(i&s)", &id, &name);

      if (id >= 1 && (gint) id <= (gint) notifier->n_menu_items && g_strcmp0 (name, "label") == 0)
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)",
                                                                g_variant_new_string (
                                                                  notifier->menu_labels[id - 1])));
        }
      else if (g_strcmp0 (name, "type") == 0)
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)",
                                                                g_variant_new_string ("standard")));
        }
      else if (g_strcmp0 (name, "enabled") == 0 || g_strcmp0 (name, "visible") == 0)
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)",
                                                                g_variant_new_boolean (TRUE)));
        }
      else
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)", g_variant_new_string ("")));
        }
    }
  else if (g_strcmp0 (method_name, "Event") == 0)
    {
      gint id = 0;
      const gchar *event_id = NULL;

      /* the signature is (isvu): id, event, data, timestamp */
      g_variant_get (parameters, "(i&s@vu)", &id, &event_id, NULL, NULL);

      if (g_strcmp0 (event_id, "clicked") == 0
          && id >= 1 && (gint) id <= (gint) notifier->n_menu_items)
        {
          if (notifier->info.menu_items[id - 1].activate != NULL)
            notifier->info.menu_items[id - 1].activate (notifier->info.user_data);
        }

      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_strcmp0 (method_name, "EventGroup") == 0)
    {
      GVariantBuilder id_errors;

      /* An empty a(isvu) came in and every event was delivered, so no id
       * failed.  The array has to be built, not passed as NULL: "@ai" would
       * ask for a ready made variant and abort the process on NULL. */
      g_variant_builder_init (&id_errors, g_variant_type_checked_ ("ai"));
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ai)", &id_errors));
    }
  else if (g_strcmp0 (method_name, "AboutToShow") == 0)
    {
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", FALSE));
    }
  else if (g_strcmp0 (method_name, "AboutToShowGroup") == 0)
    {
      GVariantBuilder updates;
      GVariantBuilder id_errors;

      /* the signature of the reply is (ai, ai), two arrays, not "@ai" */
      g_variant_builder_init (&updates, g_variant_type_checked_ ("ai"));
      g_variant_builder_init (&id_errors, g_variant_type_checked_ ("ai"));
      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("(aiai)", &updates, &id_errors));
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method '%s'", method_name);
    }
}



static const GDBusInterfaceVTable terminal_status_notifier_menu_vtable = {
  .method_call = terminal_status_notifier_menu_method_call,
  .get_property = terminal_status_notifier_menu_get_property,
  .set_property = NULL,
};

static const GDBusInterfaceVTable terminal_status_notifier_vtable = {
  .method_call = terminal_status_notifier_method_call,
  .get_property = terminal_status_notifier_get_property,
  .set_property = NULL,
};



gboolean
terminal_status_notifier_is_supported (void)
{
  GDBusConnection *connection;
  GError *error = NULL;
  GVariant *reply;
  gboolean supported = FALSE;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (connection == NULL)
    {
      g_clear_error (&error);
      return FALSE;
    }

  reply = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                       "NameHasOwner",
                                       g_variant_new ("(s)", TERMINAL_SNI_WATCHER_NAME),
                                       g_variant_type_checked_ ("(b)"), G_DBUS_CALL_FLAGS_NONE,
                                       1000, NULL, &error);
  if (reply != NULL)
    {
      g_variant_get (reply, "(b)", &supported);
      g_variant_unref (reply);
    }
  else
    {
      g_clear_error (&error);
    }

  g_object_unref (connection);

  return supported;
}



TerminalStatusNotifier *
terminal_status_notifier_new (const TerminalStatusNotifierInfo *info)
{
  TerminalStatusNotifier *notifier;
  GDBusNodeInfo *info_node;
  GError *error = NULL;
  static guint counter = 0;
  guint i;

  g_return_val_if_fail (info != NULL, NULL);

  notifier = g_new0 (TerminalStatusNotifier, 1);
  notifier->info = *info;
  if (info->n_menu_items > 0 && info->menu_items != NULL)
    {
      notifier->info.menu_items = g_new (TerminalStatusNotifierMenuItem, info->n_menu_items);
      memcpy ((gpointer) notifier->info.menu_items, info->menu_items,
              info->n_menu_items * sizeof (TerminalStatusNotifierMenuItem));
    }
  notifier->id = g_strdup (info->id);
  notifier->icon_name = g_strdup (info->icon_name);
  notifier->title = g_strdup (info->title);
  notifier->tooltip = g_strdup (info->tooltip);
  notifier->n_menu_items = info->n_menu_items;
  notifier->menu_labels = g_new0 (gchar *, info->n_menu_items + 1);
  for (i = 0; i < info->n_menu_items; i++)
    notifier->menu_labels[i] = g_strdup (info->menu_items[i].label);

  notifier->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (notifier->connection == NULL)
    {
      g_warning ("Failed to connect to the session bus: %s", error->message);
      g_clear_error (&error);
      terminal_status_notifier_free (notifier);
      return NULL;
    }

  info_node = g_dbus_node_info_new_for_xml (terminal_status_notifier_xml, &error);
  if (info_node == NULL)
    {
      g_warning ("Failed to parse the status notifier description: %s", error->message);
      g_clear_error (&error);
      terminal_status_notifier_free (notifier);
      return NULL;
    }

  notifier->registration_id = g_dbus_connection_register_object (notifier->connection, ITEM_PATH,
                                                                 info_node->interfaces[0],
                                                                 &terminal_status_notifier_vtable,
                                                                 notifier, NULL, &error);
  if (notifier->registration_id == 0)
    {
      g_warning ("Failed to export the status notifier item: %s", error->message);
      g_clear_error (&error);
      g_dbus_node_info_unref (info_node);
      terminal_status_notifier_free (notifier);
      return NULL;
    }

  /* the menu is exported only when there is something in it */
  if (info->n_menu_items > 0)
    {
      notifier->menu_registration_id =
        g_dbus_connection_register_object (notifier->connection, MENU_PATH,
                                           info_node->interfaces[1],
                                           &terminal_status_notifier_menu_vtable,
                                           notifier, NULL, &error);
      if (notifier->menu_registration_id == 0)
        {
          g_warning ("Failed to export the status notifier menu: %s", error->message);
          g_clear_error (&error);
        }
    }

  g_dbus_node_info_unref (info_node);

  notifier->bus_name = g_strdup_printf ("org.kde.StatusNotifierItem-%lu-%u",
                                        (gulong) getpid (), ++counter);
  notifier->owner_id = g_bus_own_name_on_connection (notifier->connection, notifier->bus_name,
                                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                                      terminal_status_notifier_name_acquired,
                                                      NULL, notifier, NULL);

  /* the watcher may start after us (a panel plugin, say), so keep watching it */
  notifier->watcher_id = g_bus_watch_name_on_connection (notifier->connection,
                                                         TERMINAL_SNI_WATCHER_NAME,
                                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                          terminal_status_notifier_watcher_appeared,
                                                          terminal_status_notifier_watcher_vanished,
                                                          notifier, NULL);

  return notifier;
}



void
terminal_status_notifier_free (TerminalStatusNotifier *notifier)
{
  if (notifier == NULL)
    return;

  if (notifier->watcher_id != 0)
    g_bus_unwatch_name (notifier->watcher_id);

  if (notifier->owner_id != 0)
    g_bus_unown_name (notifier->owner_id);

  if (notifier->menu_registration_id != 0)
    g_dbus_connection_unregister_object (notifier->connection, notifier->menu_registration_id);

  if (notifier->registration_id != 0)
    g_dbus_connection_unregister_object (notifier->connection, notifier->registration_id);

  if (notifier->connection != NULL)
    g_object_unref (notifier->connection);

  g_strfreev (notifier->menu_labels);
  g_free ((gpointer) notifier->info.menu_items);
  g_free (notifier->bus_name);
  g_free (notifier->id);
  g_free (notifier->icon_name);
  g_free (notifier->title);
  g_free (notifier->tooltip);
  g_free (notifier);
}



void
terminal_status_notifier_set_icon (TerminalStatusNotifier *notifier,
                                   const gchar *icon_name)
{
  g_return_if_fail (notifier != NULL);

  g_free (notifier->icon_name);
  notifier->icon_name = g_strdup (icon_name);
  terminal_status_notifier_emit (notifier, "NewIcon");
}



void
terminal_status_notifier_set_tooltip (TerminalStatusNotifier *notifier,
                                      const gchar *tooltip)
{
  g_return_if_fail (notifier != NULL);

  g_free (notifier->tooltip);
  notifier->tooltip = g_strdup (tooltip);
  terminal_status_notifier_emit (notifier, "NewToolTip");
}


void
terminal_status_notifier_menu_set_label (TerminalStatusNotifier *notifier,
                                         guint item_index,
                                         const gchar *label)
{
  GVariantBuilder updated;
  GVariantBuilder removed;
  GVariantBuilder properties;

  g_return_if_fail (notifier != NULL);
  g_return_if_fail (item_index < notifier->n_menu_items);

  g_free (notifier->menu_labels[item_index]);
  notifier->menu_labels[item_index] = g_strdup (label);

  /* tell the host about the new text, it caches the layout */
  g_variant_builder_init (&updated, g_variant_type_checked_ ("a(ia{sv})"));
  g_variant_builder_init (&removed, g_variant_type_checked_ ("a(ia{sv})"));
  g_variant_builder_init (&properties, g_variant_type_checked_ ("a{sv}"));
  g_variant_builder_add (&properties, "{sv}", "label",
                         g_variant_new_string (notifier->menu_labels[item_index]));
  {
    GVariant *item = g_variant_new ("(ia{sv})", (gint) item_index + 1, &properties);

    g_variant_builder_add_value (&updated, item);
  }

  g_dbus_connection_emit_signal (notifier->connection, NULL, MENU_PATH,
                                 "com.canonical.dbusmenu", "ItemsPropertiesUpdated",
                                 g_variant_new ("(a(ia{sv})a(ia{sv}))", &updated, &removed),
                                 NULL);
}
