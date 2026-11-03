#include "parser.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum {
    METHOD,
    HEADER,
    END,
} state_t;

typedef enum {
    METHOD_NAME,
    METHOD_BUCKET_NAME,
    METHOD_VERSION,
    DONE,
} method_state_t;

typedef enum {
    KEY,
    VALUE,
} header_state_t;

// strict rule -> only one space between them.
parse_status parse_method(const char *buf, size_t len, pika_request *out, char **err) {
    method_state_t cur_state = METHOD_NAME;
    for (size_t i = 0, j = 0; i < len; i++) {
        if (cur_state == DONE) {
            *err = "nodf";
            return PARSE_BAD_REQUEST;
        }
        if (i == len - 1 && cur_state == METHOD_VERSION) {
            char version[10];
            memcpy(version, buf + j, i - j + 1);
            version[i - j + 1] = '\0';
            if (strcmp(version, "PIKA/1.0") != 0) {
                *err = "version not matched";
                return PARSE_BAD_REQUEST;
            }
            cur_state = DONE;
            j = i + 1;
        }
        if (buf[i] == ' ' && cur_state == METHOD_NAME) {
            if (i - j > 7)
                return PARSE_BAD_REQUEST;
            char method[7];
            memcpy(method, buf + j, i - j);
            method[i - j] = '\0';
            if (!strcmp(method, "GET")) {
                out->method = PIKA_GET;
            } else if (!strcmp(method, "SET")) {
                out->method = PIKA_SET;
            } else if (!strcmp(method, "CREATE")) {
                out->method = PIKA_CREATE;
            } else if (!strcmp(method, "DELETE")) {
                out->method = PIKA_DELETE;
            } else if (!strcmp(method, "REMOVE")) {
                out->method = PIKA_REMOVE;
            } else {
                *err = "method mismatch";
                return PARSE_BAD_REQUEST;
            }
            cur_state = METHOD_BUCKET_NAME;
            j = i + 1;
        } else if (buf[i] == ' ' && cur_state == METHOD_BUCKET_NAME) {
            if (i - j > PIKA_MAX_NAME)
                return PARSE_BAD_REQUEST;
            out->bucket = calloc(i - j + 1, sizeof(char));
            if (out->bucket == NULL) {
                return INTERNAL_ERROR;
            }
            memcpy(out->bucket, buf + j, i - j);
            out->bucket[i - j] = '\0';
            cur_state = METHOD_VERSION;
            j = i + 1;
        } else if (buf[i] == ' ') {
            *err = "nodf-space";
            return PARSE_BAD_REQUEST;
        }
    }
    return PARSE_OK;
}

parse_status set_headers(char *key, char *value, pika_request *out, char **err) {
    parse_status status = PARSE_BAD_REQUEST;
    bool keep_value = true;

    if (!key || !value) {
        *err = "malformed header";
        keep_value = false;
    } else if (!strcasecmp(key, "key")) {
        if (out->has_key) {
            *err = "duplicate header";
            keep_value = false;
        } else {
            out->has_key = true;
            out->key = value;
            status = PARSE_OK;
        }
    } else if (!strcasecmp(key, "value")) {
        if (out->has_value) {
            *err = "duplicate header";
            keep_value = false;
        } else {
            out->has_value = true;
            out->value = value;
            status = PARSE_OK;
        }
    } else if (!strcasecmp(key, "append") && !strcmp(value, "true")) {
        out->append = true;
        status = PARSE_OK;
        keep_value = false;
    } else if (!strcasecmp(key, "append") && !strcmp(value, "false")) {
        out->append = false;
        status = PARSE_OK;
        keep_value = false;
    } else if (!strcasecmp(key, "limit")) {
        if (out->limit != 0) {
            *err = "duplicate header";
        } else {
            out->limit = atoi(value);
            status = PARSE_OK;
        }
        keep_value = false;
    } else {
        *err = "wrong header name";
        keep_value = false;
    }

    free(key);
    if (!keep_value)
        free(value);
    return status;
}

parse_status parser_header(const char *buf, size_t len, pika_request *out, char **err) {
    header_state_t cur_state = KEY;
    bool colon_found = false;
    char *header_key = NULL;
    char *header_value = NULL;
    for (size_t i = 0, j = 0; i < len; i++) {
        if (i == len - 1 && cur_state == VALUE) {
            header_value = calloc(len - j + 1, sizeof(char));
            if (header_value == NULL) {
                if (header_key) {
                    free(header_key);
                }
                return INTERNAL_ERROR;
            }
            memcpy(header_value, buf + j, len - j);
            header_value[len - j] = '\0';
        }
        if (buf[i] == ':') {
            if (colon_found == true && cur_state == VALUE) {
                continue;
            }
            colon_found = true;
            header_key = calloc(i - j + 1, sizeof(char));
            if (header_key == NULL) {
                return INTERNAL_ERROR;
            }
            memcpy(header_key, buf + j, i - j);
            header_key[i - j] = '\0';
            j = i + 2;
            i++;
            cur_state = VALUE;
        } else if (buf[i] == ' ') {
            if (header_key) {
                free(header_key);
            }
            if (header_value) {
                free(header_value);
            }
            *err = "wrong header format - extra space";
            return PARSE_BAD_REQUEST;
        }
    }
    return set_headers(header_key, header_value, out, err);
}

parse_status pika_parse(const char *buf, size_t len, pika_request *out, char **err) {
    out->consumed = len;
    state_t cur_state = METHOD;
    bool prev_crlr = false;
    for (size_t i = 0, j = 0; i + 1 < len;) {
        if (cur_state == END) {
            *err = "single request violated";
            return PARSE_BAD_REQUEST;
        }
        if (buf[i] != '\r') {
            i++;
            prev_crlr = false;
            continue;
        }
        if (i < len && buf[i + 1] == '\n') {
            if (prev_crlr) {
                cur_state = END;
            }
            prev_crlr = true;
            if (cur_state == METHOD) {
                parse_status status = parse_method(buf + j, i - j, out, err);
                if (status != PARSE_OK)
                    return status;
                cur_state = HEADER;
            } else if (cur_state == HEADER) {
                if (out->method == PIKA_DELETE || out->method == PIKA_CREATE) {
                    *err = "headers on create | delete ";
                    return PARSE_BAD_REQUEST;
                }
                parse_status status = parser_header(buf + j, i - j, out, err);
                if (status != PARSE_OK)
                    return status;
            }
            i += 2;
            j = i;
        } else {
            i++;
            prev_crlr = false;
        }
    }
    if (cur_state != END) {
        *err = "incomplte";
        return PARSE_INCOMPLETE;
    }
    if (out->method == PIKA_SET && !out->has_value) {
        *err = "value not present";
        return PARSE_BAD_REQUEST;
    }
    return PARSE_OK;
}

void free_pika(pika_request *req) {
    if (req == NULL)
        return;
    if (req->key) {
        free(req->key);
        req->key = NULL;
    }
    if (req->value) {
        free(req->value);
        req->value = NULL;
    }
    if (req->bucket) {
        free(req->bucket);
        req->bucket = NULL;
    }
}
