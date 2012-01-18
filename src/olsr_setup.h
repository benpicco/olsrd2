/*
 * olsr_setup.h
 *
 *  Created on: Sep 13, 2011
 *      Author: rogge
 */

#ifndef OLSR_SETUP_H_
#define OLSR_SETUP_H_

/* define custom logging levels */
enum custom_log_source {
  /*
   * add your custom logging sources here, the first source MUST
   * have the numerical value LOG_CORESOURCE_COUNT
   */

  LOG_CUSTOM_1 = LOG_CORESOURCE_COUNT,
};

int olsr_setup_cfginit(void) __attribute__((warn_unused_result));
int olsr_setup_init(void) __attribute__((warn_unused_result));
void olsr_setup_cleanup(void);
void olsr_setup_cfgcleanup(void);
size_t olsr_setup_get_logcount(void);
const char **olsr_setup_get_lognames(void);

#endif /* OLSR_SETUP_H_ */
