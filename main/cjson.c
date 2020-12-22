#include "cJSON.h"
#include <stdlib.h>

void init_cjson()
{
  cJSON_Hooks hooks = {
    .malloc_fn = malloc,
    .free_fn = free
  };
  cJSON_InitHooks(&hooks);
}
