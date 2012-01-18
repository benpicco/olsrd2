/*
 * builddata.h
 *
 *  Created on: Jun 9, 2011
 *      Author: rogge
 */

#ifndef BUILDDATA_H_
#define BUILDDATA_H_

#include "common/common_types.h"

struct olsr_builddata {
  const char *app_name;
  const char *version;
  const char *versionstring_trailer;
  const char *help_prefix;
  const char *help_suffix;

  const char *default_config;

  const char *git_commit;
  const char *git_change;

  const char *builddate;
  const char *buildsystem;

  const char *sharedlibrary_prefix;
  const char *sharedlibrary_postfix;
};

EXPORT extern const struct olsr_builddata *olsr_builddata_get(void);

#endif /* BUILDDATA_H_ */
