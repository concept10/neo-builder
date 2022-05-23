/* gbp-meson-run-command-provider.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "gbp-meson-run-command-provider"

#include <libide-threading.h>

#include "gbp-meson-run-command-provider.h"

struct _GbpMesonRunCommandProvider
{
  IdeObject parent_instance;
};

static void
gbp_meson_run_command_provider_list_commands_async (IdeRunCommandProvider *provider,
                                                    GCancellable          *cancellable,
                                                    GAsyncReadyCallback    callback,
                                                    gpointer               user_data)
{
  GbpMesonRunCommandProvider *self = (GbpMesonRunCommandProvider *)provider;
  g_autoptr(IdeTask) task = NULL;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_COMMAND_PROVIDER (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = ide_task_new (self, cancellable, callback, user_data);
  ide_task_set_source_tag (task, gbp_meson_run_command_provider_list_commands_async);

  ide_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_NOT_SUPPORTED,
                             "Not yet supported");

  IDE_EXIT;
}

static GListModel *
gbp_meson_run_command_provider_list_commands_finish (IdeRunCommandProvider  *provider,
                                                     GAsyncResult           *result,
                                                     GError                **error)
{
  GListModel *ret;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_COMMAND_PROVIDER (provider));
  g_assert (IDE_IS_TASK (result));

  ret = ide_task_propagate_pointer (IDE_TASK (result), error);

  IDE_RETURN (ret);
}

static void
run_command_provider_iface (IdeRunCommandProviderInterface *iface)
{
  iface->list_commands_async = gbp_meson_run_command_provider_list_commands_async;
  iface->list_commands_finish = gbp_meson_run_command_provider_list_commands_finish;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (GbpMesonRunCommandProvider, gbp_meson_run_command_provider, IDE_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (IDE_TYPE_RUN_COMMAND_PROVIDER, run_command_provider_iface))

static void
gbp_meson_run_command_provider_class_init (GbpMesonRunCommandProviderClass *klass)
{
}

static void
gbp_meson_run_command_provider_init (GbpMesonRunCommandProvider *self)
{
}
