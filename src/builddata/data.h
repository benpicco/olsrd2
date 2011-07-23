/*
 * builddata.h
 *
 *  Created on: Jun 9, 2011
 *      Author: rogge
 */

#ifndef BUILDDATA_H_
#define BUILDDATA_H_

#include "common/common_types.h"

EXPORT const char *olsr_builddata_get_version(void);
EXPORT const char *olsr_builddata_get_git_commit(void);
EXPORT const char *olsr_builddata_get_git_change(void);
EXPORT const char *olsr_builddata_get_builddate(void);
EXPORT const char *olsr_builddata_get_buildsystem(void);
EXPORT const char *olsr_builddata_get_sharedlibrary_prefix(void);
EXPORT const char *olsr_builddata_get_sharedlibrary_suffix(void);

#endif /* BUILDDATA_H_ */
