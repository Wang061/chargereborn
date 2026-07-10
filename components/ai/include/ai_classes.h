#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Battery detector class ids. Keep these aligned with the trained data.yaml.
#define AI_CLASS_UNKNOWN (-1)
#define AI_CLASS_21700    0
#define AI_CLASS_18650    1
#define AI_CLASS_9V       2
#define AI_CLASS_AA       3
#define AI_CLASS_BAD_AA   4

#ifdef __cplusplus
}
#endif
