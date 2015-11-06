/* ide-workbench-private.h
 *
 * Copyright (C) 2015 Christian Hergert <chergert@redhat.com>
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
 */

#ifndef IDE_WORKBENCH_PRIVATE_H
#define IDE_WORKBENCH_PRIVATE_H

#include <libpeas/peas.h>

#include "ide-editor-perspective.h"
#include "ide-greeter-perspective.h"
#include "ide-preferences-perspective.h"
#include "ide-workbench.h"

G_BEGIN_DECLS

struct _IdeWorkbench
{
  GtkApplicationWindow       parent;

  IdeContext                *context;
  PeasExtensionSet          *addins;

  IdePerspective            *perspective;

  GtkStack                  *top_stack;
  GtkStack                  *titlebar_stack;
  IdeEditorPerspective      *editor_perspective;
  IdeGreeterPerspective     *greeter_perspective;
  IdePreferencesPerspective *preferences_perspective;
  GtkStack                  *perspectives_stack;
  GtkStackSwitcher          *perspectives_stack_switcher;
  GtkPopover                *perspectives_popover;
};

typedef struct
{
  GtkCallback callback;
  gpointer    user_data;
} IdeWorkbenchForeach;

void _ide_workbench_set_context (IdeWorkbench *workbench,
                                 IdeContext   *context);

G_END_DECLS

#endif /* IDE_WORKBENCH_PRIVATE_H */
