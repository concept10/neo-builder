/* ide-editor.c
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

#define G_LOG_DOMAIN "ide-editor"

#include "config.h"

#include <glib/gi18n.h>

#include "ide-editor.h"
#include "ide-editor-page.h"

typedef struct _Focus
{
  IdeWorkspace *workspace;
  IdeFrame     *frame;
  IdeLocation  *location;
  IdeBuffer    *buffer;
  GFile        *file;
} Focus;

static Focus *
focus_new (IdeWorkspace *workspace,
           IdeFrame     *frame,
           IdeBuffer    *buffer,
           IdeLocation  *location)
{
  IdeBufferManager *bufmgr;
  IdeContext *context;
  Focus *focus;
  GFile *file = NULL;

  g_assert (IDE_IS_WORKSPACE (workspace));
  g_assert (!frame || IDE_IS_FRAME (frame));
  g_assert (!buffer || IDE_IS_BUFFER (buffer));
  g_assert (!location || IDE_IS_LOCATION (location));
  g_assert (buffer != NULL || location != NULL);

  context = ide_workspace_get_context (workspace);
  bufmgr = ide_buffer_manager_from_context (context);

  g_assert (IDE_IS_CONTEXT (context));
  g_assert (IDE_IS_BUFFER_MANAGER (bufmgr));

  if (location != NULL)
    file = ide_location_get_file (location);
  else
    file = ide_buffer_get_file (buffer);

  g_assert (buffer != NULL || file != NULL);

  if (buffer == NULL)
    buffer = ide_buffer_manager_find_buffer (bufmgr, file);

  if (frame == NULL)
    frame = ide_workspace_get_most_recent_frame (workspace);

  g_assert (IDE_IS_FRAME (frame));

  focus = g_atomic_rc_box_alloc0 (sizeof *focus);
  g_set_object (&focus->workspace, workspace);
  g_set_object (&focus->frame, frame);
  g_set_object (&focus->buffer, buffer);
  g_set_object (&focus->location, location);
  g_set_object (&focus->file, file);

  return focus;
}

static void
focus_finalize (gpointer data)
{
  Focus *focus = data;

  g_clear_object (&focus->workspace);
  g_clear_object (&focus->location);
  g_clear_object (&focus->buffer);
  g_clear_object (&focus->frame);
  g_clear_object (&focus->file);
}

static void
focus_free (Focus *focus)
{
  g_atomic_rc_box_release_full (focus, focus_finalize);
}

static void
focus_complete (Focus        *focus,
                const GError *error)
{
  g_assert (focus != NULL);
  g_assert (G_IS_FILE (focus->file));
  g_assert (!focus->location || IDE_IS_LOCATION (focus->location));
  g_assert (!focus->buffer || IDE_IS_BUFFER (focus->buffer));
  g_assert (focus->buffer || error != NULL);
  g_assert (IDE_IS_WORKSPACE (focus->workspace));
  g_assert (IDE_IS_FRAME (focus->frame));

  if (error != NULL)
    {
      IdeContext *context = ide_workspace_get_context (focus->workspace);
      ide_context_warning (context,
                           /* translators: %s is replaced with the error message */
                           _("Failed to open file: %s"),
                           error->message);
    }
  else
    {
      guint n_pages = panel_frame_get_n_pages (PANEL_FRAME (focus->frame));
      IdeEditorPage *page = NULL;

      for (guint i = 0; i < n_pages; i++)
        {
          PanelWidget *child = panel_frame_get_page (PANEL_FRAME (focus->frame), i);

          if (IDE_IS_EDITOR_PAGE (child))
            {
              IdeBuffer *buffer = ide_editor_page_get_buffer (IDE_EDITOR_PAGE (child));

              if (buffer == focus->buffer)
                {
                  page = IDE_EDITOR_PAGE (child);
                  break;
                }
            }
        }

      g_assert (!page || IDE_IS_EDITOR_PAGE (page));

      if (page == NULL)
        {
          page = IDE_EDITOR_PAGE (ide_editor_page_new (focus->buffer));
          panel_frame_add (PANEL_FRAME (focus->frame), PANEL_WIDGET (page));
        }

      panel_frame_set_visible_child (PANEL_FRAME (focus->frame), PANEL_WIDGET (page));

      if (focus->location != NULL)
        {
          IdeSourceView *view = ide_editor_page_get_view (page);
          GtkTextIter iter;

          ide_buffer_get_iter_at_location (focus->buffer, &iter, focus->location);
          gtk_text_buffer_select_range (GTK_TEXT_BUFFER (focus->buffer), &iter, &iter);
          ide_source_view_scroll_to_insert (view);
        }
    }

  focus_free (focus);
}

static void
ide_editor_load_file_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  IdeBufferManager *bufmgr = (IdeBufferManager *)object;
  g_autoptr(IdeBuffer) buffer = NULL;
  g_autoptr(GError) error = NULL;
  Focus *focus = user_data;

  g_assert (IDE_IS_BUFFER_MANAGER (bufmgr));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (focus != NULL);
  g_assert (IDE_IS_WORKSPACE (focus->workspace));
  g_assert (IDE_IS_FRAME (focus->frame));
  g_assert (G_IS_FILE (focus->file));

  if ((buffer = ide_buffer_manager_load_file_finish (bufmgr, result, &error)))
    g_set_object (&focus->buffer, buffer);

  focus_complete (focus, error);
}

static void
do_focus (IdeWorkspace *workspace,
          IdeFrame     *frame,
          IdeBuffer    *buffer,
          IdeLocation  *location)
{
  IdeBufferManager *bufmgr;
  IdeContext *context;
  Focus *focus;

  g_assert (IDE_IS_WORKSPACE (workspace));
  g_assert (!frame || IDE_IS_FRAME (frame));
  g_assert (!buffer || IDE_IS_BUFFER (buffer));
  g_assert (!location || IDE_IS_LOCATION (location));
  g_assert (buffer != NULL || location != NULL);

  context = ide_workspace_get_context (workspace);
  bufmgr = ide_buffer_manager_from_context (context);

  g_assert (IDE_IS_CONTEXT (context));
  g_assert (IDE_IS_BUFFER_MANAGER (bufmgr));

  focus = focus_new (workspace, frame, buffer, location);

  if (focus->buffer == NULL)
    ide_buffer_manager_load_file_async (bufmgr,
                                        focus->file,
                                        IDE_BUFFER_OPEN_FLAGS_NONE,
                                        NULL,
                                        ide_workspace_get_cancellable (workspace),
                                        ide_editor_load_file_cb,
                                        focus);
  else
    focus_complete (focus, NULL);
}

void
ide_editor_focus_location (IdeWorkspace *workspace,
                           IdeFrame     *frame,
                           IdeLocation  *location)
{
  g_return_if_fail (IDE_IS_WORKSPACE (workspace));
  g_return_if_fail (!frame || IDE_IS_FRAME (frame));
  g_return_if_fail (!frame || GTK_WIDGET (workspace) == gtk_widget_get_ancestor (GTK_WIDGET (frame), IDE_TYPE_WORKSPACE));
  g_return_if_fail (IDE_IS_LOCATION (location));

  do_focus (workspace, frame, NULL, location);
}

void
ide_editor_focus_buffer (IdeWorkspace *workspace,
                         IdeFrame     *frame,
                         IdeBuffer    *buffer)
{
  g_return_if_fail (IDE_IS_WORKSPACE (workspace));
  g_return_if_fail (!frame || IDE_IS_FRAME (frame));
  g_return_if_fail (!frame || GTK_WIDGET (workspace) == gtk_widget_get_ancestor (GTK_WIDGET (frame), IDE_TYPE_WORKSPACE));
  g_return_if_fail (IDE_IS_BUFFER (buffer));

  do_focus (workspace, frame, buffer, NULL);
}
