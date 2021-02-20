/* ide-lsp-search-provider.c
 *
 * Copyright 2020 Günther Wagner <info@gunibert.de>
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

#include "config.h"

#define G_LOG_DOMAIN "ide-lsp-search-provider"

#include <libide-search.h>
#include <jsonrpc-glib.h>

#include "ide-lsp-client.h"
#include "ide-lsp-util.h"
#include "ide-lsp-search-provider.h"
#include "ide-lsp-search-result.h"

typedef struct
{
  IdeLspClient *client;
} IdeLspSearchProviderPrivate;

static void provider_iface_init (IdeSearchProviderInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (IdeLspSearchProvider, ide_lsp_search_provider, IDE_TYPE_OBJECT,
                                  G_ADD_PRIVATE (IdeLspSearchProvider)
                                  G_IMPLEMENT_INTERFACE (IDE_TYPE_SEARCH_PROVIDER, provider_iface_init))

enum {
  PROP_0,
  PROP_CLIENT,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * ide_lsp_search_provider_get_client:
 * @self: An #IdeLspSearchProvider
 *
 * Gets the client for the search provider.
 *
 * Returns: (transfer none) (nullable): An #IdeLspClient or %NULL
 */
IdeLspClient *
ide_lsp_search_provider_get_client (IdeLspSearchProvider *self)
{
  IdeLspSearchProviderPrivate *priv = ide_lsp_search_provider_get_instance_private (self);

  g_return_val_if_fail (IDE_IS_LSP_SEARCH_PROVIDER (self), NULL);

  return priv->client;
}

void
ide_lsp_search_provider_set_client (IdeLspSearchProvider *self,
                                    IdeLspClient               *client)
{
  IdeLspSearchProviderPrivate *priv = ide_lsp_search_provider_get_instance_private (self);

  g_return_if_fail (IDE_IS_LSP_SEARCH_PROVIDER (self));
  g_return_if_fail (!client || IDE_IS_LSP_CLIENT (client));

  if (priv->client != NULL)
    g_clear_pointer (&priv->client, g_object_unref);
  priv->client = g_object_ref (client);
}

static void
ide_lsp_search_provider_finalize (GObject *object)
{
  IdeLspSearchProvider *self = (IdeLspSearchProvider *)object;
  IdeLspSearchProviderPrivate *priv = ide_lsp_search_provider_get_instance_private (self);

  g_clear_object (&priv->client);

  G_OBJECT_CLASS (ide_lsp_search_provider_parent_class)->finalize (object);
}

static void
ide_lsp_search_provider_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  IdeLspSearchProvider *self = IDE_LSP_SEARCH_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, ide_lsp_search_provider_get_client (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_lsp_search_provider_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  IdeLspSearchProvider *self = IDE_LSP_SEARCH_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      ide_lsp_search_provider_set_client (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_lsp_search_provider_class_init (IdeLspSearchProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_lsp_search_provider_finalize;
  object_class->get_property = ide_lsp_search_provider_get_property;
  object_class->set_property = ide_lsp_search_provider_set_property;

  properties[PROP_CLIENT] =
    g_param_spec_object ("client",
                         "client",
                         "The Language Server client",
                         IDE_TYPE_LSP_CLIENT,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
ide_lsp_search_provider_init (IdeLspSearchProvider *self)
{
}

static void
ide_lsp_search_provider_search_cb (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  IdeLspClient *client = (IdeLspClient *) source_object;
  GPtrArray *ar;
  g_autoptr(IdeTask) task = user_data;
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GVariantIter) iter = NULL;
  GVariant *symbol_information;
  GError *error = NULL;

  IDE_ENTRY;

  ar = g_ptr_array_new ();

  ide_lsp_client_call_finish (client, res, &result, &error);
  if (error != NULL)
    {
      ide_task_return_error (task, error);
      return;
    }

  iter = g_variant_iter_new (result);

  while (g_variant_iter_loop (iter, "v", &symbol_information))
    {
      g_autoptr(GFile) gfile = NULL;
      g_autoptr(IdeLocation) location = NULL;
      const gchar *title;
      const gchar *uri;
      gint64 kind;
      gint64 line, character;
      IdeSymbolKind symbol_kind;
      const gchar *icon_name;

      JSONRPC_MESSAGE_PARSE (symbol_information,
                             "name", JSONRPC_MESSAGE_GET_STRING (&title),
                             "kind", JSONRPC_MESSAGE_GET_INT64 (&kind),
                             "location", "{",
                               "uri", JSONRPC_MESSAGE_GET_STRING (&uri),
                               "range", "{",
                                 "start", "{",
                                   "line", JSONRPC_MESSAGE_GET_INT64 (&line),
                                   "character", JSONRPC_MESSAGE_GET_INT64 (&character),
                                 "}",
                               "}",
                             "}");

      symbol_kind = ide_lsp_decode_symbol_kind (kind);
      icon_name = ide_symbol_kind_get_icon_name (symbol_kind);

      gfile = g_file_new_for_uri (uri);
      location = ide_location_new (gfile, line, character);

      g_ptr_array_add (ar, ide_lsp_search_result_new (title, g_file_get_basename (gfile), location, icon_name));
    }

  ide_task_return_pointer (task,
                           g_steal_pointer(&ar),
                           g_ptr_array_unref);

  IDE_EXIT;
}

static void
ide_lsp_search_provider_search_async (IdeSearchProvider   *provider,
                                      const gchar         *query,
                                      guint                max_results,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  IdeLspSearchProvider *self = (IdeLspSearchProvider *)provider;
  IdeLspSearchProviderPrivate *priv = ide_lsp_search_provider_get_instance_private (self);
  g_autoptr(IdeTask) task = NULL;
  g_autoptr(GVariant) params = NULL;

  IDE_ENTRY;

  g_return_if_fail (IDE_IS_MAIN_THREAD ());
  g_return_if_fail (IDE_IS_LSP_SEARCH_PROVIDER (self));
  g_return_if_fail (query != NULL);
  g_assert (priv->client != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = ide_task_new (self, cancellable, callback, user_data);
  ide_task_set_source_tag (task, ide_lsp_search_provider_search_async);

  params = JSONRPC_MESSAGE_NEW ("query", JSONRPC_MESSAGE_PUT_STRING (query));

  ide_lsp_client_call_async (priv->client,
                             "workspace/symbol",
                             params,
                             cancellable,
                             ide_lsp_search_provider_search_cb,
                             g_steal_pointer (&task));

  IDE_EXIT;
}

static GPtrArray *
ide_lsp_search_provider_search_finish (IdeSearchProvider  *provider,
                                       GAsyncResult       *result,
                                       GError            **error)
{
  return ide_task_propagate_pointer (IDE_TASK (result), error);
}

static void
provider_iface_init (IdeSearchProviderInterface *iface)
{
  iface->search_async = ide_lsp_search_provider_search_async;
  iface->search_finish = ide_lsp_search_provider_search_finish;
}
