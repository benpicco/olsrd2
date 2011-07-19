#!/bin/cmake

# replace spaces with string to convert into list
IF (${PLUGIN_LIST})
    string(REPLACE " " ";" "${PLUGIN_LIST}" ${PLUGINS})
ENDIF (${PLUGIN_LIST})

# create C file which would call the static plugin constructors 
file(WRITE  ${DST} "#include \"builddata/plugin_static.h\"\n")
file(APPEND ${DST} "\n")

IF (${PLUGIN_LIST})
    FOREACH(plugin ${PLUGIN_LIST})
        file(APPEND ${DST} "void hookup_plugin_${plugin}(void);\n")
    ENDFOREACH(plugin)
ENDIF (${PLUGIN_LIST})

file(APPEND ${DST} "\n")
file(APPEND ${DST} "void\n")
file(APPEND ${DST} "olsr_plugins_load_static(void) {\n")

IF (${PLUGIN_LIST})
    FOREACH(plugin ${PLUGIN_LIST})
        file(APPEND ${DST} "  hookup_plugin_${plugin}();\n")
    ENDFOREACH(plugin)
ENDIF (${PLUGIN_LIST})

file(APPEND ${DST} "}\n")
