idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES pn532 epd driver cryptnox_utils)
