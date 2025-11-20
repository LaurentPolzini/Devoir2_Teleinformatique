#ifndef __UTIL_H__
#define __UTIL_H__

// if malloc failed, returns NULL but i must clear pointer to be sure
void libereSiDoitEtreLiberer(void **ptr, int exitVal);

// cleans a pointer. Free and set to NULL
void cleanPtr(void **ptr);

#endif
