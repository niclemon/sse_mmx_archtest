#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>

uint32_t input_read_decimal(uint32_t min_value, uint32_t max_value);
char input_read_choice(const char *choices);

#endif
