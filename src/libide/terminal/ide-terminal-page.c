/* ide-terminal-page.c
 *
 * Copyright 2015-2019 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "ide-terminal-page"

#include "config.h"

#include <fcntl.h>
#include <glib/gi18n.h>
#include <libide-foundry.h>
#include <libide-gui.h>
#include <libide-terminal.h>
#include <stdlib.h>
#include <vte/vte.h>
#include <unistd.h>

#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

#include "ide-terminal-page.h"
#include "ide-terminal-page-private.h"
#include "ide-terminal-page-actions.h"

#define FLAPPING_DURATION_USEC (G_USEC_PER_SEC / 20)

G_DEFINE_TYPE (IdeTerminalPage, ide_terminal_page, IDE_TYPE_PAGE)

enum {
  PROP_0,
  PROP_LAUNCHER,
  PROP_MANAGE_SPAWN,
  PROP_RESPAWN_ON_EXIT,
  PROP_PTY,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void ide_terminal_page_connect_terminal (IdeTerminalPage *self,
                                                VteTerminal     *terminal);

static gboolean
terminal_has_notification_signal (void)
{
  GQuark quark;
  guint signal_id;

  return g_signal_parse_name ("notification-received",
                              VTE_TYPE_TERMINAL,
                              &signal_id,
                              &quark,
                              FALSE);
}

static void
ide_terminal_page_spawn_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  IdeTerminalLauncher *launcher = (IdeTerminalLauncher *)object;
  g_autoptr(IdeTerminalPage) self = user_data;
  g_autoptr(GError) error = NULL;
  gint64 now;

  g_assert (IDE_IS_TERMINAL_LAUNCHER (launcher));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (IDE_IS_TERMINAL_PAGE (self));

  if (!ide_terminal_launcher_spawn_finish (launcher, result, &error))
    {
      g_autofree gchar *format = NULL;

      format = g_strdup_printf ("%s: %s", _("Subprocess launcher failed"), error->message);
      ide_terminal_page_feed (self, format);
    }

  if (gtk_widget_in_destruction (GTK_WIDGET (self)))
    return;

  now = g_get_monotonic_time ();

  if (ABS (now - self->last_respawn) < FLAPPING_DURATION_USEC)
    {
      ide_terminal_page_feed (self, _("Subprocess launcher failed to quickly, will not respawn."));
      return;
    }

  if (!self->respawn_on_exit)
    {
      gtk_widget_destroy (GTK_WIDGET (self));
      return;
    }

  g_clear_object (&self->pty);
  vte_terminal_reset (VTE_TERMINAL (self->terminal_top), TRUE, TRUE);
  self->pty = vte_pty_new_sync (VTE_PTY_DEFAULT, NULL, NULL);
  vte_terminal_set_pty (VTE_TERMINAL (self->terminal_top), self->pty);

  /* Spawn our terminal and wait for it to exit */
  self->last_respawn = now;
  ide_terminal_launcher_spawn_async (self->launcher,
                                     self->pty,
                                     NULL,
                                     ide_terminal_page_spawn_cb,
                                     g_object_ref (self));
}

static void
gbp_terminal_page_realize (GtkWidget *widget)
{
  IdeTerminalPage *self = (IdeTerminalPage *)widget;

  g_assert (IDE_IS_TERMINAL_PAGE (self));

  GTK_WIDGET_CLASS (ide_terminal_page_parent_class)->realize (widget);

  if (self->did_defered_setup_in_realize)
    return;

  self->did_defered_setup_in_realize = TRUE;

  self->last_respawn = g_get_monotonic_time ();

  if (self->pty == NULL)
    {
      g_autoptr(GError) error = NULL;

      if (!(self->pty = vte_pty_new_sync (VTE_PTY_DEFAULT, NULL, &error)))
        {
          g_critical ("Failed to create PTY for terminal: %s", error->message);
          return;
        }
    }

  vte_terminal_set_pty (VTE_TERMINAL (self->terminal_top), self->pty);

  if (!self->manage_spawn)
    return;

  /* Spawn our terminal and wait for it to exit */
  ide_terminal_launcher_spawn_async (self->launcher,
                                     self->pty,
                                     NULL,
                                     ide_terminal_page_spawn_cb,
                                     g_object_ref (self));
}

static void
gbp_terminal_page_get_preferred_width (GtkWidget *widget,
                                       gint      *min_width,
                                       gint      *nat_width)
{
  /*
   * Since we are placing the terminal in a GtkStack, we need
   * to fake the size a bit. Otherwise, GtkStack tries to keep the
   * widget at its natural size (which prevents us from getting
   * appropriate size requests.
   */
  GTK_WIDGET_CLASS (ide_terminal_page_parent_class)->get_preferred_width (widget, min_width, nat_width);
  *nat_width = *min_width;
}

static void
gbp_terminal_page_get_preferred_height (GtkWidget *widget,
                                        gint      *min_height,
                                        gint      *nat_height)
{
  /*
   * Since we are placing the terminal in a GtkStack, we need
   * to fake the size a bit. Otherwise, GtkStack tries to keep the
   * widget at its natural size (which prevents us from getting
   * appropriate size requests.
   */
  GTK_WIDGET_CLASS (ide_terminal_page_parent_class)->get_preferred_height (widget, min_height, nat_height);
  *nat_height = *min_height;
}

static void
gbp_terminal_page_set_needs_attention (IdeTerminalPage *self,
                                       gboolean         needs_attention)
{
  GtkWidget *parent;

  g_assert (IDE_IS_TERMINAL_PAGE (self));

  parent = gtk_widget_get_parent (GTK_WIDGET (self));

  if (GTK_IS_STACK (parent) &&
      !gtk_widget_in_destruction (GTK_WIDGET (self)) &&
      !gtk_widget_in_destruction (parent))
    {
      if (!gtk_widget_in_destruction (GTK_WIDGET (self->terminal_top)))
        self->needs_attention = !!needs_attention;

      gtk_container_child_set (GTK_CONTAINER (parent), GTK_WIDGET (self),
                               "needs-attention", needs_attention,
                               NULL);
    }
}

static void
notification_received_cb (VteTerminal     *terminal,
                          const gchar     *summary,
                          const gchar     *body,
                          IdeTerminalPage *self)
{
  g_assert (VTE_IS_TERMINAL (terminal));
  g_assert (IDE_IS_TERMINAL_PAGE (self));

  if (!gtk_widget_has_focus (GTK_WIDGET (terminal)))
    gbp_terminal_page_set_needs_attention (self, TRUE);
}

static gboolean
focus_in_event_cb (VteTerminal     *terminal,
                   GdkEvent        *event,
                   IdeTerminalPage *self)
{
  g_assert (VTE_IS_TERMINAL (terminal));
  g_assert (IDE_IS_TERMINAL_PAGE (self));

  self->needs_attention = FALSE;
  gbp_terminal_page_set_needs_attention (self, FALSE);
  gtk_revealer_set_reveal_child (self->search_revealer_top, FALSE);

  return GDK_EVENT_PROPAGATE;
}

static void
window_title_changed_cb (VteTerminal     *terminal,
                         IdeTerminalPage *self)
{
  const gchar *title;

  g_assert (VTE_IS_TERMINAL (terminal));
  g_assert (IDE_IS_TERMINAL_PAGE (self));

  title = vte_terminal_get_window_title (VTE_TERMINAL (self->terminal_top));

  if (title == NULL || title[0] == '\0')
    title = _("Untitled terminal");

  ide_page_set_title (IDE_PAGE (self), title);
}

static void
style_context_changed (GtkStyleContext *style_context,
                       IdeTerminalPage *self)
{
  GtkStateFlags state;
  GdkRGBA fg;
  GdkRGBA bg;

  g_assert (GTK_IS_STYLE_CONTEXT (style_context));
  g_assert (IDE_IS_TERMINAL_PAGE (self));

  state = gtk_style_context_get_state (style_context);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
  gtk_style_context_get_color (style_context, state, &fg);
  gtk_style_context_get_background_color (style_context, state, &bg);
  G_GNUC_END_IGNORE_DEPRECATIONS;

  if (bg.alpha == 0.0)
    gdk_rgba_parse (&bg, "#f6f7f8");

  ide_page_set_primary_color_fg (IDE_PAGE (self), &fg);
  ide_page_set_primary_color_bg (IDE_PAGE (self), &bg);
}

static IdePage *
gbp_terminal_page_create_split (IdePage *page)
{
  g_assert (IDE_IS_TERMINAL_PAGE (page));

  return g_object_new (IDE_TYPE_TERMINAL_PAGE,
                       "visible", TRUE,
                       NULL);
}

static void
gbp_terminal_page_grab_focus (GtkWidget *widget)
{
  IdeTerminalPage *self = (IdeTerminalPage *)widget;

  g_assert (IDE_IS_TERMINAL_PAGE (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->terminal_top));
}

static void
ide_terminal_page_connect_terminal (IdeTerminalPage *self,
                                    VteTerminal     *terminal)
{
  GtkAdjustment *vadj;

  vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (terminal));

  gtk_range_set_adjustment (GTK_RANGE (self->top_scrollbar), vadj);

  g_signal_connect_object (terminal,
                           "focus-in-event",
                           G_CALLBACK (focus_in_event_cb),
                           self,
                           0);

  g_signal_connect_object (terminal,
                           "window-title-changed",
                           G_CALLBACK (window_title_changed_cb),
                           self,
                           0);

  if (terminal_has_notification_signal ())
    {
      g_signal_connect_object (terminal,
                               "notification-received",
                               G_CALLBACK (notification_received_cb),
                               self,
                               0);
    }
}

static void
ide_terminal_page_context_set (GtkWidget  *widget,
                               IdeContext *context)
{
  IdeTerminalPage *self = (IdeTerminalPage *)widget;

  g_assert (IDE_IS_TERMINAL_PAGE (self));
  g_assert (!context || IDE_IS_CONTEXT (context));

  if (self->launcher == NULL && context != NULL)
    self->launcher = ide_terminal_launcher_new (context);
}

static void
ide_terminal_page_finalize (GObject *object)
{
  IdeTerminalPage *self = IDE_TERMINAL_PAGE (object);

  g_clear_object (&self->launcher);
  g_clear_object (&self->save_as_file_top);
  g_clear_pointer (&self->selection_buffer, g_free);
  g_clear_object (&self->pty);

  G_OBJECT_CLASS (ide_terminal_page_parent_class)->finalize (object);
}

static void
ide_terminal_page_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  IdeTerminalPage *self = IDE_TERMINAL_PAGE (object);

  switch (prop_id)
    {
    case PROP_LAUNCHER:
      g_value_set_object (value, self->launcher);
      break;

    case PROP_MANAGE_SPAWN:
      g_value_set_boolean (value, self->manage_spawn);
      break;

    case PROP_PTY:
      g_value_set_object (value, self->pty);
      break;

    case PROP_RESPAWN_ON_EXIT:
      g_value_set_boolean (value, self->respawn_on_exit);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_terminal_page_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  IdeTerminalPage *self = IDE_TERMINAL_PAGE (object);

  switch (prop_id)
    {
    case PROP_MANAGE_SPAWN:
      self->manage_spawn = g_value_get_boolean (value);
      break;

    case PROP_PTY:
      self->pty = g_value_dup_object (value);
      break;

    case PROP_RESPAWN_ON_EXIT:
      self->respawn_on_exit = g_value_get_boolean (value);
      break;

    case PROP_LAUNCHER:
      ide_terminal_page_set_launcher (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_terminal_page_class_init (IdeTerminalPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  IdePageClass *page_class = IDE_PAGE_CLASS (klass);

  object_class->finalize = ide_terminal_page_finalize;
  object_class->get_property = ide_terminal_page_get_property;
  object_class->set_property = ide_terminal_page_set_property;

  widget_class->realize = gbp_terminal_page_realize;
  widget_class->get_preferred_width = gbp_terminal_page_get_preferred_width;
  widget_class->get_preferred_height = gbp_terminal_page_get_preferred_height;
  widget_class->grab_focus = gbp_terminal_page_grab_focus;

  page_class->create_split = gbp_terminal_page_create_split;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/libide-terminal/ui/ide-terminal-page.ui");
  gtk_widget_class_bind_template_child (widget_class, IdeTerminalPage, terminal_top);
  gtk_widget_class_bind_template_child (widget_class, IdeTerminalPage, top_scrollbar);
  gtk_widget_class_bind_template_child (widget_class, IdeTerminalPage, terminal_overlay_top);

  properties [PROP_MANAGE_SPAWN] =
    g_param_spec_boolean ("manage-spawn",
                          "Manage Spawn",
                          "Manage Spawn",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_RESPAWN_ON_EXIT] =
    g_param_spec_boolean ("respawn-on-exit",
                          "Respawn on Exit",
                          "Respawn on Exit",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_PTY] =
    g_param_spec_object ("pty",
                         "Pty",
                         "The pseudo terminal to use",
                         VTE_TYPE_PTY,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_LAUNCHER] =
    g_param_spec_object ("launcher",
                         "Launcher",
                         "The launcher to use for spawning",
                         IDE_TYPE_TERMINAL_LAUNCHER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
ide_terminal_page_init (IdeTerminalPage *self)
{
  GtkStyleContext *style_context;

  self->respawn_on_exit = TRUE;
  self->manage_spawn = TRUE;

  self->tsearch = g_object_new (IDE_TYPE_TERMINAL_SEARCH,
                                "visible", TRUE,
                                NULL);
  self->search_revealer_top = ide_terminal_search_get_revealer (self->tsearch);

  gtk_widget_init_template (GTK_WIDGET (self));

  ide_page_set_icon_name (IDE_PAGE (self), "utilities-terminal-symbolic");
  ide_page_set_can_split (IDE_PAGE (self), TRUE);
  ide_page_set_menu_id (IDE_PAGE (self), "terminal-page-document-menu");

  gtk_overlay_add_overlay (self->terminal_overlay_top, GTK_WIDGET (self->tsearch));

  ide_terminal_page_connect_terminal (self, VTE_TERMINAL (self->terminal_top));

  ide_terminal_search_set_terminal (self->tsearch, VTE_TERMINAL (self->terminal_top));

  ide_terminal_page_actions_init (self);

  style_context = gtk_widget_get_style_context (GTK_WIDGET (self->terminal_top));
  gtk_style_context_add_class (style_context, "terminal");
  g_signal_connect_object (style_context,
                           "changed",
                           G_CALLBACK (style_context_changed),
                           self,
                           0);
  style_context_changed (style_context, self);

  gtk_widget_set_can_focus (GTK_WIDGET (self->terminal_top), TRUE);

  ide_widget_set_context_handler (self, ide_terminal_page_context_set);
}

void
ide_terminal_page_set_pty (IdeTerminalPage *self,
                           VtePty          *pty)
{
  g_return_if_fail (IDE_IS_TERMINAL_PAGE (self));
  g_return_if_fail (VTE_IS_PTY (pty));

  if (g_set_object (&self->pty, pty))
    {
      vte_terminal_reset (VTE_TERMINAL (self->terminal_top), TRUE, TRUE);
      vte_terminal_set_pty (VTE_TERMINAL (self->terminal_top), pty);
    }
}

void
ide_terminal_page_feed (IdeTerminalPage *self,
                        const gchar     *message)
{
  g_return_if_fail (IDE_IS_TERMINAL_PAGE (self));

  if (self->terminal_top != NULL)
    vte_terminal_feed (VTE_TERMINAL (self->terminal_top), message, -1);
}

void
ide_terminal_page_set_launcher (IdeTerminalPage     *self,
                                IdeTerminalLauncher *launcher)
{
  g_return_if_fail (IDE_IS_TERMINAL_PAGE (self));
  g_return_if_fail (!launcher || IDE_IS_TERMINAL_LAUNCHER (launcher));

  if (g_set_object (&self->launcher, launcher))
    {
      if (launcher != NULL)
        {
          const gchar *title = ide_terminal_launcher_get_title (launcher);
          ide_page_set_title (IDE_PAGE (self), title);
        }
      else
        {
          self->manage_spawn = FALSE;
        }
    }
}

const gchar *
ide_terminal_page_get_current_directory_uri (IdeTerminalPage *self)
{
  g_return_val_if_fail (IDE_IS_TERMINAL_PAGE (self), NULL);

  return vte_terminal_get_current_directory_uri (VTE_TERMINAL (self->terminal_top));
}
