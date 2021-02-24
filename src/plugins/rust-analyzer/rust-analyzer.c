/* rust-analyzer.c
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

#include <libpeas/peas.h>
#include <libide-lsp.h>
#include <libide-gui.h>
#include "rust-analyzer-completion-provider.h"
#include "rust-analyzer-symbol-resolver.h"
#include "rust-analyzer-diagnostic-provider.h"
#include "rust-analyzer-formatter.h"
#include "rust-analyzer-highlighter.h"
#include "rust-analyzer-hover-provider.h"
#include "rust-analyzer-workbench-addin.h"
#include "rust-analyzer-rename-provider.h"
#include "rust-analyzer-search-provider.h"
#include "rust-analyzer-preferences-addin.h"

_IDE_EXTERN void
_rust_analyzer_register_types (PeasObjectModule *module)
{
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_WORKBENCH_ADDIN,
                                              RUST_TYPE_ANALYZER_WORKBENCH_ADDIN);
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_COMPLETION_PROVIDER,
                                              RUST_TYPE_ANALYZER_COMPLETION_PROVIDER);
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_SYMBOL_RESOLVER,
                                              RUST_TYPE_ANALYZER_SYMBOL_RESOLVER);
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_DIAGNOSTIC_PROVIDER,
                                              RUST_TYPE_ANALYZER_DIAGNOSTIC_PROVIDER);
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_FORMATTER,
                                              RUST_TYPE_ANALYZER_FORMATTER);
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_HIGHLIGHTER,
                                              RUST_TYPE_ANALYZER_HIGHLIGHTER);
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_HOVER_PROVIDER,
                                              RUST_TYPE_ANALYZER_HOVER_PROVIDER);
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_RENAME_PROVIDER,
                                              RUST_TYPE_ANALYZER_RENAME_PROVIDER);
#if 0
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_SEARCH_PROVIDER,
                                              RUST_TYPE_ANALYZER_SEARCH_PROVIDER);
#endif
  peas_object_module_register_extension_type (module,
                                              IDE_TYPE_PREFERENCES_ADDIN,
                                              RUST_TYPE_ANALYZER_PREFERENCES_ADDIN);
}
