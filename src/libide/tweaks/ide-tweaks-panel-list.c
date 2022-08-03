/* ide-tweaks-panel-list.c
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

#define G_LOG_DOMAIN "ide-tweaks-panel-list"

#include "config.h"

#include "ide-tweaks-factory-private.h"
#include "ide-tweaks-model-private.h"
#include "ide-tweaks-page.h"
#include "ide-tweaks-panel-list-private.h"
#include "ide-tweaks-panel-list-row-private.h"
#include "ide-tweaks-section.h"

struct _IdeTweaksPanelList
{
  AdwBin         parent_instance;

  IdeTweaksItem *item;

  GtkListBox    *list_box;
};

enum {
  PROP_0,
  PROP_ITEM,
  N_PROPS
};

enum {
  PAGE_ACTIVATED,
  N_SIGNALS
};

G_DEFINE_FINAL_TYPE (IdeTweaksPanelList, ide_tweaks_panel_list, ADW_TYPE_BIN)

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static void
ide_tweaks_panel_list_header_func (IdeTweaksPanelListRow *row,
                                   IdeTweaksPanelListRow *before,
                                   gpointer               user_data)
{
  GtkWidget *header = NULL;
  IdeTweaksItem *before_item = NULL;
  IdeTweaksItem *row_item;

  g_assert (!before || IDE_IS_TWEAKS_PANEL_LIST_ROW (before));
  g_assert (IDE_IS_TWEAKS_PANEL_LIST_ROW (row));

  if (before != NULL)
    before_item = ide_tweaks_panel_list_row_get_item (before);
  row_item = ide_tweaks_panel_list_row_get_item (row);

  if (IDE_IS_TWEAKS_PAGE (before_item) &&
      IDE_IS_TWEAKS_PAGE (row_item) &&
      ide_tweaks_page_get_section (IDE_TWEAKS_PAGE (before_item)) !=
      ide_tweaks_page_get_section (IDE_TWEAKS_PAGE (row_item)))
    header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);

  gtk_list_box_row_set_header (GTK_LIST_BOX_ROW (row), header);
}

static void
ide_tweaks_panel_list_row_activated_cb (IdeTweaksPanelList    *self,
                                        IdeTweaksPanelListRow *row,
                                        GtkListBox            *list_box)
{
  IdeTweaksItem *page;

  g_assert (IDE_IS_TWEAKS_PANEL_LIST (self));
  g_assert (IDE_IS_TWEAKS_PANEL_LIST_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  page = ide_tweaks_panel_list_row_get_item (row);

  g_assert (page != NULL);
  g_assert (IDE_IS_TWEAKS_PAGE (page));

  g_signal_emit (self, signals [PAGE_ACTIVATED], 0, page);
}

static void
ide_tweaks_panel_list_dispose (GObject *object)
{
  IdeTweaksPanelList *self = (IdeTweaksPanelList *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (ide_tweaks_panel_list_parent_class)->dispose (object);
}

static void
ide_tweaks_panel_list_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  IdeTweaksPanelList *self = IDE_TWEAKS_PANEL_LIST (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, ide_tweaks_panel_list_get_item (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_tweaks_panel_list_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  IdeTweaksPanelList *self = IDE_TWEAKS_PANEL_LIST (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      ide_tweaks_panel_list_set_item (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_tweaks_panel_list_class_init (IdeTweaksPanelListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ide_tweaks_panel_list_dispose;
  object_class->get_property = ide_tweaks_panel_list_get_property;
  object_class->set_property = ide_tweaks_panel_list_set_property;

  properties [PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         IDE_TYPE_TWEAKS_ITEM,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/libide-tweaks/ide-tweaks-panel-list.ui");
  gtk_widget_class_bind_template_child (widget_class, IdeTweaksPanelList, list_box);
  gtk_widget_class_bind_template_callback (widget_class, ide_tweaks_panel_list_row_activated_cb);

  signals [PAGE_ACTIVATED] =
    g_signal_new ("page-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 1, IDE_TYPE_TWEAKS_PAGE);
}

static void
ide_tweaks_panel_list_init (IdeTweaksPanelList *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_list_box_set_header_func (self->list_box,
                                (GtkListBoxUpdateHeaderFunc)ide_tweaks_panel_list_header_func,
                                NULL, NULL);
}

GtkWidget *
ide_tweaks_panel_list_new (void)
{
  return g_object_new (IDE_TYPE_TWEAKS_PANEL_LIST, NULL);
}

/**
 * ide_tweaks_panel_list_get_item:
 *
 * Gets the parent item of the panel list. Children of this
 * item are what are displayed in the panel list.
 *
 * Returns: (transfer none) (nullable): an #IdeTweaksItem or %NULL
 */
IdeTweaksItem *
ide_tweaks_panel_list_get_item (IdeTweaksPanelList *self)
{
  g_return_val_if_fail (IDE_IS_TWEAKS_PANEL_LIST (self), NULL);

  return self->item;
}

static GtkWidget *
ide_tweaks_panel_list_create_row_cb (gpointer item,
                                     gpointer user_data)
{
  return g_object_new (IDE_TYPE_TWEAKS_PANEL_LIST_ROW,
                       "item", item,
                       NULL);
}

static IdeTweaksItemVisitResult
panel_list_visitor (IdeTweaksItem *item,
                    gpointer       user_data)
{
  static GType page_type;

  if (!page_type)
    page_type = IDE_TYPE_TWEAKS_PAGE;

  if (IDE_IS_TWEAKS_SECTION (item))
    return IDE_TWEAKS_ITEM_VISIT_RECURSE;

  if (IDE_IS_TWEAKS_PAGE (item))
    return IDE_TWEAKS_ITEM_VISIT_ACCEPT;

  if (IDE_IS_TWEAKS_FACTORY (item) &&
      _ide_tweaks_factory_is_one_of (IDE_TWEAKS_FACTORY (item), &page_type, 1))
    return IDE_TWEAKS_ITEM_VISIT_ACCEPT;

  return IDE_TWEAKS_ITEM_VISIT_SKIP;
}

void
ide_tweaks_panel_list_set_item (IdeTweaksPanelList *self,
                                IdeTweaksItem      *item)
{
  g_return_if_fail (IDE_IS_TWEAKS_PANEL_LIST (self));
  g_return_if_fail (IDE_IS_TWEAKS_ITEM (item));

  if (g_set_object (&self->item, item))
    {
      g_autoptr(IdeTweaksModel) model = NULL;

      if (item != NULL)
        {
          model = ide_tweaks_model_new (item, panel_list_visitor, NULL, NULL);
          gtk_list_box_bind_model (self->list_box,
                                   G_LIST_MODEL (model),
                                   ide_tweaks_panel_list_create_row_cb,
                                   NULL, NULL);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ITEM]);
    }
}

void
ide_tweaks_panel_list_select_first (IdeTweaksPanelList *self)
{
  g_return_if_fail (IDE_IS_TWEAKS_PANEL_LIST (self));

  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->list_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (GTK_IS_LIST_BOX_ROW (child))
        {
          gtk_widget_activate (GTK_WIDGET (child));
          break;
        }
    }
}
