/* ide-source-view-private.h
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

#pragma once

#include <libide-code.h>
#include <libide-gtk.h>
#include <libide-plugins.h>

#include "ide-source-view.h"

G_BEGIN_DECLS

struct _IdeSourceView
{
  GtkSourceView source_view;

  /* The document (same as get_buffer()) but gives us a pointer
   * to see our old value when notify::buffer is emitted.
   */
  IdeBuffer *buffer;

  /* These are used to generate custom CSS based on the font
   * description which is also used to scale the contents
   * in response to user zoom setting. The line-height contains
   * our setting for additional padding beyond what the font
   * itself will give us.
   */
  GtkCssProvider *css_provider;
  PangoFontDescription *font_desc;
  double line_height;
  int font_scale;

  /* This is a joined menu used to extend the GtkTextView
   * "extra-menu" property. We join things here and allow
   * addins to extend it.
   */
  IdeJoinedMenu *joined_menu;

  /* Various addins for different ways of extending the
   * GtkSourceView. These are managed in ide-source-view-addins.c
   * to load/unload/change-language in response to buffer changes.
   */
  IdeExtensionSetAdapter *completion_providers;
  IdeExtensionSetAdapter *hover_providers;
};


void _ide_source_view_addins_init         (IdeSourceView     *self,
                                           GtkSourceLanguage *language);
void _ide_source_view_addins_shutdown     (IdeSourceView     *self);
void _ide_source_view_addins_set_language (IdeSourceView     *self,
                                           GtkSourceLanguage *language);

G_END_DECLS