#ifndef PIKA_PARSER_H
#define PIKA_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#define PIKA_MAX_REQUEST 4096
#define PIKA_MAX_NAME 64 /* bucket */
#define PIKA_MAX_LIMIT 1000
#define PIKA_DEFAULT_LIMIT 100

typedef enum {
    PIKA_CREATE,
    PIKA_DELETE,
    PIKA_GET,
    PIKA_SET,
    PIKA_REMOVE,
} pika_method;

typedef enum {
    PARSE_OK,          /* one full request parsed */
    PARSE_INCOMPLETE,  /* no blank line yet: read more bytes */
    PARSE_BAD_REQUEST, /* reject; *err explains why */
    INTERNAL_ERROR,    /*  */
} parse_status;

typedef struct {
    pika_method method;
    char *bucket;
    char *key;   // key header
    char *value; // value header
    bool has_key, has_value;
    bool append;     // append header
    size_t limit;    // limit header
    size_t consumed; /* bytes used by this request */
} pika_request;

/* buf is NOT NUL-terminated and may contain any byte. */
parse_status pika_parse(const char *buf, size_t len, pika_request *out, char **err);
void free_pika(pika_request *req);

#endif
