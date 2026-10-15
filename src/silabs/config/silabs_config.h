// <o VERSION_STR>
#define VERSION_STR    "0.0.0"

// src/silabs/Makefile replaces this value with the NVM_MIGRATIONS_VERSION file
// at generation time; it is kept equal to that file for builds made elsewhere.
// <o NVM_MIGRATIONS_VERSION>
#define NVM_MIGRATIONS_VERSION    10

// <o DEFAULT_CONFIG>
#define DEFAULT_CONFIG            "device;device;"

// src/silabs/Makefile sets the board's features (src/features.mk) at
// generation time; builds made elsewhere contain every feature.
// <o FEATURE_RELAY>
#define FEATURE_RELAY        1
// <o FEATURE_SWITCH>
#define FEATURE_SWITCH       1
// <o FEATURE_COVER>
#define FEATURE_COVER        1
// <o FEATURE_BATTERY>
#define FEATURE_BATTERY      1
// <o FEATURE_METER>
#define FEATURE_METER        1
// <o FEATURE_INTERLOCK>
#define FEATURE_INTERLOCK    1
// The GPIO probe has no Silabs HAL.
// <o FEATURE_PROBE>
#define FEATURE_PROBE        0
