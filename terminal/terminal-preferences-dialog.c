/*-
 * Copyright (c) 2004-2007 os-cillation e.K.
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
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

#include <libxfce4kbd-private/xfce-shortcuts-editor.h>

#include "terminal-encoding-action.h"
#include "terminal-enum-types.h"
#include "terminal-preferences-dialog.h"
#include "terminal-private.h"
#include "terminal-shortcuts.h"
#include "terminal-util.h"
#include "terminal-widget.h"
#include "terminal-window.h"



static void
terminal_preferences_dialog_finalize (GObject *object);
static void
terminal_preferences_dialog_response (GtkDialog *dialog,
                                      gint response);
/* number of check boxes built for the page that is going up, compared with
 * the page that came out: see terminal_preferences_dialog_check_buttons () */
static gint check_buttons = 0;

static void
terminal_preferences_dialog_check_layout (GtkWidget *widget);
static void
terminal_preferences_dialog_check_layout_on_map (GtkWidget *widget,
                                                 gpointer   data);
static GtkWidget *
terminal_preferences_dialog_check_button_new (const gchar *mnemonic);
static void
terminal_preferences_dialog_check_buttons (GtkWidget *page,
                                            gint        built);
static PangoAttrList *
terminal_pango_attr_list_small (void);
static void
terminal_preferences_dialog_new_section (GtkWidget **frame,
                                         GtkWidget **vbox,
                                         GtkWidget **grid,
                                         GtkWidget **label,
                                         gint *row,
                                         const gchar *header);
static void
terminal_preferences_dialog_shortcut_update (TerminalPreferencesDialog *dialog);
static void
terminal_preferences_dialog_shortcut_toggled (GtkToggleButton *button,
                                              gpointer         user_data);
static void
terminal_preferences_dialog_shortcut_change (GtkButton *button,
                                             gpointer   user_data);
static void
terminal_preferences_dialog_copy_command (GtkButton *button,
                                          gpointer   user_data);
static void
terminal_preferences_dialog_background_notify (TerminalPreferencesDialog *dialog);
static void
terminal_preferences_dialog_background_set (TerminalPreferencesDialog *dialog,
                                            GtkWidget *widget);
static void
terminal_preferences_dialog_geometry_notify (TerminalPreferencesDialog *dialog);
static void
terminal_preferences_dialog_geometry_changed (TerminalPreferencesDialog *dialog,
                                              GtkWidget *widget);
static void
terminal_preferences_dialog_palette_notify (TerminalPreferencesDialog *dialog);
static void
terminal_preferences_dialog_palette_changed (TerminalPreferencesDialog *dialog,
                                             GtkWidget *widget);
static void
terminal_preferences_dialog_presets_changed (TerminalPreferencesDialog *dialog,
                                             GtkWidget *widget);
static void
terminal_preferences_dialog_presets_load (TerminalPreferencesDialog *dialog,
                                          GtkWidget *widget);
static void
terminal_preferences_dialog_reset_cell_scale (TerminalPreferencesDialog *dialog,
                                              GtkWidget *widget);
static void
terminal_preferences_dialog_reset_compatibility_options (TerminalPreferencesDialog *dialog,
                                                         GtkWidget *widget);
static void
terminal_preferences_dialog_reset_double_click_select (TerminalPreferencesDialog *dialog,
                                                       GtkWidget *widget);
static void
terminal_preferences_dialog_encoding_changed (TerminalPreferencesDialog *dialog,
                                              GtkWidget *widget);
static void
terminal_gtk_label_set_a11y_relation (GtkLabel *label,
                                      GtkWidget *widget);
static gboolean
monospace_filter (const PangoFontFamily *family,
                  const PangoFontFace *face,
                  gpointer data);



enum
{
  PRESET_COLUMN_TITLE,
  PRESET_COLUMN_IS_SEPARATOR,
  PRESET_COLUMN_PATH,
  N_PRESET_COLUMNS
};

enum
{
  GEOMETRY_COLUMN,
  GEOMETRY_ROW,
  N_GEOMETRY_BUTTONS
};

enum
{
  BACKGROUND_COMBO_SHOW_IMAGE_OPTIONS = 1,
  BACKGROUND_COMBO_SHOW_OPACITY_OPTIONS
};

enum
{
  N_PALETTE_BUTTONS = 16,
};

#if !LIBXFCE4UI_CHECK_VERSION(4, 21, 8)
struct _TerminalPreferencesDialogClass
{
  XfceTitledDialogClass __parent__;
};
#endif

struct _TerminalPreferencesDialog
{
  XfceTitledDialog __parent__;
  TerminalPreferences *preferences;

  GtkColorButton *palette_button[N_PALETTE_BUTTONS];
  GtkSpinButton *geometry_button[N_GEOMETRY_BUTTONS];
  GtkLabel *dropdown_label;
  GtkBox *dropdown_vbox;
  GtkNotebook *dropdown_notebook;
  GtkWidget *file_chooser;
  GtkWidget *shortcut_check;
  GtkWidget *shortcut_key_label;
  GtkWidget *shortcut_change_button;
  gint n_presets;

  gulong bg_image_signal_id;
  gulong palette_notify_signal_id;
  gulong palette_set_signal_id[N_PALETTE_BUTTONS];
  gulong geometry_notify_signal_id;
  gulong geometry_set_signal_id[N_GEOMETRY_BUTTONS];
};



G_DEFINE_TYPE (TerminalPreferencesDialog, terminal_preferences_dialog, XFCE_TYPE_TITLED_DIALOG)



static void
terminal_preferences_dialog_class_init (TerminalPreferencesDialogClass *klass)
{
  GtkDialogClass *gtkdialog_class;
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = terminal_preferences_dialog_finalize;

  gtkdialog_class = GTK_DIALOG_CLASS (klass);
  gtkdialog_class->response = terminal_preferences_dialog_response;
}



static gboolean
transform_combo_show_image_options (GBinding *binding,
                                    const GValue *src_value,
                                    GValue *dst_value,
                                    gpointer user_data)
{
  gint active_id;
  gboolean show = FALSE;

  active_id = g_value_get_int (src_value);
  if (G_UNLIKELY (active_id == BACKGROUND_COMBO_SHOW_IMAGE_OPTIONS))
    show = TRUE;

  g_value_set_boolean (dst_value, show);
  return TRUE;
}


static gboolean
transform_combo_show_opacity_options (GBinding *binding,
                                      const GValue *src_value,
                                      GValue *dst_value,
                                      gpointer user_data)
{
  gint active_id;
  gboolean show = FALSE;

  active_id = g_value_get_int (src_value);
  if (G_UNLIKELY (active_id == BACKGROUND_COMBO_SHOW_OPACITY_OPTIONS))
    show = TRUE;

  g_value_set_boolean (dst_value, show);
  return TRUE;
}



static void
terminal_preferences_dialog_init (TerminalPreferencesDialog *dialog)
{
  GtkFileFilter *filter;
  GtkTreeModel *model;
  GtkTreeIter current_iter;
  GtkWidget *notebook;
  GtkWidget *button;
  GtkWidget *image;
  GtkWidget *frame;
  GtkWidget *label;
  GtkWidget *vbox = NULL;
  GtkWidget *box;
  GtkWidget *infobar;
  GtkWidget *grid;
  GtkWidget *background_grid;
  GtkWidget *entry;
  GtkWidget *combo;
  gchar *current;
  gint row = 0;
  const gchar *text;

  /* grab a reference on the preferences */
  dialog->preferences = terminal_preferences_get ();

  /* configure the dialog properties */
  gtk_window_set_icon_name (GTK_WINDOW (dialog), "org.xfce.terminal");
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Terminal Preferences"));

#if !LIBXFCE4UI_CHECK_VERSION(4, 19, 3)
  xfce_titled_dialog_create_action_area (XFCE_TITLED_DIALOG (dialog));
#endif

  /* add the "Close" button */
  button = gtk_button_new_with_mnemonic (_("_Close"));
  image = gtk_image_new_from_icon_name ("window-close", GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button), image);
  xfce_titled_dialog_add_action_widget (XFCE_TITLED_DIALOG (dialog), button, GTK_RESPONSE_CLOSE);

  gtk_widget_show (button);

  /* add the "Help" button */
  button = gtk_button_new_with_mnemonic (_("_Help"));
  image = gtk_image_new_from_icon_name ("help-browser", GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button), image);
  xfce_titled_dialog_add_action_widget (XFCE_TITLED_DIALOG (dialog), button, GTK_RESPONSE_HELP);

  gtk_widget_show (button);

  notebook = gtk_notebook_new ();
  gtk_container_set_border_width (GTK_CONTAINER (notebook), 6);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), notebook, TRUE, TRUE, 0);
  gtk_widget_show (notebook);



  /*
   * General
   */
  label = gtk_label_new (_("General"));
  if (vbox != NULL)
    {
      terminal_preferences_dialog_check_buttons (vbox, check_buttons);
      check_buttons = 0;
    }

  vbox = g_object_new (GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "border-width", 12, "spacing", 18, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_widget_show (label);
  gtk_widget_show (vbox);


  /* section: Title */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Title"));

  label = gtk_label_new_with_mnemonic (_("_Initial title:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  entry = gtk_entry_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "title-initial",
                          G_OBJECT (entry), "text",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 1, 1);
  gtk_widget_show (entry);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Dynamically-set title:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Replaces initial title"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Goes before initial title"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Goes after initial title"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Isn't displayed"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "title-mode",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);


  /* section: Command */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Command"));

  button = terminal_preferences_dialog_check_button_new (_("_Run command as login shell"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "command-login-shell",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Select this option to force Terminal to run your shell"
                                         " as a login shell when you open new terminals. See the"
                                         " documentation of your shell for details about differences"
                                         " between running it as interactive shell and running it as"
                                         " login shell."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Run a custom command instead of my shell"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "run-custom-command",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Select this option to force Terminal to run a custom command"
                                         " instead of your shell when you open new terminals"));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Custom command:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  /* it should not be reactive to input if run-custom-command is disabled */
  g_object_bind_property (G_OBJECT (dialog->preferences), "run-custom-command",
                          G_OBJECT (label), "sensitive",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_show (label);

  entry = gtk_entry_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "custom-command",
                          G_OBJECT (entry), "text",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  /* it should not be reactive to input if run-custom-command is disabled */
  g_object_bind_property (G_OBJECT (dialog->preferences), "run-custom-command",
                          G_OBJECT (entry), "sensitive",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (entry, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 1, 1);
  gtk_widget_show (entry);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Working directory:"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "use-default-working-dir",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Select this option to make new terminals (tabs or windows) use custom"
                                          " working directory. Otherwise, current working directory will be used."));
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  entry = gtk_entry_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "default-working-dir",
                          G_OBJECT (entry), "text",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  /* it should not be reactive to input if use-default-working-dir is disabled */
  g_object_bind_property (G_OBJECT (dialog->preferences), "use-default-working-dir",
                          G_OBJECT (entry), "sensitive",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (entry, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 1, 1);
  gtk_widget_show (entry);


  /* section: Scrolling */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Scrolling"));

  button = terminal_preferences_dialog_check_button_new (_("_Scroll on output"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "scrolling-on-output",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("This option controls whether the terminal will scroll down automatically"
                                         " whenever new output is generated by the commands running inside the terminal."));
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  button = terminal_preferences_dialog_check_button_new (_("_Scroll on keystroke"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "scrolling-on-keystroke",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enables you to press any key on the keyboard to scroll down the terminal"
                                         " to the command prompt."));
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  if (vte_get_major_version () > 0
      || (vte_get_major_version () == 0 && vte_get_minor_version () > 69)
      || (vte_get_major_version () == 0 && vte_get_minor_version () == 69 && vte_get_micro_version () >= 90))
    {
      button = terminal_preferences_dialog_check_button_new (_("_Enable kinetic scrolling"));
      g_object_bind_property (G_OBJECT (dialog->preferences), "kinetic-scrolling",
                              G_OBJECT (button), "active",
                              G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
      gtk_widget_set_tooltip_text (button, _("This controls whether scrolling \"coasts\" to a stop when releasing a touchpad or touchscreen."));
      gtk_widget_set_hexpand (button, TRUE);
      gtk_grid_attach (GTK_GRID (grid), button, 2, row, 1, 1);
      gtk_widget_show (button);
    }

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Scrollbar is:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Disabled"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("On the left side"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("On the right side"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "scrolling-bar",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 2, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Scrollback:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_spin_button_new_with_range (0u, 1024u * 1024u, 1);
  g_object_bind_property (G_OBJECT (dialog->preferences), "scrolling-lines",
                          G_OBJECT (button), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "scrolling-unlimited",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  button = terminal_preferences_dialog_check_button_new (_("_Unlimited Scrollback"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "scrolling-unlimited",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("This controls whether the terminal will have no limits on scrollback."));
  gtk_grid_attach (GTK_GRID (grid), button, 2, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Enable overlay scrolling (Requires restart)"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "overlay-scrolling",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("This controls whether scrollbar is drawn as an overlay (auto-hide) or not."));
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 3, 1);
  gtk_widget_show (button);


  /* section: Cursor */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Cursor"));

  label = gtk_label_new_with_mnemonic (_("Cursor shape:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Block"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("I-Beam"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Underline"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-cursor-shape",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  button = terminal_preferences_dialog_check_button_new (_("Cursor blinks"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-cursor-blinks",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("This option controls whether the cursor will be blinking or not."));
  gtk_widget_set_halign (button, GTK_ALIGN_END);
  gtk_grid_attach (GTK_GRID (grid), button, 2, row, 1, 1);
  gtk_widget_show (button);


  /* section: Clipboard */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Clipboard"));

  button = terminal_preferences_dialog_check_button_new (_("Automatically copy selection to clipboard"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-copy-on-select",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  button = terminal_preferences_dialog_check_button_new (_("Show unsafe paste dialog"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-show-unsafe-paste-dialog",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Show a dialog that allows one to edit text that is considered unsafe for pasting"));
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);



  /* section: tmux launch options */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("tmux Launch Options"));
  label = gtk_label_new_with_mnemonic (_("Command to _start tmux:"));
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  entry = gtk_entry_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-tmux-command",
                          G_OBJECT (entry), "text",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (entry, _("Command run in the pane by Ctrl+Shift+M and the"
                                        " \"Start tmux Session\" menu entry.\n"
                                        "The default attaches to an existing session, which is"
                                        " what makes it survive an SSH disconnect."));
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 2, 1);
  gtk_widget_show (entry);



  /* section: Session */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Session"));

  button = terminal_preferences_dialog_check_button_new (_("Remember the _pane layout of tabs"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-panes-layouts-enabled",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Remember how the tabs are split into panes, together with the"
                                         " directory of every pane, and put them back when the terminal"
                                         " starts again.\n"
                                         "Turn this off to always start with a single pane in the home"
                                         " directory."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);




  /*
   * Drop-down
   */
  label = gtk_label_new (_("Drop-down"));
  if (vbox != NULL)
    {
      terminal_preferences_dialog_check_buttons (vbox, check_buttons);
      check_buttons = 0;
    }

  vbox = g_object_new (GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "border-width", 12, "spacing", 18, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  dialog->dropdown_label = GTK_LABEL (label);
  dialog->dropdown_vbox = GTK_BOX (vbox);
  dialog->dropdown_notebook = GTK_NOTEBOOK (notebook);


  /* section: Behavior */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Behavior"));

  /* The drop-down window is an explicitly started mode, so it is worth one
   * compact line only: whatever is set below, it can always be added to the
   * autostart by hand and given a shortcut, on any desktop. */
  {
    GtkWidget *info = gtk_info_bar_new ();
    GtkWidget *info_label;

    gtk_info_bar_set_message_type (GTK_INFO_BAR (info), GTK_MESSAGE_INFO);
    gtk_info_bar_set_show_close_button (GTK_INFO_BAR (info), TRUE);
    info_label = gtk_label_new (_("Regardless of the settings below, you can always"
                                  " get the drop-down terminal window by creating a hot"
                                  " key for \"xfce4-terminal-local --drop-down\"."));
    gtk_label_set_xalign (GTK_LABEL (info_label), 0.0f);
    gtk_label_set_line_wrap (GTK_LABEL (info_label), TRUE);
    gtk_label_set_attributes (GTK_LABEL (info_label), terminal_pango_attr_list_small ());
    gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (info))), info_label);
    gtk_widget_show (info_label);
    gtk_grid_attach (GTK_GRID (grid), info, 0, row, 3, 1);
    gtk_widget_show (info);
  }

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Keep window open when it loses focus"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-keep-open-default",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Use app shortcut to focus the window"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-toggle-focus",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("If enabled, the app shortcut for 'xfce4-terminal --drop-down',"
                                         " which normally opens and closes the window, will give it focus"
                                         " if the option to keep the window open is checked."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-keep-open-default",
                          G_OBJECT (button), "visible",
                          G_BINDING_SYNC_CREATE);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Always keep window above"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-keep-above",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  /* see terminal_window_dropdown_set_property() */
  if (WINDOWING_IS_X11 ())
    {
      /* next row */
      row++;

      button = terminal_preferences_dialog_check_button_new (_("_Always show the status icon (do not quit the process)"));
      g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-keep-alive",
                              G_OBJECT (button), "active",
                              G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
      gtk_widget_set_tooltip_text (button, _("Keep the drop-down window and its status icon alive:"
                                             " when the shell of the last tab ends, a fresh"
                                             " tab is opened and the window is hidden instead"
                                             " of being closed. Closing the window explicitly"
                                             " still quits the application."));
      gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
      gtk_widget_show (button);
    }

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("Toggle the window when _started again"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-parameters-once",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Show or hide the drop-down window when the"
                                         " application is started again, instead of"
                                         " opening a new tab in it."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("Start _hidden"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-start-hidden",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("The drop-down terminal window is started with the"
                                         " session but stays out of sight: it does not cover"
                                         " the desktop at login and appears on the shortcut."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  /* the drop-down terminal window is controlled by a system shortcut, the
   * terminal keeps no key grab of its own: a grab is X11-only and upstream has
   * none either */

  if (terminal_shortcuts_is_xfce_session ())
    {
      button = terminal_preferences_dialog_check_button_new (_("Use a _system shortcut to hide and show the terminal"));
      dialog->shortcut_check = button;
      {
        /* Everything this check box cannot say in one line lives in the
         * tooltip: that keeps the tab short and the text readable. */
        gchar *tip = g_strdup_printf (_("Show or hide the drop-down terminal window with a"
                                        " shortcut of the desktop (F1 by default); the desktop"
                                        " handles the key, so this works in an X11 session as"
                                        " well as in a Wayland one. Without a system shortcut"
                                        " the drop-down terminal window is only shown by"
                                        " \"xfce4-terminal-local --drop-down\"; a shortcut assigned"
                                        " by hand in the keyboard shortcut settings works as well"
                                        " and is recognised here."),
                                      TERMINAL_SHORTCUT_COMMAND);
        gtk_widget_set_tooltip_text (button, tip);
        g_free (tip);
      }
      g_signal_connect (button, "toggled",
                        G_CALLBACK (terminal_preferences_dialog_shortcut_toggled), dialog);
      gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
      gtk_widget_show (button);

      /* the key in use and the button that changes it share the row of the
       * check box: the check box used to span three columns and covered them */
      button = gtk_button_new_with_mnemonic (_("Change _shortcut..."));
      dialog->shortcut_change_button = button;
      gtk_widget_set_tooltip_text (button, _("Press the new key combination and confirm it."));
      g_signal_connect (button, "clicked",
                        G_CALLBACK (terminal_preferences_dialog_shortcut_change), dialog);
      gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
      gtk_widget_show (button);

      dialog->shortcut_key_label = gtk_label_new ("");
      gtk_label_set_xalign (GTK_LABEL (dialog->shortcut_key_label), 0.0f);
      gtk_grid_attach (GTK_GRID (grid), dialog->shortcut_key_label, 2, row, 1, 1);
      gtk_widget_show (dialog->shortcut_key_label);

      /* the state comes from the desktop configuration, not from our settings */
      terminal_preferences_dialog_shortcut_update (dialog);

      /* next row */
      row++;
    }
  else
    {
      /* The shortcut settings of this desktop are not the Xfce ones, so the
       * check box would look as if it worked and quietly do nothing: give the
       * command to assign by hand instead. */
      label = gtk_label_new (_("This desktop does not use the Xfce shortcut settings, so the"
                               " terminal shortcut has to be assigned by hand. Bind this"
                               " command in the keyboard shortcuts of your desktop:"));
      gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
      gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
      gtk_grid_attach (GTK_GRID (grid), label, 0, row, 2, 1);
      gtk_widget_show (label);

      /* next row */
      row++;

      button = gtk_button_new_with_mnemonic (_("_Copy command"));
      gtk_widget_set_tooltip_text (button, TERMINAL_SHORTCUT_COMMAND);
      g_signal_connect (button, "clicked",
                        G_CALLBACK (terminal_preferences_dialog_copy_command), dialog);
      gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
      gtk_widget_show (button);

      /* next row */
      row++;
    }

  /* section: Appearance & Animation */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Appearance and Animation"));

  label = gtk_label_new_with_mnemonic (_("Width:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 10, 100, 1);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-width",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new ("%");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Height:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 10, 100, 1);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-height",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new ("%");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Opacity:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-opacity",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new ("%");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Duration:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 500, 1);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-animation-time",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  /* TRANSLATORS: the unit for milliseconds */
  label = gtk_label_new (_("ms"));
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Always show tabs"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-always-show-tabs",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 3, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Show window borders"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-show-borders",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 3, 1);
  gtk_widget_show (button);


  /* section: Position */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Position"));

  label = gtk_label_new_with_mnemonic (_("Left"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);

  /*marks for the scale */
  gtk_scale_add_mark (GTK_SCALE (button), 0, GTK_POS_BOTTOM, NULL);
  gtk_scale_add_mark (GTK_SCALE (button), 25, GTK_POS_BOTTOM, NULL);
  gtk_scale_add_mark (GTK_SCALE (button), 50, GTK_POS_BOTTOM, NULL);
  gtk_scale_add_mark (GTK_SCALE (button), 75, GTK_POS_BOTTOM, NULL);
  gtk_scale_add_mark (GTK_SCALE (button), 100, GTK_POS_BOTTOM, NULL);

  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-position",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new_with_mnemonic (_("Right"));
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Up"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-position-vertical",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new_with_mnemonic (_("Down"));
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Move to monitor with pointer"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "dropdown-move-to-active",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 3, 1);
  gtk_widget_show (button);



  /*
   * Appearance
   */
  label = gtk_label_new (_("Appearance"));
  if (vbox != NULL)
    {
      terminal_preferences_dialog_check_buttons (vbox, check_buttons);
      check_buttons = 0;
    }

  vbox = g_object_new (GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "border-width", 12, "spacing", 18, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_widget_show (label);
  gtk_widget_show (vbox);


  /* section: Font */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Font"));

  button = terminal_preferences_dialog_check_button_new (_("_Use system font"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "font-use-system",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this to use system-wide monospace font."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  button = gtk_font_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "font-name",
                          G_OBJECT (button), "font-name",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "font-use-system",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  gtk_font_button_set_use_font (GTK_FONT_BUTTON (button), TRUE);
  gtk_font_chooser_set_filter_func (GTK_FONT_CHOOSER (button), (GtkFontFilterFunc) monospace_filter,
                                    NULL, NULL);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 5, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Allow bold text"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "font-allow-bold",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this to allow applications running inside the terminal windows to use bold font."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 6, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Text blinks:"));
  text = _("Controls whether text in the terminal is allowed to blink when '\\e[5m' escape codes are used.");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_widget_set_tooltip_text (label, text);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Never"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("When focused"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("When unfocused"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Always"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "text-blink-mode",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_widget_set_tooltip_text (combo, text);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 5, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Cell spacing:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_spin_button_new_with_range (1.0, 2.0, 0.1);
  g_object_bind_property (G_OBJECT (dialog->preferences), "cell-width-scale",
                          G_OBJECT (button), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new_with_mnemonic (_("x width"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_spin_button_new_with_range (1.0, 2.0, 0.1);
  g_object_bind_property (G_OBJECT (dialog->preferences), "cell-height-scale",
                          G_OBJECT (button), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 3, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new_with_mnemonic (_("x height"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 4, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_button_new_with_mnemonic (_("Reset"));
  g_signal_connect_swapped (button, "clicked",
                            G_CALLBACK (terminal_preferences_dialog_reset_cell_scale), dialog);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 5, row, 1, 1);
  gtk_widget_show (button);


  /* section: Background */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Background"));

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("None (use solid color)"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Background image"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Transparent Background"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "background-mode",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 0, row, 2, 1);
  gtk_widget_show (combo);

  /* next row */
  row++;

  background_grid = grid;

  /* opacity options */
  frame = g_object_new (GTK_TYPE_FRAME, "border-width", 0, "shadow-type", GTK_SHADOW_NONE, NULL);

  /* init row */
  row = 0;

  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_widget_set_margin_top (GTK_WIDGET (grid), 6);
  gtk_widget_set_margin_start (GTK_WIDGET (grid), 12);
  gtk_container_add (GTK_CONTAINER (frame), grid);
  gtk_widget_show (grid);

  gtk_grid_attach (GTK_GRID (background_grid), frame, 0, 1, 1, 1);
  g_object_bind_property_full (G_OBJECT (combo), "active",
                               G_OBJECT (frame), "visible",
                               G_BINDING_SYNC_CREATE,
                               transform_combo_show_opacity_options,
                               NULL, NULL, NULL);

  label = gtk_label_new_with_mnemonic (_("Opacity:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);
  g_object_bind_property (G_OBJECT (dialog->preferences), "background-darkness",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  /* image options */
  frame = g_object_new (GTK_TYPE_FRAME, "border-width", 0, "shadow-type", GTK_SHADOW_NONE, NULL);

  /* init row */
  row = 0;

  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_widget_set_margin_top (GTK_WIDGET (grid), 6);
  gtk_widget_set_margin_start (GTK_WIDGET (grid), 12);
  gtk_container_add (GTK_CONTAINER (frame), grid);
  gtk_widget_show (grid);

  gtk_grid_attach (GTK_GRID (background_grid), frame, 0, 1, 1, 1);
  g_object_bind_property_full (G_OBJECT (combo), "active",
                               G_OBJECT (frame), "visible",
                               G_BINDING_SYNC_CREATE,
                               transform_combo_show_image_options,
                               NULL, NULL, NULL);

  label = gtk_label_new_with_mnemonic (_("File:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_file_chooser_button_new ("Select a Background Image", GTK_FILE_CHOOSER_ACTION_OPEN);
  dialog->file_chooser = button;
  dialog->bg_image_signal_id = g_signal_connect_swapped (dialog->preferences, "notify::background-image-file",
                                                         G_CALLBACK (terminal_preferences_dialog_background_notify), dialog);
  terminal_preferences_dialog_background_notify (dialog);
  g_signal_connect_swapped (button, "file-set",
                            G_CALLBACK (terminal_preferences_dialog_background_set), dialog);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  /* add file filters */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (button), filter);

  /* add "Image Files" filter */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Image Files"));
  gtk_file_filter_add_pixbuf_formats (filter);
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (button), filter);
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (button), filter);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Adjustment:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Tiled"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Centered"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Scaled"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Stretched"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Filled"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "background-image-style",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  gtk_widget_show (combo);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Shading:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
  gtk_scale_set_value_pos (GTK_SCALE (button), GTK_POS_RIGHT);
  g_object_bind_property (G_OBJECT (dialog->preferences), "background-image-shading",
                          G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (button))), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);


  /* section: Opening new windows */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Opening New Windows"));

  button = terminal_preferences_dialog_check_button_new (_("_Display menubar in new windows"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-menubar-default",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to show menubars in newly created terminal windows."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 5, 1);
  gtk_widget_show (button);

  /* Next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Display toolbar in new windows"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-toolbar-default",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to show toolbar in newly created terminal windows."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 5, 1);
  gtk_widget_show (button);

  /* Next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Display borders around new windows"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-borders-default",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to show window decorations around newly created terminal windows."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 5, 1);
  gtk_widget_show (button);

  /* Next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Maximize new windows"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-maximize-default",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to maximize newly created terminal windows."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 5, 1);
  gtk_widget_show (button);

  /* Next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Default geometry"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_spin_button_new_with_range (10, 4000, 1);
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-maximize-default",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  dialog->geometry_set_signal_id[GEOMETRY_COLUMN] =
    g_signal_connect_swapped (button, "value-changed",
                              G_CALLBACK (terminal_preferences_dialog_geometry_changed), dialog);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  dialog->geometry_button[GEOMETRY_COLUMN] = GTK_SPIN_BUTTON (button);

  label = gtk_label_new_with_mnemonic (_("columns"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_spin_button_new_with_range (10, 3000, 1);
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-maximize-default",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  dialog->geometry_set_signal_id[GEOMETRY_ROW] =
    g_signal_connect_swapped (button, "value-changed",
                              G_CALLBACK (terminal_preferences_dialog_geometry_changed), dialog);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 3, row, 1, 1);
  gtk_widget_show (button);

  dialog->geometry_button[GEOMETRY_ROW] = GTK_SPIN_BUTTON (button);

  label = gtk_label_new_with_mnemonic (_("rows"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 4, row, 1, 1);
  gtk_widget_show (label);

  dialog->geometry_notify_signal_id =
    g_signal_connect_swapped (dialog->preferences, "notify::misc-default-geometry",
                              G_CALLBACK (terminal_preferences_dialog_geometry_notify), dialog);
  terminal_preferences_dialog_geometry_notify (dialog);


  /* section: Tabs */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Tabs"));

  label = gtk_label_new_with_mnemonic (_("Reset tab activity indicator after"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_spin_button_new_with_range (0, 30, 1);
  g_object_bind_property (G_OBJECT (dialog->preferences), "tab-activity-timeout",
                          G_OBJECT (button), "value",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new_with_mnemonic (_("seconds"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Use custom styling to make tabs slim (restart required)"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-slim-tabs",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 3, 1);
  gtk_widget_show (button);



  /*
   * Colors
   */
  label = gtk_label_new (_("Colors"));
  if (vbox != NULL)
    {
      terminal_preferences_dialog_check_buttons (vbox, check_buttons);
      check_buttons = 0;
    }

  vbox = g_object_new (GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "border-width", 12, "spacing", 18, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_widget_show (label);
  gtk_widget_show (vbox);


  /* section: General */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("General"));

  label = gtk_label_new (_("Text Color:"));
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-foreground",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-use-theme",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new (_("Background Color:"));
  gtk_grid_attach (GTK_GRID (grid), label, 2, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-background",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-use-theme",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 3, row, 1, 1);
  gtk_widget_show (button);

  label = gtk_label_new (_("Tab activity color:"));
  gtk_grid_attach (GTK_GRID (grid), label, 4, row, 1, 1);
  gtk_widget_show (label);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "tab-activity-color",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-use-theme",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 5, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Use system theme colors for text and background"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-use-theme",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 6, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Vary the background color for each tab"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-background-vary",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("The random color is based on the selected background color, keeping the same brightness."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 6, 1);
  gtk_widget_show (button);

#if VTE_CHECK_VERSION(0, 61, 90)
  if (vte_get_feature_flags () & VTE_FEATURE_FLAG_SIXEL)
    {
      /* next row */
      row++;

      button = terminal_preferences_dialog_check_button_new (_("_Enable sixel graphics"));
      g_object_bind_property (G_OBJECT (dialog->preferences), "enable-sixel",
                              G_OBJECT (button), "active",
                              G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
      gtk_grid_attach (GTK_GRID (grid), button, 0, row, 6, 1);
      gtk_widget_show (button);
    }
#endif

  /* section: Custom colors */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Custom Colors"));

  button = terminal_preferences_dialog_check_button_new (_("_Cursor color:"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-cursor-use-default",
                          G_OBJECT (button), "active",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to set custom text and background colors for cursor."
                                          " If disabled the background and text colors will be reversed."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-cursor-use-default",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-cursor-foreground",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-cursor-use-default",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-cursor",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 2, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Text selection color:"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-selection-use-default",
                          G_OBJECT (button), "active",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to set custom text and background colors for cursor."
                                          " If disabled the background and text colors will be reversed."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-selection-use-default",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-selection",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-selection-use-default",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-selection-background",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 2, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Bold selection color:"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-bold-use-default",
                          G_OBJECT (button), "active",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to set a custom bold color. If disabled the text color will be used."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  button = gtk_color_button_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-bold-use-default",
                          G_OBJECT (button), "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-bold",
                          G_OBJECT (button), "rgba",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);


  /* section: Palette */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Palette"));

  for (gint i = 0; i < N_PALETTE_BUTTONS; i++)
    {
      row = i / (N_PALETTE_BUTTONS / 2);
      button = gtk_color_button_new ();
      dialog->palette_set_signal_id[i] =
        g_signal_connect_swapped (button, "color-set",
                                  G_CALLBACK (terminal_preferences_dialog_palette_changed), dialog);
      gtk_widget_set_halign (button, GTK_ALIGN_START);
      gtk_grid_attach (GTK_GRID (grid), button, i % (N_PALETTE_BUTTONS / 2), row, 1, 1);
      gtk_widget_show (button);
      dialog->palette_button[i] = GTK_COLOR_BUTTON (button);
    }

  dialog->palette_notify_signal_id =
    g_signal_connect_swapped (dialog->preferences, "notify::color-palette",
                              G_CALLBACK (terminal_preferences_dialog_palette_notify), dialog);
  terminal_preferences_dialog_palette_notify (dialog);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Show bold text in bright colors:"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-bold-is-bright",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_tooltip_text (button, _("Enable this option to allow escape sequences such as '\\e[1;35m' to switch text "
                                          "bright colors in addition to bold. If disabled, text color will remain intact."));
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 8, 1);
  gtk_widget_show (button);

  /* section: Presets */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Presets"));

  combo = gtk_combo_box_text_new ();
  terminal_preferences_dialog_presets_load (dialog, combo);
  gtk_widget_set_halign (combo, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), combo, 0, row, 1, 1);
  if (dialog->n_presets == 0)
    gtk_widget_hide (frame);
  else
    gtk_widget_show (combo);



  /*
   * Compatibility
   */
  label = gtk_label_new (_("Compatibility"));
  if (vbox != NULL)
    {
      terminal_preferences_dialog_check_buttons (vbox, check_buttons);
      check_buttons = 0;
    }

  vbox = g_object_new (GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "border-width", 12, "spacing", 18, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_widget_show (label);
  gtk_widget_show (vbox);

  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Compatibility"));

  infobar = gtk_info_bar_new ();
  gtk_container_set_border_width (GTK_CONTAINER (infobar), 1);
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), _("These options may cause some applications to behave incorrectly.\n"
                                              "They are only here to allow you to work around certain applications\n"
                                              "and operating systems that expect different terminal behavior."));
  box = gtk_info_bar_get_content_area (GTK_INFO_BAR (infobar));
  gtk_container_add (GTK_CONTAINER (box), label);
  gtk_info_bar_set_message_type (GTK_INFO_BAR (infobar), GTK_MESSAGE_WARNING);
  gtk_grid_attach (GTK_GRID (grid), infobar, 0, row, 2, 1);
  gtk_widget_show (label);
  gtk_widget_show (infobar);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Backspace key generates:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Auto-detect"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("ASCII Del"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Escape sequence"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Control-H"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Erase TTY"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "binding-backspace",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Delete key generates:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Auto-detect"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("ASCII Del"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Escape sequence"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Control-H"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Erase TTY"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "binding-delete",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Ambiguous-width characters:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Narrow"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Wide"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
  g_object_bind_property (G_OBJECT (dialog->preferences), "binding-ambiguous-width",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  /* next row */
  row++;

  button = gtk_button_new_with_mnemonic (_("Reset compatibility options to defaults"));
  g_signal_connect_swapped (button, "clicked",
                            G_CALLBACK (terminal_preferences_dialog_reset_compatibility_options), dialog);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);



  /*
   * Advanced
   */
  label = gtk_label_new (_("Advanced"));
  if (vbox != NULL)
    {
      terminal_preferences_dialog_check_buttons (vbox, check_buttons);
      check_buttons = 0;
    }

  vbox = g_object_new (GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "border-width", 12, "spacing", 18, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_widget_show (label);
  gtk_widget_show (vbox);


  /* section: Double Click */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Double Click"));

  label = gtk_label_new_with_mnemonic (_("Consider the following characters part of a word when double clicking:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  /* next row */
  row++;

  entry = gtk_entry_new ();
  g_object_bind_property (G_OBJECT (dialog->preferences), "word-chars",
                          G_OBJECT (entry), "text",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 0, row, 1, 1);
  gtk_widget_show (entry);

  button = gtk_button_new_with_mnemonic (_("Reset"));
  g_signal_connect_swapped (button, "clicked",
                            G_CALLBACK (terminal_preferences_dialog_reset_double_click_select), dialog);
  gtk_grid_attach (GTK_GRID (grid), button, 1, row, 1, 1);
  gtk_widget_show (button);


  /* section: Encoding */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Encoding"));

  label = gtk_label_new_with_mnemonic (_("Default character encoding"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  g_object_get (dialog->preferences, "encoding", &current, NULL);
  model = terminal_encoding_model_new (current, &current_iter);
  gtk_combo_box_set_model (GTK_COMBO_BOX (combo), model);
  gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo), &current_iter);
  g_signal_connect_swapped (combo, "changed",
                            G_CALLBACK (terminal_preferences_dialog_encoding_changed), dialog);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  /* free mem */
  g_object_unref (G_OBJECT (model));
  g_free (current);


  /* section: Shortcuts */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Shortcuts"));

  button = terminal_preferences_dialog_check_button_new (_("_Disable all menu access keys (such as Alt + f)"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "shortcuts-no-mnemonics",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Disable menu shortcut key (F10 by default)"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "shortcuts-no-menukey",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 1, 1);
  gtk_widget_show (button);


  /* section: Misc */
  terminal_preferences_dialog_new_section (&frame, &vbox, &grid, &label, &row, _("Misc"));

  button = terminal_preferences_dialog_check_button_new (_("_Use middle mouse click to close tabs"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-tab-close-middle-click",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("Underline urls on mouseover"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-highlight-urls",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Use middle mouse click to open urls"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-middle-click-opens-uri",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-highlight-urls",
                          G_OBJECT (button), "visible",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("Enable OSC 8 Hyperlinks"));
  gtk_widget_set_tooltip_text (button, _("These are the links displayed when you run the 'ls --hyperlink' command, or by any other means using the OSC 8 escape sequence."));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-hyperlinks-enabled",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Auto-hide mouse pointer"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-mouse-autohide",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Open new tab to the right of the active tab"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-new-tab-adjacent",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Audible bell"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-bell",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  button = terminal_preferences_dialog_check_button_new (_("_Visual bell"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-bell-urgent",
                          G_OBJECT (button), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_grid_attach (GTK_GRID (grid), button, 0, row, 2, 1);
  gtk_widget_show (button);

  /* next row */
  row++;

  label = gtk_label_new_with_mnemonic (_("Right click action:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Context Menu"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Paste clipboard"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Paste selection"));
  g_object_bind_property (G_OBJECT (dialog->preferences), "misc-right-click-action",
                          G_OBJECT (combo), "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
  terminal_gtk_label_set_a11y_relation (GTK_LABEL (label), combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);



  /*
   * Shortcuts
   */
  label = gtk_label_new (_("Shortcuts"));
  if (vbox != NULL)
    {
      terminal_preferences_dialog_check_buttons (vbox, check_buttons);
      check_buttons = 0;
    }

  vbox = g_object_new (GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "border-width", 12, "spacing", 18, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_widget_show (label);
  gtk_widget_show (vbox);

  frame = g_object_new (GTK_TYPE_FRAME, "border-width", 0, "shadow-type", GTK_SHADOW_IN, NULL);
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, TRUE, 0);
  gtk_widget_show (frame);

  grid = xfce_shortcuts_editor_new (7, _("Window"),
                                    terminal_window_get_action_entries (),
                                    (size_t) TERMINAL_WINDOW_ACTION_N, _("Terminal"),
                                    terminal_widget_get_action_entries (),
                                    (size_t) TERMINAL_WIDGET_ACTION_N);
  gtk_container_add (GTK_CONTAINER (frame), grid);
  gtk_widget_show (grid);

  terminal_preferences_dialog_check_buttons (vbox, check_buttons);
  check_buttons = 0;

  /* Overlapping widgets have bitten this dialog twice: a forgotten row
   * increment gave two check boxes the same place, and the shortcut check box
   * spanned three columns and covered the button beside it. The walk needs a
   * mapped dialog and a visible page, so it waits for the first map and for
   * every page the user brings up; it only runs when asked for, but a stand
   * reads its report from stderr. */
  if (g_getenv ("TERMINAL_DEBUG_UI") != NULL)
    {
      g_signal_connect (dialog, "map",
                        G_CALLBACK (terminal_preferences_dialog_check_layout_on_map), NULL);
      g_signal_connect_swapped (notebook, "switch-page",
                                G_CALLBACK (terminal_preferences_dialog_check_layout_on_map), dialog);
    }
}



static gint
terminal_preferences_dialog_count_buttons (GtkWidget *widget)
{
  GList *children, *iter;
  gint found = 0;

  if (GTK_IS_CHECK_BUTTON (widget) || GTK_IS_RADIO_BUTTON (widget))
    found++;

  children = gtk_container_get_children (GTK_CONTAINER (widget));
  for (iter = children; iter != NULL; iter = iter->next)
    found += terminal_preferences_dialog_count_buttons (GTK_WIDGET (iter->data));
  g_list_free (children);

  return found;
}



/* GtkGrid drops the first widget of a cell without a word, so two check boxes
 * of one page that were meant to sit in two rows do not overlap on the screen:
 * one of them simply is not there. Comparing what was built with what ended
 * up on the page is the only way to see it. */
static void
terminal_preferences_dialog_check_buttons (GtkWidget *page,
                                            gint        built)
{
  gint found;

  if (g_getenv ("TERMINAL_DEBUG_UI") == NULL || page == NULL || !GTK_IS_WIDGET (page))
    return;

  found = terminal_preferences_dialog_count_buttons (page);
  if (found != built)
    g_printerr ("layout-missing: %d check buttons built, %d on the page\n", built, found);
}



static GtkWidget *
terminal_preferences_dialog_check_button_new (const gchar *mnemonic)
{
  check_buttons++;
  return gtk_check_button_new_with_mnemonic (mnemonic);
}



static gboolean
terminal_preferences_dialog_check_layout_idle (gpointer data)
{
  terminal_preferences_dialog_check_layout (GTK_WIDGET (data));
  return G_SOURCE_REMOVE;
}



static void
terminal_preferences_dialog_check_layout_on_map (GtkWidget *widget,
                                                 gpointer   data)
{
  /* one idle turn later the page has its final geometry */
  g_idle_add (terminal_preferences_dialog_check_layout_idle,
              data != NULL ? data : widget);
}



/* Report any two widgets of the dialog that share pixels without one of them
 * being an ancestor of the other. The grid cell itself is of no use here:
 * GtkGrid quietly drops the first widget of a cell, so a clash shows up only
 * in the geometry. */
static void
terminal_preferences_dialog_check_layout (GtkWidget *widget)
{
  GList *drawn = NULL, *a, *b, *children;

  if (!gtk_widget_get_visible (widget) || !gtk_widget_get_realized (widget))
    return;

  children = gtk_container_get_children (GTK_CONTAINER (widget));
  for (a = children; a != NULL; a = a->next)
    terminal_preferences_dialog_check_layout (GTK_WIDGET (a->data));
  g_list_free (children);

  /* the children come first, bottom up, so every pair is seen once */
  if (gtk_widget_get_window (widget) != NULL)
    drawn = g_list_prepend (drawn, widget);

  for (a = drawn; a != NULL; a = a->next)
    for (b = a->next; b != NULL; b = b->next)
      {
        GtkWidget *first = GTK_WIDGET (a->data);
        GtkWidget *second = GTK_WIDGET (b->data);
        GtkAllocation r1, r2;

        if (gtk_widget_is_ancestor (first, second) || gtk_widget_is_ancestor (second, first))
          continue;

        gtk_widget_get_allocation (first, &r1);
        gtk_widget_get_allocation (second, &r2);
        if (r1.x < r2.x + r2.width && r2.x < r1.x + r1.width
            && r1.y < r2.y + r2.height && r2.y < r1.y + r1.height)
          g_printerr ("layout-overlap: %s at %d,%d %dx%d and %s at %d,%d %dx%d\n",
                      G_OBJECT_TYPE_NAME (first), r1.x, r1.y, r1.width, r1.height,
                      G_OBJECT_TYPE_NAME (second), r2.x, r2.y, r2.width, r2.height);
      }

  g_list_free (drawn);
}



static PangoAttrList *
terminal_pango_attr_list_small (void)
{
  static PangoAttrList *attr_list = NULL;
  if (G_UNLIKELY (attr_list == NULL))
    {
      attr_list = pango_attr_list_new ();
      pango_attr_list_insert (attr_list, pango_attr_scale_new (PANGO_SCALE_SMALL));
      pango_attr_list_insert (attr_list, pango_attr_style_new (PANGO_STYLE_ITALIC));
    }
  return attr_list;
}



static PangoAttrList *
terminal_pango_attr_list_bold (void)
{
  static PangoAttrList *attr_list = NULL;
  if (G_UNLIKELY (attr_list == NULL))
    {
      attr_list = pango_attr_list_new ();
      pango_attr_list_insert (attr_list, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    }
  return attr_list;
}



/* The key is read from a window of our own instead of a server wide grab: a
 * Wayland client is not allowed to grab keys at all, and a dialog that has the
 * focus sees the combination just as well. */
static gboolean
terminal_preferences_dialog_grab_key_press (GtkWidget *widget,
                                            GdkEventKey *event,
                                            gpointer   user_data)
{
  GtkWidget *dialog = GTK_WIDGET (user_data);
  guint keyval = event->keyval;
  guint mods = event->state & gtk_accelerator_get_default_mod_mask ();
  gchar *name;

  if (keyval == GDK_KEY_Escape)
    {
      gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
      return GDK_EVENT_STOP;
    }

  /* modifiers on their own are not a shortcut, wait for a real key */
  switch (keyval)
    {
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:
    case GDK_KEY_Control_L: case GDK_KEY_Control_R:
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
    case GDK_KEY_Super_L: case GDK_KEY_Super_R:
    case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:
    case GDK_KEY_Hyper_L: case GDK_KEY_Hyper_R:
    case GDK_KEY_ISO_Level3_Shift: case GDK_KEY_ISO_Level3_Latch:
    case GDK_KEY_ISO_Level3_Lock: case GDK_KEY_ISO_Next_Group:
    case GDK_KEY_ISO_Prev_Group: case GDK_KEY_ISO_First_Group:
    case GDK_KEY_ISO_Last_Group: case GDK_KEY_Mode_switch:
    case GDK_KEY_Caps_Lock: case GDK_KEY_Shift_Lock:
    case GDK_KEY_Num_Lock: case GDK_KEY_Scroll_Lock:
      return GDK_EVENT_STOP;
    default:
      break;
    }

  name = gtk_accelerator_name (keyval, mods);
  if (G_UNLIKELY (name == NULL || *name == '\0'))
    {
      g_free (name);
      return GDK_EVENT_STOP;
    }

  g_object_set_data_full (G_OBJECT (dialog), "binding", name, g_free);
  gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  return GDK_EVENT_STOP;
}

static gchar *
terminal_preferences_dialog_grab_key (GtkWindow *parent)
{
  GtkWidget *dialog;
  GtkWidget *content;
  GtkWidget *label;
  gchar *binding = NULL;

  dialog = gtk_dialog_new_with_buttons (_("Press the shortcut for the drop-down panel"), parent,
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        _("_Cancel"), GTK_RESPONSE_CANCEL,
                                        NULL);
  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  label = gtk_label_new (_("Press the key combination, Escape cancels"));
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_widget_set_margin_start (label, 12);
  gtk_widget_set_margin_end (label, 12);
  gtk_widget_set_margin_top (label, 12);
  gtk_widget_set_margin_bottom (label, 12);
  gtk_container_add (GTK_CONTAINER (content), label);
  gtk_widget_show_all (dialog);

  g_signal_connect (dialog, "key-press-event",
                    G_CALLBACK (terminal_preferences_dialog_grab_key_press), dialog);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    binding = g_strdup (g_object_get_data (G_OBJECT (dialog), "binding"));

  gtk_widget_destroy (dialog);

  return binding;
}

static void
terminal_preferences_dialog_shortcut_update (TerminalPreferencesDialog *dialog)
{
  gchar *binding;
  gchar *text = NULL;
  guint key = 0, mods = 0;

  binding = terminal_shortcuts_get_drop_down_binding ();
  if (binding != NULL)
    {
      gtk_accelerator_parse (binding, &key, &mods);
      text = gtk_accelerator_get_label (key, mods);
    }

  gtk_label_set_text (GTK_LABEL (dialog->shortcut_key_label),
                      text != NULL ? text : _("not set"));
  gtk_widget_set_tooltip_text (dialog->shortcut_change_button, text);

  g_signal_handlers_block_by_func (dialog->shortcut_check,
                                   terminal_preferences_dialog_shortcut_toggled, dialog);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->shortcut_check), binding != NULL);
  g_signal_handlers_unblock_by_func (dialog->shortcut_check,
                                     terminal_preferences_dialog_shortcut_toggled, dialog);

  g_free (text);
  g_free (binding);
}

static void
terminal_preferences_dialog_shortcut_toggled (GtkToggleButton *button,
                                              gpointer         user_data)
{
  TerminalPreferencesDialog *dialog = TERMINAL_PREFERENCES_DIALOG (user_data);
  gchar *binding;

  if (gtk_toggle_button_get_active (button))
    {
      if (terminal_shortcuts_get_drop_down_binding () != NULL)
        return;

      binding = terminal_preferences_dialog_grab_key (GTK_WINDOW (dialog));
      if (binding == NULL)
        {
          /* cancelled: put the checkmark back where it was */
          terminal_preferences_dialog_shortcut_update (dialog);
          return;
        }

      terminal_shortcuts_set_drop_down_binding (binding);
      g_free (binding);
    }
  else
    {
      terminal_shortcuts_remove_drop_down_binding ();
    }

  terminal_preferences_dialog_shortcut_update (dialog);
}

static void
terminal_preferences_dialog_copy_command (GtkButton *button,
                                          gpointer   user_data)
{
  GtkClipboard *clipboard;

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_text (clipboard, TERMINAL_SHORTCUT_COMMAND, -1);
  gtk_clipboard_store (clipboard);
}



static void
terminal_preferences_dialog_shortcut_change (GtkButton *button,
                                             gpointer   user_data)
{
  TerminalPreferencesDialog *dialog = TERMINAL_PREFERENCES_DIALOG (user_data);
  gchar *binding;
  gchar *conflict;

  binding = terminal_preferences_dialog_grab_key (GTK_WINDOW (dialog));
  if (binding == NULL)
    return;

  conflict = terminal_shortcuts_get_conflict (binding);
  if (conflict != NULL)
    {
      GtkWidget *confirm;
      gchar *question;
      gint choice;

      question = g_strdup_printf (_("The combination %s is already used by \"%s\"."
                                    " Use it for the drop-down panel anyway?"),
                                  binding, conflict);
      confirm = xfce_message_dialog_new (GTK_WINDOW (dialog),
                                         _("Replace the shortcut?"),
                                         "dialog-warning",
                                         _("Combination already in use"),
                                         question,
                                         _("_Use it"), NULL);
      g_free (question);

      choice = gtk_dialog_run (GTK_DIALOG (confirm));
      gtk_widget_destroy (confirm);

      if (choice != GTK_RESPONSE_ACCEPT)
        {
          g_free (conflict);
          g_free (binding);
          terminal_preferences_dialog_shortcut_update (dialog);
          return;
        }
    }
  g_free (conflict);

  terminal_shortcuts_remove_drop_down_binding ();
  terminal_shortcuts_set_drop_down_binding (binding);
  g_free (binding);

  terminal_preferences_dialog_shortcut_update (dialog);
}



static void
terminal_preferences_dialog_new_section (GtkWidget **frame,
                                         GtkWidget **vbox,
                                         GtkWidget **grid,
                                         GtkWidget **label,
                                         gint *row,
                                         const gchar *header)
{
  *frame = g_object_new (GTK_TYPE_FRAME, "border-width", 0, "shadow-type", GTK_SHADOW_NONE, NULL);
  gtk_box_pack_start (GTK_BOX (*vbox), *frame, FALSE, TRUE, 0);
  gtk_widget_show (*frame);

  *label = gtk_label_new (header);
  /* For bold text */
  gtk_label_set_attributes (GTK_LABEL (*label), terminal_pango_attr_list_bold ());
  gtk_frame_set_label_widget (GTK_FRAME (*frame), *label);
  gtk_widget_show (*label);

  /* init row */
  *row = 0;

  *grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (*grid), 12);
  gtk_grid_set_row_spacing (GTK_GRID (*grid), 6);
  gtk_widget_set_margin_top (GTK_WIDGET (*grid), 6);
  gtk_widget_set_margin_start (GTK_WIDGET (*grid), 12);
  gtk_container_add (GTK_CONTAINER (*frame), *grid);
  gtk_widget_show (*grid);
}



static void
terminal_preferences_dialog_background_notify (TerminalPreferencesDialog *dialog)
{
  gchar *button_file, *prop_file;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));

  button_file = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog->file_chooser));
  g_object_get (G_OBJECT (dialog->preferences), "background-image-file", &prop_file, NULL);
  if (g_strcmp0 (button_file, prop_file) != 0)
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (dialog->file_chooser), prop_file);
  g_free (button_file);
  g_free (prop_file);
}



static void
terminal_preferences_dialog_background_set (TerminalPreferencesDialog *dialog,
                                            GtkWidget *widget)
{
  gchar *filename;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  g_return_if_fail (GTK_IS_FILE_CHOOSER_BUTTON (widget));

  filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
  g_signal_handler_block (dialog->preferences, dialog->bg_image_signal_id);
  g_object_set (G_OBJECT (dialog->preferences), "background-image-file", filename, NULL);
  g_signal_handler_unblock (dialog->preferences, dialog->bg_image_signal_id);
  g_free (filename);
}



static void
terminal_preferences_dialog_geometry_notify (TerminalPreferencesDialog *dialog)
{
  gchar *geometry = NULL;
  gchar **split = NULL;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));

  g_object_get (G_OBJECT (dialog->preferences), "misc-default-geometry", &geometry, NULL);

  if (G_UNLIKELY (geometry == NULL))
    return;

  split = g_strsplit (geometry, "x", -1);
  g_free (geometry);

  if (G_UNLIKELY (split == NULL))
    return;

  /* apply values to buttons */
  for (gint i = 0; i < N_GEOMETRY_BUTTONS; i++)
    {
      /* setting the value should not trigger an update in the preference, hence the "value-changed" signal is being blocked */
      g_signal_handler_block (dialog->geometry_button[i], dialog->geometry_set_signal_id[i]);
      gtk_spin_button_set_value (dialog->geometry_button[i], atoi (split[i]));
      /* unblock signal */
      g_signal_handler_unblock (dialog->geometry_button[i], dialog->geometry_set_signal_id[i]);
    }

  g_strfreev (split);
}



static void
terminal_preferences_dialog_geometry_changed (TerminalPreferencesDialog *dialog,
                                              GtkWidget *widget)
{
  gint rows;
  gint cols;
  gchar *geometry;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  g_return_if_fail (GTK_IS_SPIN_BUTTON (widget));

  cols = gtk_spin_button_get_value_as_int (dialog->geometry_button[GEOMETRY_COLUMN]);
  rows = gtk_spin_button_get_value_as_int (dialog->geometry_button[GEOMETRY_ROW]);

  geometry = g_strdup_printf ("%dx%d", cols, rows);

  /* set property */

  /* setting the property should not trigger an update on the buttons, otherwise it will go on in a loop */
  g_signal_handler_block (dialog->preferences, dialog->geometry_notify_signal_id);
  g_object_set (G_OBJECT (dialog->preferences), "misc-default-geometry", geometry, NULL);
  /* unblock signal */
  g_signal_handler_unblock (dialog->preferences, dialog->geometry_notify_signal_id);

  g_free (geometry);
}



static void
terminal_preferences_dialog_palette_notify (TerminalPreferencesDialog *dialog)
{
  guint i;
  GdkRGBA color;
  gchar *color_str = NULL;
  gchar **colors = NULL;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));

  g_object_get (dialog->preferences, "color-palette", &color_str, NULL);

  if (G_UNLIKELY (color_str == NULL))
    return;

  /* make array */
  colors = g_strsplit (color_str, ";", -1);
  g_free (color_str);

  if (G_UNLIKELY (colors == NULL))
    return;

  /* apply values to buttons */
  for (i = 0; i < N_PALETTE_BUTTONS && colors[i] != NULL; i++)
    {
      /* setting the color should not trigger an update in the preference, hence the "color-set" signal is being blocked */
      g_signal_handler_block (GTK_WIDGET (dialog->palette_button[i]), dialog->palette_set_signal_id[i]);
      if (gdk_rgba_parse (&color, colors[i]))
        gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (dialog->palette_button[i]), &color);
      /* unblock signal */
      g_signal_handler_unblock (GTK_WIDGET (dialog->palette_button[i]), dialog->palette_set_signal_id[i]);
    }

  g_strfreev (colors);
}



static void
terminal_preferences_dialog_palette_changed (TerminalPreferencesDialog *dialog,
                                             GtkWidget *widget)
{
  guint i;
  GdkRGBA color;
  gchar *color_str;
  GString *array;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  g_return_if_fail (GTK_IS_COLOR_BUTTON (widget));

  array = g_string_sized_new (225);

  for (i = 0; i < N_PALETTE_BUTTONS; i++)
    {
      gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (dialog->palette_button[i]), &color);

      /* append to string */
      color_str = gdk_rgba_to_string (&color);
      g_string_append (array, color_str);
      g_free (color_str);

      if (i != N_PALETTE_BUTTONS - 1)
        g_string_append_c (array, ';');
    }

  /* set property */

  /* setting the property should not trigger an update on the buttons, otherwise it will go on in a loop */
  g_signal_handler_block (dialog->preferences, dialog->palette_notify_signal_id);
  g_object_set (dialog->preferences, "color-palette", array->str, NULL);
  /* unblock signal */
  g_signal_handler_unblock (dialog->preferences, dialog->palette_notify_signal_id);

  g_string_free (array, TRUE);
}



static gboolean
terminal_preferences_dialog_presets_sepfunc (GtkTreeModel *model,
                                             GtkTreeIter *iter,
                                             gpointer user_data)
{
  gboolean is_separator;
  gtk_tree_model_get (model, iter, PRESET_COLUMN_IS_SEPARATOR, &is_separator, -1);
  return is_separator;
}



static void
terminal_preferences_dialog_presets_changed (TerminalPreferencesDialog *dialog,
                                             GtkWidget *widget)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkComboBox *combobox;
  gchar *path;
  XfceRc *rc;
  GParamSpec **pspecs, *pspec;
  guint nspecs;
  guint n;
  const gchar *blurb;
  const gchar *name;
  const gchar *str;
  GValue src = G_VALUE_INIT;
  GValue dst = G_VALUE_INIT;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  g_return_if_fail (GTK_IS_COMBO_BOX (widget));

  combobox = GTK_COMBO_BOX (widget);

  if (!gtk_combo_box_get_active_iter (combobox, &iter))
    return;

  model = gtk_combo_box_get_model (combobox);
  gtk_tree_model_get (model, &iter, PRESET_COLUMN_PATH, &path, -1);
  if (path == NULL)
    return;

  /* load file */
  rc = xfce_rc_simple_open (path, TRUE);
  g_free (path);
  if (G_UNLIKELY (rc == NULL))
    return;

  xfce_rc_set_group (rc, "Scheme");

  g_value_init (&src, G_TYPE_STRING);

  /* walk all properties and look for items in the scheme */
  pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (dialog->preferences), &nspecs);
  for (n = 0; n < nspecs; ++n)
    {
      pspec = pspecs[n];

      /* get color keys */
      blurb = g_param_spec_get_blurb (pspec);
      if (strstr (blurb, "Color") == NULL)
        continue;

      /* read value */
      name = g_param_spec_get_name (pspec);
      str = xfce_rc_read_entry_untranslated (rc, blurb, NULL);

      if (str == NULL || *str == '\0')
        {
          /* reset to the default value */
          g_value_init (&dst, G_PARAM_SPEC_VALUE_TYPE (pspec));
          g_param_value_set_default (pspec, &dst);
          g_object_set_property (G_OBJECT (dialog->preferences), name, &dst);
          g_value_unset (&dst);
        }
      else
        {
          g_value_set_static_string (&src, str);

          if (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_STRING)
            {
              /* set the string property */
              g_object_set_property (G_OBJECT (dialog->preferences), name, &src);
            }
          else
            {
              /* transform value */
              g_value_init (&dst, G_PARAM_SPEC_VALUE_TYPE (pspec));
              if (G_LIKELY (g_value_transform (&src, &dst)))
                g_object_set_property (G_OBJECT (dialog->preferences), name, &dst);
              else
                g_warning ("Unable to convert scheme property \"%s\"", name);
              g_value_unset (&dst);
            }
        }
    }

  g_free (pspecs);
  g_value_unset (&src);
  xfce_rc_close (rc);
}



static void
terminal_preferences_dialog_presets_load (TerminalPreferencesDialog *dialog,
                                          GtkWidget *widget)
{
  gchar **global, **user, **presets;
  guint n_global, n_user, n_presets = 0, n;
  XfceRc *rc;
  GtkListStore *store;
  GtkTreeIter iter;
  GtkComboBox *combobox;
  const gchar *title;
  gchar *path;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  g_return_if_fail (GTK_IS_COMBO_BOX (widget));

  combobox = GTK_COMBO_BOX (widget);

  /* load schemes */
  global = xfce_resource_match (XFCE_RESOURCE_DATA, "xfce4/terminal/colorschemes/*", TRUE);
  user = xfce_resource_match (XFCE_RESOURCE_CONFIG, "xfce4/terminal/colorschemes/*", TRUE);
  n_global = g_strv_length (global);
  n_user = g_strv_length (user);
  presets = g_new0 (gchar *, n_global + n_user);
  if (G_LIKELY (presets != NULL))
    {
      /* copy pointers to global- and user-defined presets */
      for (n = 0; n < n_global; n++)
        presets[n] = global[n];
      for (n = 0; n < n_user; n++)
        presets[n_global + n] = user[n];

      /* create sorting store */
      store = gtk_list_store_new (N_PRESET_COLUMNS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
      gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                            PRESET_COLUMN_TITLE,
                                            GTK_SORT_ASCENDING);

      /* append files */
      for (n = 0; n < n_global + n_user && presets[n] != NULL; n++)
        {
          /* open the scheme */
          path = xfce_resource_lookup (n < n_global ? XFCE_RESOURCE_DATA : XFCE_RESOURCE_CONFIG,
                                       presets[n]);
          if (G_UNLIKELY (path == NULL))
            continue;

          rc = xfce_rc_simple_open (path, TRUE);
          if (G_UNLIKELY (rc == NULL))
            {
              g_free (path);
              continue;
            }

          xfce_rc_set_group (rc, "Scheme");

          /* translated name */
          title = xfce_rc_read_entry (rc, "Name", NULL);
          if (G_LIKELY (title != NULL))
            {
              gtk_list_store_insert_with_values (store, NULL, n_presets++, PRESET_COLUMN_TITLE, title, PRESET_COLUMN_PATH, path, -1);
            }

          xfce_rc_close (rc);
          g_free (path);
        }

      /* stop sorting */
      gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                            GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
                                            GTK_SORT_ASCENDING);

      /* default item + separator */
      gtk_list_store_insert_with_values (store, &iter, 0, PRESET_COLUMN_TITLE, _("Select Preset"), -1);
      gtk_list_store_insert_with_values (store, NULL, 1, PRESET_COLUMN_IS_SEPARATOR, TRUE, -1);

      /* set model */
      gtk_combo_box_set_model (combobox, GTK_TREE_MODEL (store));
      gtk_combo_box_set_active_iter (combobox, &iter);
      gtk_combo_box_set_row_separator_func (combobox, terminal_preferences_dialog_presets_sepfunc, NULL, NULL);
      g_signal_connect_swapped (combobox, "changed",
                                G_CALLBACK (terminal_preferences_dialog_presets_changed), dialog);
      g_object_unref (store);
    }

  g_strfreev (global);
  g_strfreev (user);
  g_free (presets);

  dialog->n_presets = n_presets;
}



static void
terminal_preferences_dialog_reset_properties (TerminalPreferencesDialog *dialog,
                                              guint props_n,
                                              const gchar **props)
{
  for (guint i = 0; i < props_n; i++)
    {
      GParamSpec *spec = g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->preferences), props[i]);
      if (G_LIKELY (spec != NULL))
        {
          GValue value = G_VALUE_INIT;
          g_value_init (&value, spec->value_type);
          g_param_value_set_default (spec, &value);
          g_object_set_property (G_OBJECT (dialog->preferences), props[i], &value);
          g_value_unset (&value);
        }
    }
}



static void
terminal_preferences_dialog_reset_cell_scale (TerminalPreferencesDialog *dialog,
                                              GtkWidget *widget)
{
  const gchar *properties[] = { "cell-width-scale", "cell-height-scale" };
  terminal_preferences_dialog_reset_properties (dialog, G_N_ELEMENTS (properties), properties);
}



static void
terminal_preferences_dialog_reset_compatibility_options (TerminalPreferencesDialog *dialog,
                                                         GtkWidget *widget)
{
  const gchar *properties[] = { "binding-backspace", "binding-delete", "binding-ambiguous-width" };
  terminal_preferences_dialog_reset_properties (dialog, G_N_ELEMENTS (properties), properties);
}



static void
terminal_preferences_dialog_reset_double_click_select (TerminalPreferencesDialog *dialog,
                                                       GtkWidget *widget)
{
  const gchar *properties[] = { "word-chars" };
  terminal_preferences_dialog_reset_properties (dialog, G_N_ELEMENTS (properties), properties);
}



static void
terminal_preferences_dialog_encoding_changed (TerminalPreferencesDialog *dialog,
                                              GtkWidget *widget)
{
  GtkTreeIter iter;
  gchar *encoding;
  GtkTreeModel *model;
  gboolean is_charset;
  GtkTreeIter child_iter;
  GtkComboBox *combobox;

  g_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  g_return_if_fail (GTK_IS_COMBO_BOX (widget));

  combobox = GTK_COMBO_BOX (widget);

  if (gtk_combo_box_get_active_iter (combobox, &iter))
    {
      model = gtk_combo_box_get_model (combobox);
      gtk_tree_model_get (model, &iter, ENCODING_COLUMN_IS_CHARSET, &is_charset, ENCODING_COLUMN_VALUE, &encoding, -1);

      /* select the child if a menu header is clicked */
      if (encoding == NULL && !is_charset)
        {
          if (gtk_tree_model_iter_children (model, &child_iter, &iter))
            gtk_combo_box_set_active_iter (combobox, &child_iter);
          return;
        }

      /* save new default */
      g_object_set (dialog->preferences, "encoding", encoding, NULL);
      g_free (encoding);
    }
}



static void
terminal_gtk_label_set_a11y_relation (GtkLabel *label,
                                      GtkWidget *widget)
{
  AtkRelationSet *relations;
  AtkRelation *relation;
  AtkObject *object;

  g_return_if_fail (GTK_IS_WIDGET (widget));
  g_return_if_fail (GTK_IS_LABEL (label));

  object = gtk_widget_get_accessible (widget);
  relations = atk_object_ref_relation_set (gtk_widget_get_accessible (GTK_WIDGET (label)));
  relation = atk_relation_new (&object, 1, ATK_RELATION_LABEL_FOR);
  atk_relation_set_add (relations, relation);
  g_object_unref (G_OBJECT (relation));
  g_object_unref (relations);
}



static gboolean
monospace_filter (const PangoFontFamily *family,
                  const PangoFontFace *face,
                  gpointer data)
{
  return pango_font_family_is_monospace ((PangoFontFamily *) family);
}



static void
terminal_preferences_dialog_finalize (GObject *object)
{
  TerminalPreferencesDialog *dialog = TERMINAL_PREFERENCES_DIALOG (object);

  /* disconnect signals */
  if (G_LIKELY (dialog->bg_image_signal_id != 0))
    g_signal_handler_disconnect (dialog->preferences, dialog->bg_image_signal_id);
  if (G_LIKELY (dialog->palette_notify_signal_id != 0))
    g_signal_handler_disconnect (dialog->preferences, dialog->palette_notify_signal_id);
  if (G_LIKELY (dialog->geometry_notify_signal_id != 0))
    g_signal_handler_disconnect (dialog->preferences, dialog->geometry_notify_signal_id);

  /* other signals like the set-signals do not need to be disconnected since the
   * instances have already been released and the respective signals taken care of */

  /* release our reference on the preferences */
  g_object_unref (G_OBJECT (dialog->preferences));

  (*G_OBJECT_CLASS (terminal_preferences_dialog_parent_class)->finalize) (object);
}



static void
terminal_preferences_dialog_response (GtkDialog *dialog,
                                      gint response)
{
  if (G_UNLIKELY (response == GTK_RESPONSE_HELP))
    {
      /* open the preferences section of the user manual */
      xfce_dialog_show_help (GTK_WINDOW (dialog), "xfce4-terminal", "preferences", NULL);
    }
  else
    {
      /* close the preferences dialog */
      gtk_widget_destroy (GTK_WIDGET (dialog));
    }
}



GtkWidget *
terminal_preferences_dialog_new (gboolean show_drop_down,
                                 gboolean drop_down_mode)
{
  static TerminalPreferencesDialog *dialog = NULL;

  if (G_LIKELY (dialog == NULL))
    {
      dialog = g_object_new (TERMINAL_TYPE_PREFERENCES_DIALOG, NULL);
      g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer) &dialog);
    }

  gtk_widget_set_visible (GTK_WIDGET (dialog->dropdown_label), show_drop_down);
  gtk_widget_set_visible (GTK_WIDGET (dialog->dropdown_vbox), show_drop_down);

  if (drop_down_mode)
    gtk_notebook_set_current_page (GTK_NOTEBOOK (dialog->dropdown_notebook), 1);

  return GTK_WIDGET (dialog);
}
