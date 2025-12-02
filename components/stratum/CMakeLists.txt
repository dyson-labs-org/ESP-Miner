idf_component_register(
SRCS
    "utils.c"
    "mining.c"
    "stratum_api.c"
                    
INCLUDE_DIRS
    "include"

REQUIRES
    "cjson"
    "mbedtls"
    "app_update"
    "esp_timer"
)