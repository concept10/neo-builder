/* gbp-create-project-widget.c
 *
 * Copyright 2016-2022 Christian Hergert <christian@hergert.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "gbp-create-project-widget"

#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include <libide-greeter.h>
#include <libide-plugins.h>
#include <libide-projects.h>
#include <libide-vcs.h>

#include "gbp-create-project-widget.h"

struct _GbpCreateProjectWidget
{
  GtkWidget         parent_instance;

  GtkWidget        *main;
  IdeTemplateInput *input;
  GtkMenuButton    *template_button;
  GtkMenuButton    *language_button;
  GtkMenuButton    *licenses_button;
  GtkImage         *directory_clash;

  AdwEntryRow      *app_id_row;
  AdwEntryRow      *language_row;
  AdwEntryRow      *location_row;
  AdwEntryRow      *name_row;
  AdwEntryRow      *template_row;
};

enum {
  PROP_0,
  PROP_IS_READY,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (GbpCreateProjectWidget, gbp_create_project_widget, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

#define ADD_ERROR(widget) gtk_widget_add_css_class(GTK_WIDGET(widget),"error")
#define REMOVE_ERROR(widget) gtk_widget_remove_css_class(GTK_WIDGET(widget),"error")

static gboolean
gbp_create_project_widget_check_ready (GbpCreateProjectWidget *self)
{
  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));

  return TRUE;
}

static void
template_activated_cb (GbpCreateProjectWidget *self,
                       guint                   position,
                       GtkListView            *list_view)
{
  g_autoptr(IdeProjectTemplate) template = NULL;
  g_autofree char *id = NULL;
  GListModel *model;

  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));
  g_assert (GTK_IS_LIST_VIEW (list_view));

  model = G_LIST_MODEL (gtk_list_view_get_model (list_view));
  template = g_list_model_get_item (model, position);

  gtk_menu_button_popdown (self->template_button);

  id = ide_project_template_get_id (template);
  ide_template_input_set_template (self->input, id);
}

static void
language_activated_cb (GbpCreateProjectWidget *self,
                       guint                   position,
                       GtkListView            *list_view)
{
  g_autoptr(GtkStringObject) string = NULL;
  GListModel *model;

  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));
  g_assert (GTK_IS_LIST_VIEW (list_view));

  model = G_LIST_MODEL (gtk_list_view_get_model (list_view));
  string = g_list_model_get_item (model, position);

  gtk_menu_button_popdown (self->language_button);

  ide_template_input_set_language (self->input,
                                   gtk_string_object_get_string (string));
}

static void
license_activated_cb (GbpCreateProjectWidget *self,
                      guint                   position,
                      GtkListView            *list_view)
{
  g_autoptr(GtkStringObject) string = NULL;
  GListModel *model;

  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));
  g_assert (GTK_IS_LIST_VIEW (list_view));

  model = G_LIST_MODEL (gtk_list_view_get_model (list_view));
  string = g_list_model_get_item (model, position);

  gtk_menu_button_popdown (self->licenses_button);

  ide_template_input_set_license_name (self->input,
                                       gtk_string_object_get_string (string));
}

static void
location_row_changed_cb (GbpCreateProjectWidget *self,
                         GtkEditable            *editable)
{
  g_autofree char *expanded = NULL;
  g_autoptr(GFile) directory = NULL;
  const char *text;

  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));
  g_assert (GTK_IS_EDITABLE (editable));

  text = gtk_editable_get_text (editable);
  expanded = ide_path_expand (text);
  directory = g_file_new_for_path (expanded);

  ide_template_input_set_directory (self->input, directory);
}

static void
input_notify_cb (GbpCreateProjectWidget *self,
                 GParamSpec             *pspec,
                 IdeTemplateInput       *input)
{
  IdeTemplateInputValidation flags;

  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));
  g_assert (IDE_IS_TEMPLATE_INPUT (input));

  flags = ide_template_input_validate (input);

#define CHECK_FLAG(FLAG,widget)                         \
  G_STMT_START {                                        \
    if ((flags & IDE_TEMPLATE_INPUT_INVAL_##FLAG) != 0) \
      ADD_ERROR(widget);                                \
    else                                                \
      REMOVE_ERROR(widget);                             \
  } G_STMT_END

  CHECK_FLAG (APP_ID, self->app_id_row);
  CHECK_FLAG (LANGUAGE, self->language_row);
  CHECK_FLAG (LOCATION, self->location_row);
  CHECK_FLAG (NAME, self->name_row);
  CHECK_FLAG (TEMPLATE, self->template_row);

#undef CHECK_FLAG

  if ((flags & IDE_TEMPLATE_INPUT_INVAL_LOCATION) &&
      !(flags & IDE_TEMPLATE_INPUT_INVAL_NAME))
    {
      ADD_ERROR (self->name_row);
      gtk_widget_show (GTK_WIDGET (self->directory_clash));
    }
  else
    {
      gtk_widget_hide (GTK_WIDGET (self->directory_clash));
    }

  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "create-project.expand",
                                 flags == IDE_TEMPLATE_INPUT_VALID);
}

static void
select_folder_response_cb (GbpCreateProjectWidget *self,
                           int                     response_id,
                           GtkFileChooserNative   *native)
{
  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));
  g_assert (GTK_IS_FILE_CHOOSER_NATIVE (native));

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      g_autoptr(GFile) file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));
      g_autofree char *path = ide_path_collapse (g_file_peek_path (file));

      gtk_editable_set_text (GTK_EDITABLE (self->location_row), path);
    }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

static void
select_folder_action (GtkWidget  *widget,
                      const char *action_name,
                      GVariant   *param)
{
  GbpCreateProjectWidget *self = (GbpCreateProjectWidget *)widget;
  GtkFileChooserNative *native;
  GtkRoot *root;

  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));

  root = gtk_widget_get_root (widget);
  native = gtk_file_chooser_native_new (_("Select Location"),
                                        GTK_WINDOW (root),
                                        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                        _("Select"),
                                        _("Cancel"));
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (native),
                                       ide_template_input_get_directory (self->input),
                                       NULL);
  g_signal_connect_object (native,
                           "response",
                           G_CALLBACK (select_folder_response_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
gbp_create_project_widget_expand_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  IdeTemplateInput *input = (IdeTemplateInput *)object;
  g_autoptr(IdeGreeterWorkspace) greeter = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) directory = NULL;

  IDE_ENTRY;

  g_assert (IDE_IS_TEMPLATE_INPUT (input));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (IDE_IS_GREETER_WORKSPACE (greeter));

  if (!(directory = ide_template_input_expand_finish (input, result, &error)))
    {
      /* Make sure it wasn't closed/cancelled */
      if (gtk_widget_get_visible (GTK_WIDGET (greeter)))
        {
          GtkWidget *dialog;

          dialog = gtk_message_dialog_new (GTK_WINDOW (greeter),
                                           (GTK_DIALOG_MODAL |
                                            GTK_DIALOG_DESTROY_WITH_PARENT |
                                            GTK_DIALOG_USE_HEADER_BAR),
                                           GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_CLOSE,
                                           "%s",
                                           _("New Project Creation Failed"));
          gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                    "%s", error->message);
          g_signal_connect (dialog,
                            "response",
                            G_CALLBACK (gtk_window_destroy),
                            NULL);
          gtk_window_present (GTK_WINDOW (dialog));
        }
    }
  else
    {
      g_autoptr(IdeProjectInfo) project_info = NULL;

      project_info = ide_project_info_new ();
      ide_project_info_set_file (project_info, directory);
      ide_project_info_set_directory (project_info, directory);

      ide_greeter_workspace_open_project (greeter, project_info);
    }

  ide_greeter_workspace_end (greeter);

  IDE_EXIT;
}

static void
expand_action (GtkWidget  *widget,
               const char *action_name,
               GVariant   *param)
{
  GbpCreateProjectWidget *self = (GbpCreateProjectWidget *)widget;
  IdeGreeterWorkspace *greeter;

  IDE_ENTRY;

  g_assert (GBP_IS_CREATE_PROJECT_WIDGET (self));

  greeter = IDE_GREETER_WORKSPACE (ide_widget_get_workspace (widget));
  ide_greeter_workspace_begin (greeter);

  gtk_widget_action_set_enabled (widget, "create-project.expand", FALSE);
  ide_template_input_expand_async (self->input,
                                   ide_workspace_get_cancellable (IDE_WORKSPACE (greeter)),
                                   gbp_create_project_widget_expand_cb,
                                   g_object_ref (greeter));

  IDE_EXIT;
}

static void
gbp_create_project_widget_dispose (GObject *object)
{
  GbpCreateProjectWidget *self = (GbpCreateProjectWidget *)object;

  g_clear_pointer (&self->main, gtk_widget_unparent);

  G_OBJECT_CLASS (gbp_create_project_widget_parent_class)->dispose (object);
}

static void
gbp_create_project_widget_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  GbpCreateProjectWidget *self = GBP_CREATE_PROJECT_WIDGET (object);

  switch (prop_id)
    {
    case PROP_IS_READY:
      g_value_set_boolean (value, gbp_create_project_widget_check_ready (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gbp_create_project_widget_class_init (GbpCreateProjectWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = gbp_create_project_widget_dispose;
  object_class->get_property = gbp_create_project_widget_get_property;

  properties [PROP_IS_READY] =
    g_param_spec_boolean ("is-ready", NULL, NULL, FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/plugins/create-project/gbp-create-project-widget.ui");

  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, app_id_row);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, directory_clash);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, input);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, language_button);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, language_row);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, licenses_button);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, location_row);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, main);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, name_row);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, template_button);
  gtk_widget_class_bind_template_child (widget_class, GbpCreateProjectWidget, template_row);

  gtk_widget_class_bind_template_callback (widget_class, template_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, language_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, license_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, location_row_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, input_notify_cb);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);

  gtk_widget_class_install_action (widget_class, "create-project.select-folder", NULL, select_folder_action);
  gtk_widget_class_install_action (widget_class, "create-project.expand", NULL, expand_action);

  g_type_ensure (IDE_TYPE_TEMPLATE_INPUT);
}

static void
gbp_create_project_widget_init (GbpCreateProjectWidget *self)
{
  g_autofree char *projects_dir = ide_path_collapse (ide_get_projects_dir ());

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_editable_set_text (GTK_EDITABLE (self->location_row), projects_dir);

  /* Always start disabled */
  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "create-project.expand",
                                 FALSE);
}
