/*
 * cfg_stringarray.h
 *
 *  Created on: Sep 16, 2011
 *      Author: rogge
 */

#ifndef CFG_STRINGARRAY_H_
#define CFG_STRINGARRAY_H_

/* Represents a string or an array of strings */
struct cfg_stringarray {
  /* pointer to the first string */
  char *value;

  /* pointer to the last string */
  char *last_value;

  /* total length of all strings including zero-bytes */
  size_t length;
};

#define CFG_FOR_ALL_STRINGS(array, charptr) for (charptr = (array)->value; charptr != NULL && charptr <= (array)->last_value; charptr += strlen(charptr) + 1)

#endif /* CFG_STRINGARRAY_H_ */
