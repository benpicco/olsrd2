
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2012, the olsr.org team - see HISTORY file
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

#ifndef OLSR_LOGGING_CFG_H_
#define OLSR_LOGGING_CFG_H_

#include "common/common_types.h"
#include "config/cfg_schema.h"
#include "config/cfg_db.h"

#include "olsr_logging.h"

#define CFG_VALIDATE_LOGSOURCE(p_name, p_def, p_help, args...) _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_logcfg_schema_validate, .cb_valhelp = olsr_logcfg_schema_help, ##args )

EXPORT void olsr_logcfg_init(enum log_source *debug_lvl_1, size_t length);
EXPORT void olsr_logcfg_cleanup(void);
EXPORT int olsr_logcfg_apply(struct cfg_db *db) __attribute__((warn_unused_result));

EXPORT int olsr_logcfg_schema_validate(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);
EXPORT void olsr_logcfg_schema_help(
    const struct cfg_schema_entry *entry, struct autobuf *out);

#endif /* OLSR_LOGGING_CFG_H_ */
