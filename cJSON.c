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

/* Minimal cJSON - parse-only, stripped for TinyCraft Launcher */
/* This version was modified by qwq672 to make it compatible with Tinycraft Launcher. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "cJSON.h"

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

static cJSON *cJSON_New_Item(void) {
    cJSON *node = (cJSON*)malloc(sizeof(cJSON));
    if (node) memset(node, 0, sizeof(cJSON));
    return node;
}

CJSON_PUBLIC(void) cJSON_Delete(cJSON *item) {
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

typedef struct { const unsigned char *content; size_t length, offset, depth; } parse_buffer;
#define can_read(b, s) ((b) && (b)->offset + (s) <= (b)->length)
#define cannot(b, s) (!can_read(b, s))

static int parse_value(cJSON *item, parse_buffer *buf);

static void skip_ws(parse_buffer *buf) {
    while (buf->offset < buf->length) {
        unsigned char c = buf->content[buf->offset];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        buf->offset++;
    }
}

static int parse_string(cJSON *item, parse_buffer *buf) {
    const unsigned char *input = buf->content + buf->offset, *end = buf->content + buf->length;
    size_t alloc_len = 0, i;
    if (*input != '\"') return 0;
    input++;
    { const unsigned char *ptr = input;
      while (ptr < end && *ptr != '\"') { if (*ptr == '\\') ptr++; ptr++; alloc_len++; } }
    unsigned char *output = (unsigned char*)malloc(alloc_len + 1);
    if (!output) return 0;
    for (i = 0; input < end && *input != '\"'; input++) {
        if (*input == '\\') { input++;
            switch (*input) {
                case 'b': output[i++]='\b'; break; case 'f': output[i++]='\f'; break;
                case 'n': output[i++]='\n'; break; case 'r': output[i++]='\r'; break;
                case 't': output[i++]='\t'; break;
                case '\"': case '\\': case '/': output[i++]=*input; break;
                case 'u': input+=4; output[i++]='?'; break;
                default: output[i++]=*input; break;
            }
        } else output[i++] = *input;
    }
    output[i] = '\0';
    if (*input == '\"') { input++; buf->offset = input - buf->content;
        item->valuestring = (char*)output; item->type = cJSON_String; return 1; }
    free(output); return 0;
}

static int parse_number(cJSON *item, parse_buffer *buf) {
    const unsigned char *input = buf->content + buf->offset;
    double number = 0; int sign = 1, scale = 0, subscale = 0, signsubscale = 1;
    if (*input == '-') { sign = -1; input++; buf->offset++; }
    if (*input == '0') { input++; buf->offset++; }
    if (*input >= '1' && *input <= '9')
        do { number = number * 10.0 + (*input++ - '0'); buf->offset++; }
        while (buf->offset < buf->length && *input >= '0' && *input <= '9');
    if (*input == '.' && buf->offset + 1 < buf->length) { input++; buf->offset++;
        while (buf->offset < buf->length && *input >= '0' && *input <= '9')
            { number = number * 10.0 + (*input++ - '0'); buf->offset++; scale--; } }
    if ((*input == 'e' || *input == 'E') && buf->offset + 1 < buf->length) { input++; buf->offset++;
        if (*input == '+') { input++; buf->offset++; }
        else if (*input == '-') { signsubscale = -1; input++; buf->offset++; }
        while (buf->offset < buf->length && *input >= '0' && *input <= '9')
            { subscale = subscale * 10 + (*input++ - '0'); buf->offset++; } }
    number = sign * number * pow(10.0, scale + subscale * signsubscale);
    item->valuedouble = number; item->valueint = (int)number; item->type = cJSON_Number;
    return 1;
}

static int parse_array(cJSON *item, parse_buffer *buf) {
    if (buf->content[buf->offset] != '[') return 0;
    buf->offset++; skip_ws(buf); item->type = cJSON_Array;
    if (buf->offset < buf->length && buf->content[buf->offset] == ']') { buf->offset++; return 1; }
    cJSON *child; item->child = child = cJSON_New_Item();
    if (!child || !parse_value(child, buf)) return 0;
    skip_ws(buf);
    while (buf->offset < buf->length && buf->content[buf->offset] == ',') {
        cJSON *nw; buf->offset++; skip_ws(buf);
        if (!(nw = cJSON_New_Item())) return 0;
        child->next = nw; nw->prev = child; child = nw;
        if (!parse_value(child, buf)) return 0;
        skip_ws(buf);
    }
    if (buf->offset < buf->length && buf->content[buf->offset] == ']') { buf->offset++; return 1; }
    return 0;
}

static int parse_object(cJSON *item, parse_buffer *buf) {
    if (buf->content[buf->offset] != '{') return 0;
    buf->offset++; skip_ws(buf); item->type = cJSON_Object;
    if (buf->offset < buf->length && buf->content[buf->offset] == '}') { buf->offset++; return 1; }
    cJSON *child; item->child = child = cJSON_New_Item();
    if (!child || buf->offset >= buf->length || buf->content[buf->offset] != '\"') return 0;
    if (!parse_string(child, buf)) return 0;
    child->string = child->valuestring; child->valuestring = NULL; child->type |= cJSON_StringIsConst;
    skip_ws(buf); if (buf->offset >= buf->length || buf->content[buf->offset] != ':') return 0;
    buf->offset++; skip_ws(buf); if (!parse_value(child, buf)) return 0; skip_ws(buf);
    while (buf->offset < buf->length && buf->content[buf->offset] == ',') {
        cJSON *nw; buf->offset++; skip_ws(buf);
        if (!(nw = cJSON_New_Item())) return 0;
        child->next = nw; nw->prev = child; child = nw;
        if (buf->offset >= buf->length || buf->content[buf->offset] != '\"') return 0;
        if (!parse_string(child, buf)) return 0;
        child->string = child->valuestring; child->valuestring = NULL; child->type |= cJSON_StringIsConst;
        skip_ws(buf); if (buf->offset >= buf->length || buf->content[buf->offset] != ':') return 0;
        buf->offset++; skip_ws(buf); if (!parse_value(child, buf)) return 0; skip_ws(buf);
    }
    if (buf->offset < buf->length && buf->content[buf->offset] == '}') { buf->offset++; return 1; }
    return 0;
}

static int parse_value(cJSON *item, parse_buffer *buf) {
    if (cannot(buf, 1)) return 0;
    skip_ws(buf);
    if (buf->depth > CJSON_NESTING_LIMIT) return 0;
    buf->depth++;
    if (buf->offset + 4 <= buf->length && !strncmp((const char*)(buf->content+buf->offset), "null", 4))
        { item->type = cJSON_NULL; buf->offset += 4; buf->depth--; return 1; }
    if (buf->offset + 5 <= buf->length && !strncmp((const char*)(buf->content+buf->offset), "false", 5))
        { item->type = cJSON_False; buf->offset += 5; buf->depth--; return 1; }
    if (buf->offset + 4 <= buf->length && !strncmp((const char*)(buf->content+buf->offset), "true", 4))
        { item->type = cJSON_True; buf->offset += 4; buf->depth--; return 1; }
    if (buf->offset < buf->length) {
        unsigned char c = buf->content[buf->offset];
        if (c == '\"') { buf->depth--; return parse_string(item, buf); }
        if (c == '-' || (c >= '0' && c <= '9')) { buf->depth--; return parse_number(item, buf); }
        if (c == '[') { buf->depth--; return parse_array(item, buf); }
        if (c == '{') { buf->depth--; return parse_object(item, buf); }
    }
    buf->depth--; return 0;
}

CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value) {
    if (!value) return NULL;
    parse_buffer buf; buf.content = (const unsigned char*)value;
    buf.length = strlen(value); buf.offset = 0; buf.depth = 0;
    cJSON *item = cJSON_New_Item();
    if (!item || !parse_value(item, &buf)) { cJSON_Delete(item); return NULL; }
    return item;
}

CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array) {
    int size = 0; cJSON *child;
    if (!array) return 0;
    for (child = array->child; child; child = child->next) size++;
    return size;
}
CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index) {
    cJSON *child; if (!array || !array->child) return NULL;
    for (child = array->child; child && index > 0; child = child->next) index--;
    return child;
}
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON *object, const char *string) {
    cJSON *child; if (!object || !string) return NULL;
    for (child = object->child; child; child = child->next)
        if (child->string && !strcmp(child->string, string)) return child;
    return NULL;
}
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON *item)
    { return item && (item->type & 0xFF) == cJSON_String; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON *item)
    { return item && (item->type & 0xFF) == cJSON_Number; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON *item)
    { return item && (item->type & 0xFF) == cJSON_Object; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON *item)
    { return item && (item->type & 0xFF) == cJSON_Array; }

/* ---- Minimal Create/Add/Print (Yggdrasil auth) ---- */
static unsigned char *str_dup(const unsigned char *str) {
    if (!str) return NULL;
    size_t len = strlen((const char*)str) + 1;
    unsigned char *copy = (unsigned char*)malloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void) {
    cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Object; return item;
}
static cJSON *arr_append(cJSON *array, cJSON *item) {
    if (!array || !item) return NULL;
    cJSON *child = array->child;
    if (!child) array->child = item;
    else { while (child->next) child = child->next; child->next = item; item->prev = child; }
    return item;
}
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) {
    if (!object || !string || !item) return 0;
    if (item->string) free(item->string);
    item->string = str_dup((const unsigned char*)string);
    return item->string && arr_append(object, item) != NULL;
}
CJSON_PUBLIC(cJSON *) cJSON_AddStringToObject(cJSON *object, const char *name, const char *string) {
    cJSON *item = cJSON_New_Item();
    if (item) { item->type = cJSON_String; item->valuestring = str_dup((const unsigned char*)string); }
    if (item && cJSON_AddItemToObject(object, name, item)) return item;
    if (item) cJSON_Delete(item); return NULL;
}
CJSON_PUBLIC(cJSON *) cJSON_AddNumberToObject(cJSON *object, const char *name, double number) {
    cJSON *item = cJSON_New_Item();
    if (item) { item->type = cJSON_Number; item->valuedouble = number; item->valueint = (int)number; }
    if (item && cJSON_AddItemToObject(object, name, item)) return item;
    if (item) cJSON_Delete(item); return NULL;
}
CJSON_PUBLIC(cJSON *) cJSON_AddBoolToObject(cJSON *object, const char *name, cJSON_bool boolean) {
    cJSON *item = cJSON_New_Item();
    if (item) item->type = boolean ? cJSON_True : cJSON_False;
    if (item && cJSON_AddItemToObject(object, name, item)) return item;
    if (item) cJSON_Delete(item); return NULL;
}

/* Minimal JSON serializer for auth requests */
static void jpr(const cJSON *item, char **buf, size_t *sz, size_t *cp) {
    if (!item) return;
    int t = item->type & 0xFF;
    if (t == cJSON_NULL) { *buf = realloc(*buf, *cp + 4); memcpy(*buf + *sz, "null", 4); *sz += 4; *cp = *sz; }
    else if (t == cJSON_False) { *buf = realloc(*buf, *cp + 5); memcpy(*buf + *sz, "false", 5); *sz += 5; *cp = *sz; }
    else if (t == cJSON_True) { *buf = realloc(*buf, *cp + 4); memcpy(*buf + *sz, "true", 4); *sz += 4; *cp = *sz; }
    else if (t == cJSON_Number) {
        char n[64]; int l = snprintf(n, sizeof(n), "%g", item->valuedouble);
        *buf = realloc(*buf, *cp + l); memcpy(*buf + *sz, n, l); *sz += l; *cp = *sz;
    } else if (t == cJSON_String) {
        if (item->valuestring) {
            size_t l = strlen(item->valuestring);
            *buf = realloc(*buf, *cp + l + 2);
            (*buf)[(*sz)++] = '\"'; memcpy(*buf + *sz, item->valuestring, l); *sz += l;
            (*buf)[(*sz)++] = '\"'; *cp = *sz;
        }
    } else if (t == cJSON_Object) {
        cJSON *c = item->child;
        *buf = realloc(*buf, *cp + 1); (*buf)[(*sz)++] = '{'; *cp = *sz;
        while (c) {
            if (c->string) {
                size_t l = strlen(c->string);
                *buf = realloc(*buf, *cp + l + 3);
                (*buf)[(*sz)++] = '\"'; memcpy(*buf + *sz, c->string, l); *sz += l;
                (*buf)[(*sz)++] = '\"'; (*buf)[(*sz)++] = ':'; *cp = *sz;
            }
            jpr(c, buf, sz, cp); c = c->next;
            if (c) { *buf = realloc(*buf, *cp + 1); (*buf)[(*sz)++] = ','; *cp = *sz; }
        }
        *buf = realloc(*buf, *cp + 1); (*buf)[(*sz)++] = '}'; *cp = *sz;
    }
}
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item) {
    char *buf = NULL; size_t sz = 0, cp = 0;
    jpr(item, &buf, &sz, &cp);
    buf = realloc(buf, cp + 1); buf[sz] = '\0'; return buf;
}
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void) { return NULL; }
