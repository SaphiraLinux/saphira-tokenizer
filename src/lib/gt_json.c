/* gt_json.c — minimal, strict, read-only JSON scanner. */
#include "gt_json.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64

void gt_json_init(gt_json *j, const char *text, size_t len) {
    j->s = text;
    j->n = len;
    j->i = 0;
    j->depth = 0;
}

void gt_ws(gt_json *j) {
    while (j->i < j->n) {
        char c = j->s[j->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') j->i++;
        else break;
    }
}

int gt_peek(gt_json *j) {
    gt_ws(j);
    return j->i < j->n ? (unsigned char)j->s[j->i] : -1;
}

bool gt_eat(gt_json *j, char c) {
    if (gt_peek(j) == (unsigned char)c) { j->i++; return true; }
    return false;
}

bool gt_lit(gt_json *j, const char *lit) {
    gt_ws(j);
    size_t k = strlen(lit);
    if (j->i + k > j->n || memcmp(j->s + j->i, lit, k) != 0) return false;
    j->i += k;
    return true;
}

static int hex4(const char *p) {
    int v = 0;
    for (int k = 0; k < 4; k++) {
        char c = p[k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

gt_status gt_json_string(gt_json *j, gt_buf *out) {
    gt_ws(j);
    if (j->i >= j->n || j->s[j->i] != '"') return GT_ERR_BAD_FORMAT;
    j->i++;
    gt_buf_clear(out);
    while (j->i < j->n) {
        unsigned char c = (unsigned char)j->s[j->i];
        if (c == '"') { j->i++; return GT_OK; }
        if (c == '\\') {
            j->i++;
            if (j->i >= j->n) return GT_ERR_BAD_FORMAT;
            char e = j->s[j->i++];
            uint8_t outb;
            switch (e) {
                case '"': outb = '"'; break;
                case '\\': outb = '\\'; break;
                case '/': outb = '/'; break;
                case 'b': outb = '\b'; break;
                case 'f': outb = '\f'; break;
                case 'n': outb = '\n'; break;
                case 'r': outb = '\r'; break;
                case 't': outb = '\t'; break;
                case 'u': {
                    if (j->i + 4 > j->n) return GT_ERR_BAD_FORMAT;
                    int v = hex4(j->s + j->i);
                    if (v < 0) return GT_ERR_BAD_FORMAT;
                    j->i += 4;
                    /* Encode as UTF-8. Surrogate pairs are passed through as
                     * replacement rather than joined: a tokenizer key is not
                     * expected to contain one, and guessing would hide it. */
                    if (v >= 0xD800 && v <= 0xDFFF) v = 0xFFFD;
                    uint8_t tmp[4];
                    size_t tl = 0;
                    if (v < 0x80) { tmp[tl++] = (uint8_t)v; }
                    else if (v < 0x800) {
                        tmp[tl++] = (uint8_t)(0xC0 | (v >> 6));
                        tmp[tl++] = (uint8_t)(0x80 | (v & 0x3F));
                    } else {
                        tmp[tl++] = (uint8_t)(0xE0 | (v >> 12));
                        tmp[tl++] = (uint8_t)(0x80 | ((v >> 6) & 0x3F));
                        tmp[tl++] = (uint8_t)(0x80 | (v & 0x3F));
                    }
                    gt_status rc = gt_buf_append(out, tmp, tl);
                    if (rc != GT_OK) return rc;
                    continue;
                }
                default: return GT_ERR_BAD_FORMAT;
            }
            gt_status rc = gt_buf_push_byte(out, outb);
            if (rc != GT_OK) return rc;
            continue;
        }
        if (c < 0x20) return GT_ERR_BAD_FORMAT; /* raw control char */
        gt_status rc = gt_buf_push_byte(out, c);
        if (rc != GT_OK) return rc;
        j->i++;
    }
    return GT_ERR_BAD_FORMAT; /* unterminated */
}

gt_status gt_json_number_i64(gt_json *j, int64_t *out) {
    gt_ws(j);
    size_t start = j->i;
    if (j->i < j->n && (j->s[j->i] == '-' || j->s[j->i] == '+')) j->i++;
    while (j->i < j->n && ((j->s[j->i] >= '0' && j->s[j->i] <= '9') ||
                           j->s[j->i] == '.' || j->s[j->i] == 'e' ||
                           j->s[j->i] == 'E' || j->s[j->i] == '-' ||
                           j->s[j->i] == '+')) {
        j->i++;
    }
    if (j->i == start) return GT_ERR_BAD_FORMAT;
    char tmp[64];
    size_t len = j->i - start;
    if (len >= sizeof tmp) return GT_ERR_BAD_FORMAT;
    memcpy(tmp, j->s + start, len);
    tmp[len] = 0;
    char *end = NULL;
    long long v = strtoll(tmp, &end, 10);
    if (!end || *end != 0) return GT_ERR_BAD_FORMAT;
    *out = (int64_t)v;
    return GT_OK;
}

gt_status gt_json_skip_value(gt_json *j) {
    int c = gt_peek(j);
    if (c == '"') {
        gt_buf tmp;
        gt_buf_init(&tmp);
        gt_status rc = gt_json_string(j, &tmp);
        gt_buf_free(&tmp);
        return rc;
    }
    if (c == '{' || c == '[') {
        if (j->depth >= MAX_DEPTH) return GT_ERR_BAD_FORMAT;
        char open = (char)c, close = (c == '{') ? '}' : ']';
        j->depth++;
        j->i++;
        gt_ws(j);
        if (gt_peek(j) == close) { j->i++; j->depth--; return GT_OK; }
        for (;;) {
            if (open == '{') {
                gt_buf k;
                gt_buf_init(&k);
                gt_status rc = gt_json_string(j, &k);
                gt_buf_free(&k);
                if (rc != GT_OK) { j->depth--; return rc; }
                if (!gt_eat(j, ':')) { j->depth--; return GT_ERR_BAD_FORMAT; }
            }
            gt_status rc = gt_json_skip_value(j);
            if (rc != GT_OK) { j->depth--; return rc; }
            if (gt_eat(j, ',')) continue;
            if (gt_eat(j, close)) { j->depth--; return GT_OK; }
            j->depth--;
            return GT_ERR_BAD_FORMAT;
        }
    }
    if (c == 't' || c == 'f') {
        if (gt_lit(j, "true")) return GT_OK;
        if (gt_lit(j, "false")) return GT_OK;
        return GT_ERR_BAD_FORMAT;
    }
    if (c == 'n') return gt_lit(j, "null") ? GT_OK : GT_ERR_BAD_FORMAT;
    if (c == '-' || (c >= '0' && c <= '9')) {
        int64_t v;
        return gt_json_number_i64(j, &v);
    }
    return GT_ERR_BAD_FORMAT;
}

gt_status gt_json_object_key(gt_json *j, gt_buf *out) {
    gt_status rc = gt_json_string(j, out);
    if (rc != GT_OK) return rc;
    if (!gt_eat(j, ':')) return GT_ERR_BAD_FORMAT;
    return GT_OK;
}

gt_status gt_json_obj_begin(gt_json *j, bool *done) {
    if (!gt_eat(j, '{')) return GT_ERR_BAD_FORMAT;
    *done = false;
    if (gt_peek(j) == '}') { j->i++; *done = true; }
    return GT_OK;
}

gt_status gt_json_obj_next(gt_json *j, bool *done) {
    if (*done) return GT_ERR_BAD_FORMAT;
    if (gt_eat(j, ',')) {
        if (gt_peek(j) == '}') { j->i++; *done = true; return GT_OK; }
        return GT_OK;
    }
    if (gt_eat(j, '}')) { *done = true; return GT_OK; }
    return GT_ERR_BAD_FORMAT;
}

gt_status gt_json_arr_begin(gt_json *j, bool *done) {
    if (!gt_eat(j, '[')) return GT_ERR_BAD_FORMAT;
    *done = false;
    if (gt_peek(j) == ']') { j->i++; *done = true; }
    return GT_OK;
}

gt_status gt_json_arr_next(gt_json *j, bool *done) {
    if (*done) return GT_ERR_BAD_FORMAT;
    if (gt_eat(j, ',')) {
        if (gt_peek(j) == ']') { j->i++; *done = true; return GT_OK; }
        return GT_OK;
    }
    if (gt_eat(j, ']')) { *done = true; return GT_OK; }
    return GT_ERR_BAD_FORMAT;
}
