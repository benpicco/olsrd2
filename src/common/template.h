/*
 * template.h
 *
 *  Created on: Jul 19, 2011
 *      Author: rogge
 */

#ifndef TEMPLATE_H_
#define TEMPLATE_H_

#include "common/common_types.h"
#include "common/autobuf.h"

struct abuf_template_storage {
  size_t start;
  size_t end;
  size_t key_index;
};
EXPORT int abuf_template_init(const char **keys, size_t length,
    const char *format, struct abuf_template_storage *indexTable, size_t indexLength);
EXPORT int abuf_templatef(struct autobuf *autobuf, const char *format,
    char **values, struct abuf_template_storage *indexTable, size_t indexLength);

#endif /* TEMPLATE_H_ */
