#!/bin/cmake

# replace spaces with string to convert into list
message ("Generate static plugin loader for '${PLUGINS}'")

string(REPLACE " " ";" PLUGIN_LIST "${PLUGINS}")

# create C file which would call the static plugin constructors 
file(WRITE  ${DST} "#include \"builddata/plugin_static.h\"\n")
file(APPEND ${DST} "#include \"common/common_types.h\"\n")
file(APPEND ${DST} "\n")

FOREACH(plugin ${PLUGIN_LIST})
    file(APPEND ${DST} "extern void hookup_plugin_${plugin}(void);\n")
ENDFOREACH(plugin)

file(APPEND ${DST} "\n")
file(APPEND ${DST} "void\n")
file(APPEND ${DST} "olsr_plugins_load_static(void) {\n")

FOREACH(plugin ${PLUGIN_LIST})
    file(APPEND ${DST} "  hookup_plugin_${plugin}();\n")
ENDFOREACH(plugin)

file(APPEND ${DST} "}\n")
