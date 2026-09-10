/*
 * catnip_typescale.h - what the type roles actually look like on this panel.
 *
 * Every size in the device on one page, largest first, each line drawn in the
 * role it is naming. It exists because the sizes are the platform's and an
 * argument about them is otherwise an argument about numbers: `body` went from
 * 14 to 16 because 14 is a size you read by leaning in, and the only way to
 * settle that is to hold the thing and look.
 *
 * It is reached from the device page, beside the preference page and the
 * diagnostic, because it belongs to the same question those two answer: what is
 * this thing, and what does it do.
 */
#ifndef CATNIP_TYPESCALE_H
#define CATNIP_TYPESCALE_H

#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catnip_typescale catnip_typescale;

catnip_typescale *catnip_typescale_new(catnip_rt *rt);

/* Put the page up. It has no state of its own: every line is fixed, because a
 * specimen that changed would not be one. */
void catnip_typescale_show(catnip_typescale *t);

void catnip_typescale_free(catnip_typescale *t);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_TYPESCALE_H */
