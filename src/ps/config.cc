#include "kv_engine/base_kv.h"
#include <cmath>
#include <folly/Conv.h>
#include <folly/GLog.h>
#include <stdlib.h>
#include <thread>

DEFINE_int32(value_size, 32 * 4, "");
DEFINE_int32(max_kv_num_per_request, 300,
             "max kv_ count per request, used for allocate buffer");
