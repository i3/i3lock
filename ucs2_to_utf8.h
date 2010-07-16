#ifndef _UCS2_TO_UTF8_H
#define _UCS2_TO_UTF8_H

int convert_ucs_to_utf8(char *input, char *output);
char *convert_utf8_to_ucs2(char *input, int *real_strlen);
void u8_dec(char *s, int *i);

#endif
