#ifndef UNICODE_H
#define UNICODE_H

#include<string.h>
#include <stdint.h>


void uni_str_to_hex(char* unicode_str, uint32_t* unicode_hex, size_t str_len);

void uni_hex_to_utf8(uint32_t* uni_hex, char* utf8_str, size_t hex_len);

#endif // UNICODE_H