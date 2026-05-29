/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* Minimal cJSON implementation for AVM xlayer config parsing. */

#include "third_party/cJSON/cJSON.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Internal helpers --- */

static cJSON *cJSON_New_Item(void) {
  cJSON *node = (cJSON *)calloc(1, sizeof(cJSON));
  return node;
}

/* Skip whitespace and comments */
static const char *skip_whitespace(const char *in) {
  if (in == NULL) return NULL;
  while (*in && (unsigned char)*in <= ' ') in++;
  /* Skip // line comments */
  while (*in == '/' && *(in + 1) == '/') {
    while (*in && *in != '\n') in++;
    while (*in && (unsigned char)*in <= ' ') in++;
  }
  return in;
}

/* Forward declarations */
static const char *parse_value(cJSON *item, const char *value);
static const char *parse_string(cJSON *item, const char *str);
static const char *parse_number(cJSON *item, const char *num);
static const char *parse_array(cJSON *item, const char *value);
static const char *parse_object(cJSON *item, const char *value);

/* --- Parse string --- */
static unsigned parse_hex4(const char *str) {
  unsigned h = 0;
  for (int i = 0; i < 4; i++) {
    h <<= 4;
    if (*str >= '0' && *str <= '9')
      h += (unsigned)(*str - '0');
    else if (*str >= 'A' && *str <= 'F')
      h += (unsigned)(10 + *str - 'A');
    else if (*str >= 'a' && *str <= 'f')
      h += (unsigned)(10 + *str - 'a');
    else
      return 0;
    str++;
  }
  return h;
}

static const char *parse_string(cJSON *item, const char *str) {
  if (*str != '\"') return NULL;
  str++;

  const char *start = str;
  size_t len = 0;

  /* First pass: compute length */
  while (*str && *str != '\"') {
    if (*str == '\\') {
      str++;
      if (*str == 'u') {
        str += 4;
        len += 4; /* UTF-8 worst case, simplified */
      } else {
        len++;
      }
    } else {
      len++;
    }
    str++;
  }
  if (*str != '\"') return NULL;

  char *out = (char *)malloc(len + 1);
  if (!out) return NULL;

  str = start;
  char *ptr = out;
  while (*str && *str != '\"') {
    if (*str != '\\') {
      *ptr++ = *str++;
    } else {
      str++;
      switch (*str) {
        case 'b': *ptr++ = '\b'; break;
        case 'f': *ptr++ = '\f'; break;
        case 'n': *ptr++ = '\n'; break;
        case 'r': *ptr++ = '\r'; break;
        case 't': *ptr++ = '\t'; break;
        case 'u': {
          unsigned uc = parse_hex4(str + 1);
          str += 4;
          /* Simple UTF-8 encoding */
          if (uc < 0x80) {
            *ptr++ = (char)uc;
          } else if (uc < 0x800) {
            *ptr++ = (char)(0xC0 | (uc >> 6));
            *ptr++ = (char)(0x80 | (uc & 0x3F));
          } else {
            *ptr++ = (char)(0xE0 | (uc >> 12));
            *ptr++ = (char)(0x80 | ((uc >> 6) & 0x3F));
            *ptr++ = (char)(0x80 | (uc & 0x3F));
          }
          break;
        }
        default: *ptr++ = *str; break;
      }
      str++;
    }
  }
  *ptr = '\0';

  item->valuestring = out;
  item->type = cJSON_String;
  return str + 1; /* skip closing quote */
}

/* --- Parse number --- */
static const char *parse_number(cJSON *item, const char *num) {
  double n = 0;
  double sign = 1;
  int scale = 0;
  int subscale = 0;
  int signsubscale = 1;

  if (*num == '-') {
    sign = -1;
    num++;
  }
  if (*num == '0') {
    num++;
  } else if (*num >= '1' && *num <= '9') {
    do {
      n = n * 10.0 + (*num - '0');
      num++;
    } while (*num >= '0' && *num <= '9');
  }
  if (*num == '.' && num[1] >= '0' && num[1] <= '9') {
    num++;
    do {
      n = n * 10.0 + (*num - '0');
      scale--;
      num++;
    } while (*num >= '0' && *num <= '9');
  }
  if (*num == 'e' || *num == 'E') {
    num++;
    if (*num == '+')
      num++;
    else if (*num == '-') {
      signsubscale = -1;
      num++;
    }
    while (*num >= '0' && *num <= '9') {
      subscale = subscale * 10 + (*num - '0');
      num++;
    }
  }

  n = sign * n * pow(10.0, scale + subscale * signsubscale);

  item->valuedouble = n;
  item->valueint = (int)n;
  item->type = cJSON_Number;
  return num;
}

/* --- Parse array --- */
static const char *parse_array(cJSON *item, const char *value) {
  if (*value != '[') return NULL;
  item->type = cJSON_Array;
  value = skip_whitespace(value + 1);
  if (*value == ']') return value + 1; /* empty array */

  cJSON *child = cJSON_New_Item();
  if (!child) return NULL;
  item->child = child;
  value = skip_whitespace(parse_value(child, skip_whitespace(value)));
  if (!value) return NULL;

  while (*value == ',') {
    cJSON *new_item = cJSON_New_Item();
    if (!new_item) return NULL;
    child->next = new_item;
    new_item->prev = child;
    child = new_item;
    value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
    if (!value) return NULL;
  }

  if (*value == ']') return value + 1;
  return NULL; /* malformed */
}

/* --- Parse object --- */
static const char *parse_object(cJSON *item, const char *value) {
  if (*value != '{') return NULL;
  item->type = cJSON_Object;
  value = skip_whitespace(value + 1);
  if (*value == '}') return value + 1; /* empty object */

  cJSON *child = cJSON_New_Item();
  if (!child) return NULL;
  item->child = child;

  /* Parse key */
  value = parse_string(child, skip_whitespace(value));
  if (!value) return NULL;
  child->string = child->valuestring;
  child->valuestring = NULL;
  child->type = cJSON_Invalid;

  if (*value != ':') return NULL;
  value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
  if (!value) return NULL;

  while (*value == ',') {
    cJSON *new_item = cJSON_New_Item();
    if (!new_item) return NULL;
    child->next = new_item;
    new_item->prev = child;
    child = new_item;

    value = parse_string(child, skip_whitespace(value + 1));
    if (!value) return NULL;
    child->string = child->valuestring;
    child->valuestring = NULL;
    child->type = cJSON_Invalid;

    if (*value != ':') return NULL;
    value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
    if (!value) return NULL;
  }

  if (*value == '}') return value + 1;
  return NULL; /* malformed */
}

/* --- Parse value --- */
static const char *parse_value(cJSON *item, const char *value) {
  if (!value) return NULL;
  if (!strncmp(value, "null", 4)) {
    item->type = cJSON_NULL;
    return value + 4;
  }
  if (!strncmp(value, "false", 5)) {
    item->type = cJSON_False;
    item->valueint = 0;
    return value + 5;
  }
  if (!strncmp(value, "true", 4)) {
    item->type = cJSON_True;
    item->valueint = 1;
    return value + 4;
  }
  if (*value == '\"') return parse_string(item, value);
  if (*value == '-' || (*value >= '0' && *value <= '9'))
    return parse_number(item, value);
  if (*value == '[') return parse_array(item, value);
  if (*value == '{') return parse_object(item, value);
  return NULL; /* failure */
}

/* --- Public API --- */

cJSON *cJSON_Parse(const char *value) {
  cJSON *c = cJSON_New_Item();
  if (!c) return NULL;
  const char *end = parse_value(c, skip_whitespace(value));
  if (!end) {
    cJSON_Delete(c);
    return NULL;
  }
  return c;
}

void cJSON_Delete(cJSON *item) {
  cJSON *next;
  while (item) {
    next = item->next;
    if (!(item->type & cJSON_IsReference) && item->child)
      cJSON_Delete(item->child);
    if (!(item->type & cJSON_IsReference) && item->valuestring)
      free(item->valuestring);
    if (!(item->type & cJSON_StringIsConst) && item->string)
      free(item->string);
    free(item);
    item = next;
  }
}

int cJSON_GetArraySize(const cJSON *array) {
  cJSON *child;
  int size = 0;
  if (!array) return 0;
  child = array->child;
  while (child) {
    size++;
    child = child->next;
  }
  return size;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index) {
  if (!array || index < 0) return NULL;
  cJSON *child = array->child;
  while (child && index > 0) {
    child = child->next;
    index--;
  }
  return child;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object,
                                        const char *string) {
  if (!object || !string) return NULL;
  cJSON *child = object->child;
  while (child) {
    if (child->string && strcmp(child->string, string) == 0) return child;
    child = child->next;
  }
  return NULL;
}

char *cJSON_GetStringValue(const cJSON *item) {
  if (!cJSON_IsString(item)) return NULL;
  return item->valuestring;
}

double cJSON_GetNumberValue(const cJSON *item) {
  if (!cJSON_IsNumber(item)) return 0.0;
  return item->valuedouble;
}
