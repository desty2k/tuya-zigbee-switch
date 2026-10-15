#ifndef DEVICE_CONFIG_BUILD_FEATURES_H_
#define DEVICE_CONFIG_BUILD_FEATURES_H_

/*
 * Features compiled into this image, each 0 or 1. The build sets them from the
 * board's feature list (src/features.mk); a feature at 0 has its code left
 * out of the image, and the config parser rejects its tokens.
 */
#ifdef HAL_SILABS
#include "silabs_config.h"
#endif

#if !defined(FEATURE_RELAY) || !defined(FEATURE_SWITCH) ||  \
    !defined(FEATURE_COVER) || !defined(FEATURE_BATTERY) || \
    !defined(FEATURE_METER) || !defined(FEATURE_PROBE) ||   \
    !defined(FEATURE_INTERLOCK)
#error "FEATURE_* must be set by the build (src/features.mk)"
#endif

#endif
