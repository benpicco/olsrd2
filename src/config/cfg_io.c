
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
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

#ifndef WIN32
#include <alloca.h>
#else
#include <malloc.h>
#endif
#include <assert.h>
#include <string.h>

#include "common/autobuf.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"
#include "common/string.h"

#include "config/cfg.h"
#include "config/cfg_io.h"

static struct cfg_io *_find_io(const char *url,
    const char **io_param, struct autobuf *log);

struct avl_tree cfg_io_tree;
struct cfg_io *cfg_io_default = NULL;
static bool _io_initialized = false;


/**
 * Add a new io-handler to the global registry
 * @param io pointer to io handler object
 * @param name name of IO handler
 */
void
cfg_io_add(struct cfg_io *io) {
  if (!_io_initialized) {
    avl_init(&cfg_io_tree, cfg_avlcmp_keys, false, NULL);
    _io_initialized = true;

    /* first io-handler, make it the default (can be overwritten later) */
    io->def = true;
  }

  io->node.key = &io->name;
  avl_insert(&cfg_io_tree, &io->node);

  if (io->def) {
    cfg_io_default = io;
  }
}

/**
 * Unregister an io-handler
 * @param io pointer to io handler
 */
void
cfg_io_remove(struct cfg_io *io) {
  if (io->name) {
    avl_remove(&cfg_io_tree, &io->node);
    io->node.key = NULL;
    io->name = NULL;
  }

  if (cfg_io_default == io) {
    if (avl_is_empty(&cfg_io_tree)) {
      cfg_io_default = NULL;
    }
    else {
      cfg_io_default = avl_first_element(&cfg_io_tree, cfg_io_default, node);
    }
  }
}

/**
 * Load a configuration database from an external source
 * @param url URL specifying the external source
 *   might contain io-handler specification with <iohandler>://
 *   syntax.
 * @param parser name of parser to be used by io-handler (if necessary),
 *   NULL if parser autodetection should be used
 * @param log pointer to autobuffer to contain logging output
 *   by loader.
 * @return pointer to configuration database, NULL if an error happened
 */
struct cfg_db *
cfg_io_load_parser(const char *url, const char *parser, struct autobuf *log) {
  struct cfg_io *io;
  const char *io_param = NULL;

  io = _find_io(url, &io_param, log);
  if (io == NULL) {
    cfg_append_printable_line(log, "Error, unknown config io '%s'.", io->name);
    return NULL;
  }

  if (io->load == NULL) {
    cfg_append_printable_line(log, "Error, config io '%s' does not support loading.", io->name);
    return NULL;
  }
  return io->load(io_param, parser, log);
}

/**
 * Store a configuration database into an external destination.
 * @param url URL specifying the external source
 *   might contain io-handler specification with <iohandler>://
 *   syntax.
 * @param parser name of parser to be used by io-handler (if necessary),
 *   NULL if parser autodetection should be used
 * @param src configuration database to be stored
 * @param log pointer to autobuffer to contain logging output
 *   by storage.
 * @return 0 if data was stored, -1 if an error happened
 */
int
cfg_io_save_parser(const char *url, const char *parser, struct cfg_db *src, struct autobuf *log) {
  struct cfg_io *io;
  const char *io_param = NULL;

  io = _find_io(url, &io_param, log);
  if (io == NULL) {
    cfg_append_printable_line(log, "Error, unknown config io '%s'.", io->name);
    return -1;
  }

  if (io->save == NULL) {
    cfg_append_printable_line(log, "Error, config io '%s' does not support saving.", io->name);
    return -1;
  }
  return io->save(io_param, parser, src, log);
}

/**
 * Decode the URL string for load/storage
 * @param url url string
 * @param io_param pointer to a charpointer, will be used as a second
 *   return parameter for URL postfix
 * @param log pointer to autobuffer to contain logging output
 *   by storage.
 * @return pointer to io handler, NULL if none found or an error
 *   happened
 */
static struct cfg_io *
_find_io(const char *url, const char **io_param, struct autobuf *log) {
  struct cfg_io *io;
  char *buffer;
  const char *ptr1;

  if (!_io_initialized) {
    cfg_append_printable_line(log, "IO-handler empty!");
    return NULL;
  }

  ptr1 = strstr(url, "://");
  if (ptr1 == url) {
    cfg_append_printable_line(log, "Illegal URL '%s' as parameter for io selection", url);
    return NULL;
  }
  if (ptr1 == NULL) {
    /* get default io handler */
    io = cfg_io_default;
    ptr1 = url;
  }
  else {
    buffer = alloca(strlen(url) + 1);
    strcpy(buffer, url);
    if (ptr1 - url < (int)sizeof(buffer)) {
      buffer[ptr1 - url] = 0;
    }

    io = avl_find_element(&cfg_io_tree, buffer, io, node);
    ptr1 += 3;
  }

  if (io == NULL) {
    cfg_append_printable_line(log, "Cannot find loader for parameter '%s'", url);
    return NULL;
  }

  *io_param = ptr1;
  return io;
}
