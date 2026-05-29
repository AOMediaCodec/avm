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

/* Minimal cJSON implementation for AVM xlayer config parsing.
 * Supports: objects, arrays, strings, numbers, booleans, null.
 * Based on the cJSON API by Dave Gamble. */

#ifndef CJSON_H
#define CJSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* cJSON Types: */
#define cJSON_Invalid (0)
#define cJSON_False (1 << 0)
#define cJSON_True (1 << 1)
#define cJSON_NULL (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw (1 << 7)

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

/* The cJSON structure: */
typedef struct cJSON {
  struct cJSON *next;
  struct cJSON *prev;
  struct cJSON *child;

  int type;
  char *valuestring;
  int valueint;
  double valuedouble;
  char *string;
} cJSON;

/* Supply a block of JSON, and this returns a cJSON object you can
 * interrogate. */
cJSON *cJSON_Parse(const char *value);

/* Delete a cJSON entity and all subentities. */
void cJSON_Delete(cJSON *item);

/* Returns the number of items in an array (or object). */
int cJSON_GetArraySize(const cJSON *array);

/* Retrieve item number "index" from array "array". Returns NULL if
 * unsuccessful. */
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);

/* Get item "string" from object. Case sensitive. */
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object,
                                        const char *string);

/* Check item type */
#define cJSON_IsInvalid(item) \
  ((item) == NULL || ((item)->type & 0xFF) == cJSON_Invalid)
#define cJSON_IsFalse(item) \
  ((item) != NULL && ((item)->type & 0xFF) == cJSON_False)
#define cJSON_IsTrue(item) \
  ((item) != NULL && ((item)->type & 0xFF) == cJSON_True)
#define cJSON_IsBool(item) \
  ((item) != NULL && (((item)->type & 0xFF) & (cJSON_True | cJSON_False)))
#define cJSON_IsNull(item) \
  ((item) != NULL && ((item)->type & 0xFF) == cJSON_NULL)
#define cJSON_IsNumber(item) \
  ((item) != NULL && ((item)->type & 0xFF) == cJSON_Number)
#define cJSON_IsString(item) \
  ((item) != NULL && ((item)->type & 0xFF) == cJSON_String)
#define cJSON_IsArray(item) \
  ((item) != NULL && ((item)->type & 0xFF) == cJSON_Array)
#define cJSON_IsObject(item) \
  ((item) != NULL && ((item)->type & 0xFF) == cJSON_Object)

/* Return string value, or NULL */
char *cJSON_GetStringValue(const cJSON *item);

/* Return number value, or 0 */
double cJSON_GetNumberValue(const cJSON *item);

/* Macro to iterate over array/object children */
#define cJSON_ArrayForEach(element, array) \
  for (element = (array != NULL) ? (array)->child : NULL; element != NULL; \
       element = element->next)

#ifdef __cplusplus
}
#endif

#endif /* CJSON_H */
