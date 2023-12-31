/* ide-ctags-service.h
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

#pragma once

#include <libide-sourceview.h>

#include "ide-ctags-completion-provider.h"
#include "ide-ctags-highlighter.h"

G_BEGIN_DECLS

#define IDE_TYPE_CTAGS_SERVICE (ide_ctags_service_get_type())

G_DECLARE_FINAL_TYPE (IdeCtagsService, ide_ctags_service, IDE, CTAGS_SERVICE, IdeObject)

void       ide_ctags_service_register_highlighter   (IdeCtagsService            *self,
                                                     IdeCtagsHighlighter        *highlighter);
void       ide_ctags_service_register_completion    (IdeCtagsService            *self,
                                                     IdeCtagsCompletionProvider *completion);
void       ide_ctags_service_pause                  (IdeCtagsService            *self);
void       ide_ctags_service_unpause                (IdeCtagsService            *self);
GPtrArray *ide_ctags_service_get_indexes            (IdeCtagsService            *self);

G_END_DECLS
