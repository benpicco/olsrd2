
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2009, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/autobuf.h"
#include "common/list.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/template.h"
#include "builddata/data.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "olsr_plugins.h"
#include "olsr.h"

/* Local functions */
struct avl_tree plugin_tree;
static bool plugin_tree_initialized = false;

/* library loading patterns */
static const char *dlopen_values[5];
static const char *dlopen_keys[5] = {
  "LIB",
  "PATH",
  "PRE",
  "POST",
  "VER",
};

static const char *dlopen_patterns[] = {
  "%PATH%/%PRE%%LIB%%POST%.%VER%",
  "%PATH%/%PRE%%LIB%%POST%",
  "%PATH%/%LIB%",
  "%PRE%%LIB%%POST%.%VER%",
  "%PRE%%LIB%%POST%",
  "%LIB%",
};


static int olsr_internal_unload_plugin(struct olsr_plugin *plugin, bool cleanup);
static void *_open_plugin(const char *filename);

OLSR_SUBSYSTEM_STATE(pluginsystem_state);

/**
 * This function is called by the constructor of a plugin.
 * because of this the first call has to initialize the list
 * head.
 *
 * @param pl_def pointer to plugin definition
 */
void
olsr_hookup_plugin(struct olsr_plugin *pl_def) {
  assert (pl_def->name);

  /* make sure plugin system is initialized */
  olsr_plugins_init();

  /* hook static plugin into avl tree */
  pl_def->p_node.key = pl_def->name;
  avl_insert(&plugin_tree, &pl_def->p_node);

  /* initialize the plugin */
  if (olsr_plugins_load(pl_def->name) == NULL) {
    OLSR_WARN(LOG_PLUGINLOADER, "Cannot load plugin %s", pl_def->name);
  }
}

/**
 * Initialize the plugin loader system
 */
int
olsr_plugins_init(void) {
  if (olsr_subsystem_init(&pluginsystem_state))
    return 0;

  avl_init(&plugin_tree, avl_comp_strcasecmp, false, NULL);
  plugin_tree_initialized = true;

  /* load predefined values for dlopen templates */
  dlopen_values[1] = ".";
  dlopen_values[2] = get_olsrd_sharedlibrary_prefix();
  dlopen_values[3] = get_olsrd_sharedlibrary_suffix();
  dlopen_values[4] = get_olsrd_version();
  return 0;
}

/**
 * Disable and unload all plugins
 */
void
olsr_plugins_cleanup(void) {
  struct olsr_plugin *plugin, *iterator;

  if (olsr_subsystem_cleanup(&pluginsystem_state))
    return;

  OLSR_FOR_ALL_PLUGIN_ENTRIES(plugin, iterator) {
    olsr_plugins_disable(plugin);
    olsr_internal_unload_plugin(plugin, true);
  }
}

/**
 * Query for a certain plugin name
 * @param libname name of plugin
 * @return pointer to plugin db entry, NULL if not found
 */
struct olsr_plugin *
olsr_get_plugin(const char *libname) {
  struct olsr_plugin *plugin;
  char *ptr, memorize;

  /* SOT: Hacked away the funny plugin check which fails if pathname is included */
  if ((ptr = strrchr(libname, '/')) != NULL) {
    libname = ptr + 1;
  }

  if ((ptr = strstr(libname, "olsrd_")) != NULL) {
    libname = ptr + strlen("olsrd_");
  }

  if ((ptr = strrchr(libname, '.')) != NULL) {
    memorize = *ptr;
    *ptr = 0;
  }

  plugin = avl_find_element(&plugin_tree, libname, plugin, p_node);

  if (ptr) {
    /* restore path */
    *ptr = memorize;
  }
  return plugin;
}

/**
 * Load a plugin and call its initialize callback
 *
 *@param libname the name of the library(file)
 *@return plugin db object
 */
struct olsr_plugin *
olsr_plugins_load(const char *libname)
{
  void *dlhandle;
  struct olsr_plugin *plugin;

  /* see if the plugin is there */
  if ((plugin = olsr_get_plugin(libname)) == NULL) {
    /* attempt to load the plugin */
#if 0
    if (olsr_cnf->dlPath) {
      char *path;
      path = malloc(strlen(olsr_cnf->dlPath) + strlen(libname) + 1, "Memory for absolute library path");
      strcpy(path, olsr_cnf->dlPath);
      strcat(path, libname);
    }
#endif
    dlhandle = _open_plugin(libname);

    if (dlhandle == NULL) {
      return NULL;
    }

    /* plugin should be in the tree now */
    if ((plugin = olsr_get_plugin(libname)) == NULL) {
      OLSR_WARN(LOG_PLUGINLOADER, "dynamic library loading failed: \"%s\"!\n", dlerror());
      return NULL;
    }

    plugin->int_dlhandle = dlhandle;
  }

  if (!plugin->int_loaded && plugin->load != NULL) {
    if (plugin->load()) {
      OLSR_WARN(LOG_PLUGINLOADER, "Load callback failed for plugin %s\n", plugin->name);
      return NULL;
    }
    OLSR_DEBUG(LOG_PLUGINLOADER, "Load callback of plugin %s successful\n", plugin->name);
  }
  plugin->int_loaded = true;
  return plugin;
}

/**
 * Enable a loaded plugin.
 * @param plugin pointer to plugin db object
 * @return 0 if plugin was enabled, -1 otherwise
 */
int
olsr_plugins_enable(struct olsr_plugin *plugin) {
  if (plugin->int_enabled) {
    OLSR_DEBUG(LOG_PLUGINLOADER, "Plugin %s is already active.\n", plugin->name);
    return 0;
  }

  if (!plugin->int_loaded && plugin->load != NULL) {
    if (plugin->load()) {
      OLSR_WARN(LOG_PLUGINLOADER, "Error, pre init failed for plugin %s\n", plugin->name);
      return -1;
    }
    OLSR_DEBUG(LOG_PLUGINLOADER, "Pre initialization of plugin %s successful\n", plugin->name);
  }

  plugin->int_loaded = true;

  if (plugin->enable != NULL) {
    if (plugin->enable()) {
      OLSR_WARN(LOG_PLUGINLOADER, "Error, post init failed for plugin %s\n", plugin->name);
      return -1;
    }
    OLSR_DEBUG(LOG_PLUGINLOADER, "Post initialization of plugin %s successful\n", plugin->name);
  }
  plugin->int_enabled = true;

  if (plugin->author != NULL && plugin->descr != NULL) {
    OLSR_INFO(LOG_PLUGINLOADER, "Plugin '%s' (%s) by %s activated successfully\n",
        plugin->descr, plugin->name, plugin->author);
  }
  else {
    OLSR_INFO(LOG_PLUGINLOADER, "Plugin '%s' activated successfully\n", plugin->name);
  }

  return 0;
}

/**
 * Disable (but not unload) an active plugin
 * @param plugin pointer to plugin db object
 * @return 0 if plugin was disabled, 1 otherwise
 */
int
olsr_plugins_disable(struct olsr_plugin *plugin) {
  if (!plugin->int_enabled) {
    OLSR_DEBUG(LOG_PLUGINLOADER, "Plugin %s is not active.\n", plugin->name);
    return 0;
  }

  if (!plugin->deactivate) {
    OLSR_DEBUG(LOG_PLUGINLOADER, "Plugin %s does not support disabling\n", plugin->name);
    return 1;
  }

  OLSR_INFO(LOG_PLUGINLOADER, "Deactivating plugin %s\n", plugin->name);

  if (plugin->disable != NULL) {
    if (plugin->disable()) {
      OLSR_DEBUG(LOG_PLUGINLOADER, "Plugin %s cannot be deactivated, error in pre cleanup\n", plugin->name);
      return 1;
    }
    OLSR_DEBUG(LOG_PLUGINLOADER, "Pre cleanup of plugin %s successful\n", plugin->name);
  }

  plugin->int_enabled = false;
  return 0;
}

/**
 * Unloads an active plugin. Static plugins cannot be removed until
 * final cleanup.
 * @param plugin pointer to plugin db object
 * @return 0 if plugin was removed, 1 otherwise
 */
int
olsr_plugins_unload(struct olsr_plugin *plugin) {
  return olsr_internal_unload_plugin(plugin, false);
}

/**
 * Internal helper function to unload a plugin using the old API
 * @param plugin pointer to plugin db object
 * @param cleanup true if this is the final cleanup
 *   before OLSR shuts down, false otherwise
 * @return 0 if the plugin was removed, -1 otherwise
 */
static int
olsr_internal_unload_plugin(struct olsr_plugin *plugin, bool cleanup) {
  if (plugin->int_enabled) {
    /* deactivate first if necessary */
    olsr_plugins_disable(plugin);
  }

  if (plugin->int_dlhandle == NULL && !cleanup) {
    /*
     * this is a static plugin and OLSR is not shutting down,
     * so it cannot be unloaded
     */
    return -1;
  }

  OLSR_INFO(LOG_PLUGINLOADER, "Unloading plugin %s\n", plugin->name);

  if (plugin->unload != NULL) {
    plugin->unload();
  }

  /* remove first from tree */
  avl_delete(&plugin_tree, &plugin->p_node);

  /* cleanup */
  if (plugin->int_dlhandle) {
    dlclose(plugin->int_dlhandle);
  }

  return false;
}


static void *
_open_plugin(const char *filename) {
  struct abuf_template_storage table[5];
  struct autobuf abuf;
  void *result;
  size_t i;
  int indexCount;

  if (abuf_init(&abuf, strlen(filename))) {
    OLSR_WARN_OOM(LOG_PLUGINLOADER);
    return NULL;
  }

  result = NULL;
  dlopen_values[0] = filename;

  for (i=0; result == NULL && i<ARRAYSIZE(dlopen_patterns); i++) {
    if ((indexCount = abuf_template_init(dlopen_keys, ARRAYSIZE(dlopen_keys),
        dlopen_patterns[i], table, ARRAYSIZE(table))) == -1) {
      OLSR_WARN(LOG_PLUGINLOADER, "Could not parse pattern %s for dlopen",
          dlopen_patterns[i]);
      continue;
    }

    abuf_clear(&abuf);
    abuf_templatef(&abuf, dlopen_patterns[i], dlopen_values, table, indexCount);

    OLSR_DEBUG(LOG_PLUGINLOADER, "Trying to load library: %s", abuf.buf);
    result = dlopen(abuf.buf, RTLD_NOW);
  }
  if (result == NULL) {
    OLSR_WARN(LOG_PLUGINLOADER, "dynamic library loading failed.\n");
  }
  else {
    OLSR_INFO(LOG_PLUGINLOADER, "Loading plugin %s from %s\n", filename, abuf.buf);
  }

  abuf_free(&abuf);
  return result;
}

/*
 * Local Variables:
 * mode: c
 * style: linux
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
