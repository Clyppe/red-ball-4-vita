#ifndef RB4_SOOMLA_H
#define RB4_SOOMLA_H

void soomla_init(void);

/* Parse a Soomla NdkGlue JSON request and return a malloc'd JSON object.
 * Caller must free(). Never returns NULL. */
char *soomla_receive(const char *json);

#endif
