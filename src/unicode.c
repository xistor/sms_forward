#include<stdlib.h>
#include"unicode.h"
#include "luat_debug.h"

/**
 * Encode a code point using UTF-8
 * 
 * @author Ondřej Hruška <ondra@ondrovo.com>
 * @license MIT
 * 
 * @param out - output buffer (min 5 characters), will be 0-terminated
 * @param utf - code point 0-0x10FFFF
 * @return number of bytes on success, 0 on failure (also produces U+FFFD, which uses 3 bytes)
 */

static int utf8_encode(char *out, uint32_t utf) {

    // printf("utf %x\n", utf);
  if (utf <= 0x7F) {
    // Plain ASCII
    out[0] = (char) utf;
    out[1] = 0;
    return 1;
  }
  else if (utf <= 0x07FF) {
    // 2-byte unicode
    out[0] = (char) (((utf >> 6) & 0x1F) | 0xC0);
    out[1] = (char) (((utf >> 0) & 0x3F) | 0x80);
    out[2] = 0;
    return 2;
  }
  else if (utf <= 0xFFFF) {
    // 3-byte unicode
    out[0] = (char) (((utf >> 12) & 0x0F) | 0xE0);
    out[1] = (char) (((utf >>  6) & 0x3F) | 0x80);
    out[2] = (char) (((utf >>  0) & 0x3F) | 0x80);
    out[3] = 0;
    return 3;
  }
  else if (utf <= 0x10FFFF) {
    // 4-byte unicode
    out[0] = (char) (((utf >> 18) & 0x07) | 0xF0);
    out[1] = (char) (((utf >> 12) & 0x3F) | 0x80);
    out[2] = (char) (((utf >>  6) & 0x3F) | 0x80);
    out[3] = (char) (((utf >>  0) & 0x3F) | 0x80);
    out[4] = 0;
    return 4;
  }
  else { 
    // error - use replacement character
    out[0] = (char) 0xEF;  
    out[1] = (char) 0xBF;
    out[2] = (char) 0xBD;
    out[3] = 0;
    return 0;
  }
}


void uni_str_to_hex(char* unicode_str, uint32_t* unicode_hex, size_t str_len) {

    LUAT_DEBUG_PRINT("unicode_str: [%s] len[%d]", unicode_str, str_len);

    char uni_str[str_len + 1];
    memcpy(uni_str, unicode_str, str_len + 1);
    LUAT_DEBUG_PRINT("uni_str: [%s] ", uni_str);

    for(int i = str_len / 4 - 1; i >= 0; i--){
        uint32_t num = (int)strtol(&uni_str[(i) * 4], NULL, 16);
        unicode_hex[i] = num;
        uni_str[i * 4] = '\0';
        // LUAT_DEBUG_PRINT("unicode_hex[%d]: [%x] ", i, num);
    }
}

void uni_hex_to_utf8(uint32_t* uni_hex, char* utf8_str, size_t hex_len) {
    
    int n = 0, offset = 0;
    for(size_t i = 0; i < hex_len; i++) {
        // printf("n %d\n", n);
        n = utf8_encode(utf8_str + offset, uni_hex[i]);
        offset += n;
    }

    LUAT_DEBUG_PRINT("utf8_str %s \n",utf8_str);
}

