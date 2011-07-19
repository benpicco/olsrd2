/*
 * builddata.h
 *
 *  Created on: Jun 9, 2011
 *      Author: rogge
 */

#ifndef BUILDDATA_H_
#define BUILDDATA_H_

#include "common/common_types.h"

EXPORT const char *get_olsrd_version(void);
EXPORT const char *get_olsrd_git_commit(void);
EXPORT const char *get_olsrd_git_change(void);
EXPORT const char *get_olsrd_builddate(void);
EXPORT const char *get_olsrd_buildsystem(void);

#endif /* BUILDDATA_H_ */
