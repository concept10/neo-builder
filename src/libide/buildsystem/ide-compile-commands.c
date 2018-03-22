/* ide-compile-commands.c
 *
 * Copyright © 2017 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "ide-compile-commands"

#include "config.h"

#include <dazzle.h>
#include <json-glib/json-glib.h>
#include <string.h>

#include "ide-debug.h"

#include "buildsystem/ide-compile-commands.h"
#include "threading/ide-task.h"

/**
 * SECTION:ide-compile-commands
 * @title: IdeCompileCommands
 * @short_description: Integration with compile_commands.json
 *
 * The #IdeCompileCommands object provides a simplified interface to
 * interact with compile_commands.json files which are generated by a
 * number of build systems, including Clang tooling, Meson and CMake.
 *
 * Create a new #IdeCompileCommands instance, and then asynchronously
 * load the file using ide_compile_commands_load_async(). After the
 * database has been loaded, you can access build commands using
 * ide_compile_commands_lookup().
 *
 * Due to the rather unfortunate design of JSON, this file holds on
 * to a number of strings during the lifetime of the object, for each
 * of the compile commands. On larger projects, this can be the order
 * of a couple of megabytes.
 *
 * Since: 3.28
 */

struct _IdeCompileCommands
{
  GObject parent_instance;

  /*
   * The info_by_file field contains a hashtable whose keys are #GFile
   * matching the file that is to be compiled. It contains as a value
   * the CompileInfo struct describing how to compile that file.
   */
  GHashTable *info_by_file;

  /*
   * The vala_info field contains an array of every vala like file we've
   * discovered while parsing the database. This is used so because some
   * compile_commands.json only have a single valac command which wont
   * match the file we want to lookup (Notably Meson-based).
   */
  GPtrArray *vala_info;

  /*
   * The has_loaded field determines if we've had a load (async or sync
   * variant) operation called. We can only do this safely once because
   * we assign state in the task worker. Callers must discard the object
   * if the load operation fails.
   */
  guint has_loaded : 1;
};

typedef struct
{
  GFile *directory;
  GFile *file;
  gchar *command;
} CompileInfo;

G_DEFINE_TYPE (IdeCompileCommands, ide_compile_commands, G_TYPE_OBJECT)

static void
compile_info_free (gpointer data)
{
  CompileInfo *info = data;

  if (info != NULL)
    {
      g_clear_object (&info->directory);
      g_clear_object (&info->file);
      g_clear_pointer (&info->command, g_free);
      g_slice_free (CompileInfo, info);
    }
}

static void
ide_compile_commands_finalize (GObject *object)
{
  IdeCompileCommands *self = (IdeCompileCommands *)object;

  g_clear_pointer (&self->info_by_file, g_hash_table_unref);
  g_clear_pointer (&self->vala_info, g_ptr_array_unref);

  G_OBJECT_CLASS (ide_compile_commands_parent_class)->finalize (object);
}

static void
ide_compile_commands_class_init (IdeCompileCommandsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_compile_commands_finalize;
}

static void
ide_compile_commands_init (IdeCompileCommands *self)
{
}

/**
 * ide_compile_commands_new:
 *
 * Creates a new #IdeCompileCommands object which can be used to parse
 * clang-style compile commands database files (compile_commands.json).
 *
 * Returns: The newly created #IdeCompileCommands
 *
 * Since: 3.28
 */
IdeCompileCommands *
ide_compile_commands_new (void)
{
  return g_object_new (IDE_TYPE_COMPILE_COMMANDS, NULL);
}

static void
ide_compile_commands_load_worker (IdeTask      *task,
                                  gpointer      source_object,
                                  gpointer      task_data,
                                  GCancellable *cancellable)
{
  IdeCompileCommands *self = source_object;
  GFile *gfile = task_data;
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GHashTable) info_by_file = NULL;
  g_autoptr(GHashTable) directories_by_path = NULL;
  g_autoptr(GPtrArray) vala_info = NULL;
  g_autofree gchar *contents = NULL;
  JsonNode *root;
  JsonArray *ar;
  gsize len = 0;
  guint n_items;

  IDE_ENTRY;

  g_assert (IDE_IS_TASK (task));
  g_assert (IDE_IS_COMPILE_COMMANDS (self));
  g_assert (G_IS_FILE (gfile));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  parser = json_parser_new ();

  if (!g_file_load_contents (gfile, cancellable, &contents, &len, NULL, &error) ||
      !json_parser_load_from_data (parser, contents, len, &error))
    {
      ide_task_return_error (task, g_steal_pointer (&error));
      IDE_EXIT;
    }

  if (NULL == (root = json_parser_get_root (parser)) ||
      !JSON_NODE_HOLDS_ARRAY (root) ||
      NULL == (ar = json_node_get_array (root)))
    {
      ide_task_return_new_error (task,
                                 G_IO_ERROR,
                                 G_IO_ERROR_INVALID_DATA,
                                 "Failed to extract commands, invalid json");
      IDE_EXIT;
    }

  info_by_file = g_hash_table_new_full (g_file_hash,
                                        (GEqualFunc)g_file_equal,
                                        NULL,
                                        compile_info_free);

  directories_by_path = g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               NULL,
                                               g_object_unref);

  vala_info = g_ptr_array_new_with_free_func (compile_info_free);

  n_items = json_array_get_length (ar);

  for (guint i = 0; i < n_items; i++)
    {
      CompileInfo *info;
      JsonNode *item;
      JsonNode *value;
      JsonObject *obj;
      GFile *dir;
      const gchar *directory = NULL;
      const gchar *file = NULL;
      const gchar *command = NULL;

      item = json_array_get_element (ar, i);

      /* Skip past this node if its invalid for some reason, so we
       * can try to be tolerante of errors created by broken tooling.
       */
      if (item == NULL ||
          !JSON_NODE_HOLDS_OBJECT (item) ||
          NULL == (obj = json_node_get_object (item)))
        continue;

      if (json_object_has_member (obj, "file") &&
          NULL != (value = json_object_get_member (obj, "file")) &&
          JSON_NODE_HOLDS_VALUE (value))
        file = json_node_get_string (value);

      if (json_object_has_member (obj, "directory") &&
          NULL != (value = json_object_get_member (obj, "directory")) &&
          JSON_NODE_HOLDS_VALUE (value))
        directory = json_node_get_string (value);

      if (json_object_has_member (obj, "command") &&
          NULL != (value = json_object_get_member (obj, "command")) &&
          JSON_NODE_HOLDS_VALUE (value))
        command = json_node_get_string (value);

      /* Ignore items that are missing something or other */
      if (file == NULL || command == NULL || directory == NULL)
        continue;

      /* Try to reduce the number of GFile we have for directories */
      if (NULL == (dir = g_hash_table_lookup (directories_by_path, directory)))
        {
          dir = g_file_new_for_path (directory);
          g_hash_table_insert (directories_by_path, (gchar *)directory, dir);
        }

      info = g_slice_new0 (CompileInfo);
      info->file = g_file_resolve_relative_path (dir, file);
      info->directory = g_object_ref (dir);
      info->command = g_strdup (command);
      g_hash_table_replace (info_by_file, info->file, info);

      /*
       * We might need to keep a special copy of this for resolving .vala
       * builds which won't be able ot be matched based on the filename. We
       * keep all of them around right now in case we want to later on find
       * the closest match based on directory.
       */
      if (g_str_has_suffix (file, ".vala"))
        {
          info = g_slice_new0 (CompileInfo);
          info->file = g_file_resolve_relative_path (dir, file);
          info->directory = g_object_ref (dir);
          info->command = g_strdup (command);
          g_ptr_array_add (vala_info, info);
        }
    }

  self->info_by_file = g_steal_pointer (&info_by_file);
  self->vala_info = g_steal_pointer (&vala_info);

  ide_task_return_boolean (task, TRUE);

  IDE_EXIT;
}

/**
 * ide_compile_commands_load:
 * @self: An #IdeCompileCommands
 * @file: a #GFile
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: A location for a #GError, or %NULL
 *
 * Synchronously loads the contents of the requested @file and parses
 * the JSON command database contained within.
 *
 * You may only call this function once on an #IdeCompileCommands object.
 * If there is a failure, you must create a new #IdeCompileCommands instance
 * instead of calling this function again.
 *
 * See also: ide_compile_commands_load_async()
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 *
 * Since: 3.28
 */
gboolean
ide_compile_commands_load (IdeCompileCommands  *self,
                           GFile               *file,
                           GCancellable        *cancellable,
                           GError             **error)
{
  g_autoptr(IdeTask) task = NULL;
  gboolean ret;

  IDE_ENTRY;

  g_return_val_if_fail (IDE_IS_COMPILE_COMMANDS (self), FALSE);
  g_return_val_if_fail (self->has_loaded == FALSE, FALSE);
  g_return_val_if_fail (G_IS_FILE (file), FALSE);
  g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), FALSE);

  self->has_loaded = TRUE;

  task = ide_task_new (self, cancellable, NULL, NULL);
  ide_task_set_priority (task, G_PRIORITY_LOW);
  ide_task_set_source_tag (task, ide_compile_commands_load);
  ide_task_set_task_data (task, g_object_ref (file), g_object_unref);
  ide_compile_commands_load_worker (task, self, file, cancellable);

  ret = ide_task_propagate_boolean (task, error);

  IDE_RETURN (ret);
}

/**
 * ide_compile_commands_load_async:
 * @self: An #IdeCompileCommands
 * @file: a #GFile
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the callback for the async operation
 * @user_data: user data for @callback
 *
 * Asynchronously loads the contents of the requested @file and parses
 * the JSON command database contained within.
 *
 * You may only call this function once on an #IdeCompileCommands object.
 * If there is a failure, you must create a new #IdeCompileCommands instance
 * instead of calling this function again.
 *
 * See also: ide_compile_commands_load_finish()
 *
 * Since: 3.28
 */
void
ide_compile_commands_load_async (IdeCompileCommands  *self,
                                 GFile               *file,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_autoptr(IdeTask) task = NULL;

  IDE_ENTRY;

  g_return_if_fail (IDE_IS_COMPILE_COMMANDS (self));
  g_return_if_fail (self->has_loaded == FALSE);
  g_return_if_fail (G_IS_FILE (file));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  self->has_loaded = TRUE;

  task = ide_task_new (self, cancellable, callback, user_data);
  ide_task_set_priority (task, G_PRIORITY_LOW);
  ide_task_set_source_tag (task, ide_compile_commands_load_async);
  ide_task_set_task_data (task, g_object_ref (file), g_object_unref);
  ide_task_run_in_thread (task, ide_compile_commands_load_worker);

  IDE_EXIT;
}

/**
 * ide_compile_commands_load_finish:
 * @self: An #IdeCompileCommands
 * @result: a #GAsyncResult provided to the callback
 * @error: A location for a #GError, or %NULL
 *
 * Completes an asynchronous request to ide_compile_commands_load_async().
 *
 * See also: ide_compile_commands_load_async()
 *
 * Returns: %TRUE if the file was loaded successfully; otherwise %FALSE
 *   and @error is set.
 *
 * Since: 3.28
 */
gboolean
ide_compile_commands_load_finish (IdeCompileCommands  *self,
                                  GAsyncResult        *result,
                                  GError             **error)
{
  gboolean ret;

  IDE_ENTRY;

  g_return_val_if_fail (IDE_IS_COMPILE_COMMANDS (self), FALSE);
  g_return_val_if_fail (IDE_IS_TASK (result), FALSE);

  ret = ide_task_propagate_boolean (IDE_TASK (result), error);

  IDE_RETURN (ret);
}

static gboolean
suffix_is_c_like (const gchar *suffix)
{
  if (suffix == NULL)
    return FALSE;

  return !!strstr (suffix, ".c") || !!strstr (suffix, ".h") ||
         !!strstr (suffix, ".cc") || !!strstr (suffix, ".hh") ||
         !!strstr (suffix, ".cxx") || !!strstr (suffix, ".hxx") ||
         !!strstr (suffix, ".cpp") || !!strstr (suffix, ".hpp");
}

static gboolean
suffix_is_vala (const gchar *suffix)
{
  if (suffix == NULL)
    return FALSE;

  return !!strstr (suffix, ".vala");
}

static gchar *
ide_compile_commands_resolve (IdeCompileCommands *self,
                              const CompileInfo  *info,
                              const gchar        *path)
{
  g_autoptr(GFile) file = NULL;

  g_assert (IDE_IS_COMPILE_COMMANDS (self));
  g_assert (info != NULL);

  if (path == NULL)
    return NULL;

  if (g_path_is_absolute (path))
    return g_strdup (path);

  file = g_file_resolve_relative_path (info->directory, path);
  if (file != NULL)
    return g_file_get_path (file);

  return NULL;
}

static void
ide_compile_commands_filter_c (IdeCompileCommands   *self,
                               const CompileInfo    *info,
                               const gchar * const  *system_includes,
                               gchar              ***argv)
{
  g_autoptr(GPtrArray) ar = NULL;

  g_assert (IDE_IS_COMPILE_COMMANDS (self));
  g_assert (info != NULL);
  g_assert (argv != NULL);

  if (*argv == NULL)
    return;

  ar = g_ptr_array_new_with_free_func (g_free);

  if (system_includes != NULL)
    {
      for (guint i = 0; system_includes[i]; i++)
        g_ptr_array_add (ar, g_strdup_printf ("-I%s", system_includes[i]));
    }

  for (guint i = 0; (*argv)[i] != NULL; i++)
    {
      const gchar *param = (*argv)[i];
      const gchar *next = (*argv)[i+1];
      g_autofree gchar *resolved = NULL;

      if (param[0] != '-')
        continue;

      switch (param[1])
        {
        case 'I': /* -I/usr/include, -I /usr/include */
          if (param[2] != '\0')
            next = &param[2];
          resolved = ide_compile_commands_resolve (self, info, next);
          if (resolved != NULL)
            g_ptr_array_add (ar, g_strdup_printf ("-I%s", resolved));
          break;

        case 'f': /* -fPIC */
        case 'W': /* -Werror... */
        case 'm': /* -m64 -mtune=native */
        case 'O': /* -O2 */
          g_ptr_array_add (ar, g_strdup (param));
          break;

        case 'M': /* -MMD -MQ -MT -MF <file> */
          /* ignore the -M class of commands */
          break;

        case 'D': /* -DFOO, -D FOO */
        case 'x': /* -xc++ */
          g_ptr_array_add (ar, g_strdup (param));
          if (param[2] == '\0')
            g_ptr_array_add (ar, g_strdup (next));
          break;

        default:
          if (g_str_has_prefix (param, "-std=") ||
              dzl_str_equal0 (param, "-pthread") ||
              g_str_has_prefix (param, "-isystem"))
            {
              g_ptr_array_add (ar, g_strdup (param));
            }
          else if (next != NULL && dzl_str_equal0 (param, "-include"))
            {
              g_ptr_array_add (ar, g_strdup (param));
              g_ptr_array_add (ar, ide_compile_commands_resolve (self, info, next));
            }
          break;
        }
    }

  g_ptr_array_add (ar, NULL);

  g_strfreev (*argv);
  *argv = (gchar **)g_ptr_array_free (g_steal_pointer (&ar), FALSE);
}

static void
ide_compile_commands_filter_vala (IdeCompileCommands   *self,
                                  const CompileInfo    *info,
                                  gchar              ***argv)
{
  GPtrArray *ar;

  g_assert (IDE_IS_COMPILE_COMMANDS (self));
  g_assert (info != NULL);
  g_assert (argv != NULL);

  if (*argv == NULL)
    return;

  ar = g_ptr_array_new ();

  for (guint i = 0; (*argv)[i] != NULL; i++)
    {
      const gchar *param = (*argv)[i];
      const gchar *next = (*argv)[i+1];

      if (g_str_has_prefix (param, "--pkg=") ||
          g_str_has_prefix (param, "--target-glib=") ||
          !!strstr (param, ".vapi"))
        {
          g_ptr_array_add (ar, g_strdup (param));
        }
      else if (g_str_has_prefix (param, "--vapidir=") ||
               g_str_has_prefix (param, "--girdir=") ||
               g_str_has_prefix (param, "--metadatadir="))
        {
          g_autofree gchar *resolved = NULL;
          gchar *eq = strchr (param, '=');

          next = eq + 1;
          *eq = '\0';

          resolved = ide_compile_commands_resolve (self, info, next);
          g_ptr_array_add (ar, g_strdup_printf ("%s=%s", param, resolved));
        }
      else if (next != NULL &&
               (g_str_has_prefix (param, "--pkg") ||
                g_str_has_prefix (param, "--target-glib")))
        {
          g_ptr_array_add (ar, g_strdup (param));
          g_ptr_array_add (ar, g_strdup (next));
          i++;
        }
      else if (next != NULL &&
               (g_str_has_prefix (param, "--vapidir") ||
                g_str_has_prefix (param, "--girdir") ||
                g_str_has_prefix (param, "--metadatadir")))
        {
          g_ptr_array_add (ar, g_strdup (param));
          g_ptr_array_add (ar, ide_compile_commands_resolve (self, info, next));
          i++;
        }
    }

  g_free (*argv);

  g_ptr_array_add (ar, NULL);
  *argv = (gchar **)g_ptr_array_free (ar, FALSE);
}

static const CompileInfo *
find_with_alternates (IdeCompileCommands *self,
                      GFile              *file)
{
  const CompileInfo *info;

  g_assert (IDE_IS_COMPILE_COMMANDS (self));
  g_assert (G_IS_FILE (file));

  if (self->info_by_file == NULL)
    return NULL;

  if (NULL != (info = g_hash_table_lookup (self->info_by_file, file)))
    return info;

  {
    g_autofree gchar *path = g_file_get_path (file);
    gsize len = strlen (path);

    if (g_str_has_suffix (path, "-private.h"))
      {
        g_autofree gchar *other_path = NULL;
        g_autoptr(GFile) other = NULL;

        path[len - strlen ("-private.h")] = 0;

        other_path = g_strconcat (path, ".c", NULL);
        other = g_file_new_for_path (other_path);

        if (NULL != (info = g_hash_table_lookup (self->info_by_file, other)))
          return info;
      }
    else if (g_str_has_suffix (path, ".h"))
      {
        static const gchar *tries[] = { "c", "cc", "cpp" };
        path[--len] = 0;

        for (guint i = 0; i < G_N_ELEMENTS (tries); i++)
          {
            g_autofree gchar *other_path = g_strconcat (path, tries[i], NULL);
            g_autoptr(GFile) other = g_file_new_for_path (other_path);

            if (NULL != (info = g_hash_table_lookup (self->info_by_file, other)))
              return info;
          }
      }
  }

  return NULL;
}

/**
 * ide_compile_commands_lookup:
 * @self: An #IdeCompileCommands
 * @file: a #GFile representing the file to lookup
 * @system_includes: system include dirs if any
 * @directory: (out) (optional) (transfer full): A location for a #GFile, or %NULL
 * @error: A location for a #GError, or %NULL
 *
 * Locates the commands to compile the @file requested.
 *
 * If @directory is non-NULL, then the directory to run the command from
 * is placed in @directory.
 *
 * Returns: (nullable) (transfer full): A string array or %NULL if
 *   there was a failure to locate or parse the command.
 *
 * Since: 3.28
 */
gchar **
ide_compile_commands_lookup (IdeCompileCommands   *self,
                             GFile                *file,
                             const gchar * const  *system_includes,
                             GFile               **directory,
                             GError              **error)
{
  g_autofree gchar *base = NULL;
  const CompileInfo *info;
  const gchar *dot;

  g_return_val_if_fail (IDE_IS_COMPILE_COMMANDS (self), NULL);
  g_return_val_if_fail (G_IS_FILE (file), NULL);

  base = g_file_get_basename (file);
  dot = strrchr (base, '.');

  if (NULL != (info = find_with_alternates (self, file)))
    {
      g_auto(GStrv) argv = NULL;
      gint argc = 0;

      if (!g_shell_parse_argv (info->command, &argc, &argv, error))
        return NULL;

      if (suffix_is_c_like (dot))
        ide_compile_commands_filter_c (self, info, system_includes, &argv);
      else if (suffix_is_vala (dot))
        ide_compile_commands_filter_vala (self, info, &argv);

      if (directory != NULL)
        *directory = g_object_ref (info->directory);

      return g_steal_pointer (&argv);
    }

  /*
   * Some compile-commands databases will give us info about .vala, but there
   * may only be a single valac command to run. While we parsed the JSON
   * document we stored information about each of the Vala files in a special
   * list for exactly this purpose.
   */
  if (dzl_str_equal0 (dot, ".vala") && self->vala_info != NULL)
    {
      for (guint i = 0; i < self->vala_info->len; i++)
        {
          g_auto(GStrv) argv = NULL;
          gint argc = 0;

          info = g_ptr_array_index (self->vala_info, i);

          if (!g_shell_parse_argv (info->command, &argc, &argv, NULL))
            continue;

          ide_compile_commands_filter_vala (self, info, &argv);

          if (directory != NULL)
            *directory = g_object_ref (info->directory);

          return g_steal_pointer (&argv);
        }
    }

  g_set_error_literal (error,
                       G_IO_ERROR,
                       G_IO_ERROR_NOT_FOUND,
                       "Failed to locate command for requested file");

  return NULL;
}
