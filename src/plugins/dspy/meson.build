if get_option('plugin_dspy')

libdspy_dep = dependency('dspy-1', version: '>= 1.4.0')

plugins_sources += files([
  'dspy-plugin.c',
  'gbp-dspy-application-addin.c',
  'gbp-dspy-workspace.c',
])

plugin_dspy_resources = gnome.compile_resources(
  'dspy-resources',
  'dspy.gresource.xml',
  c_name: 'gbp_dspy',
)

plugins_sources += plugin_dspy_resources
plugins_deps += libdspy_dep

endif
