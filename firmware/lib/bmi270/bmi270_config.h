/*
 * bmi270_config.h - see bmi270_config.c for where this image comes from.
 */
#ifndef BMI270_CONFIG_H
#define BMI270_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMI270_CONFIG_SIZE 8192

extern const uint8_t bmi270_config_file[BMI270_CONFIG_SIZE];

#ifdef __cplusplus
}
#endif

#endif /* BMI270_CONFIG_H */
