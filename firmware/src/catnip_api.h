/*
 * catnip_api.h - install the device & service namespaces into a runtime (#3).
 *
 * Exposes device.*, sensor.*, gpio.*, service.* and fs.* to Lua, all backed by
 * the catnip_hal. Also mirrors them under the catnip namespace. Call after the
 * runtime is created (and, typically, after ui is opened).
 */
#ifndef CATNIP_API_H
#define CATNIP_API_H

#include "catnip_hal.h"
#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install the namespaces, backed by `hal` (which must outlive the runtime).
 * Returns 0 on success. */
int catnip_api_open(catnip_rt *rt, const catnip_hal *hal);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_API_H */
