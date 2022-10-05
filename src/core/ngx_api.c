
/*
 * Copyright (C) Web Server LLC
 */


#include <ngx_config.h>
#include <ngx_core.h>


static ngx_int_t ngx_api_next_segment(ngx_str_t *path, ngx_str_t *name);

static ngx_int_t ngx_api_generic_iter(ngx_api_iter_ctx_t *ictx,
    ngx_api_ctx_t *actx);

static void *ngx_api_create_conf(ngx_cycle_t *cycle);
static void ngx_api_cleanup(void *data);
static void ngx_api_entries_free(ngx_api_entry_t *entry);
static ngx_api_entry_t *ngx_api_entries_dup(ngx_api_entry_t *entry,
    ngx_log_t *log);


static ngx_core_module_t  ngx_api_module_ctx = {
    ngx_string("api"),
    ngx_api_create_conf,
    NULL
};


ngx_module_t  ngx_api_module = {
    NGX_MODULE_V1,
    &ngx_api_module_ctx,                   /* module context */
    NULL,                                  /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_api_entry_t  ngx_api_status_entries[] = {
    ngx_api_null_entry
};


static ngx_api_entry_t  ngx_api_root_entries[] = {

    {
        .name      = ngx_string("status"),
        .handler   = ngx_api_object_handler,
        .data.ents = ngx_api_status_entries
    },

    ngx_api_null_entry
};


static ngx_api_entry_t  ngx_api_root_entry = {
    .name      = ngx_string("/"),
    .handler   = ngx_api_object_handler,
    .data.ents = ngx_api_root_entries
};


ngx_int_t
ngx_api_object_iterate(ngx_api_iter_pt iter, ngx_api_iter_ctx_t *ictx,
    ngx_api_ctx_t *actx)
{
    ngx_int_t         rc;
    ngx_str_t         name;
    ngx_data_item_t  *obj;

    if (ngx_api_next_segment(&actx->path, &name) == NGX_OK) {
        obj = NULL;

    } else {
        obj = ngx_data_new_object(actx->pool);
        if (obj == NULL) {
            return NGX_ERROR;
        }
    }

    for ( ;; ) {
        rc = iter(ictx, actx);

        if (rc != NGX_OK) {
            if (rc == NGX_DECLINED) {
                break;
            }

            return NGX_ERROR;
        }

        if (obj == NULL
            && (ictx->entry.name.len != name.len
                || ngx_strncmp(ictx->entry.name.data,
                               name.data, name.len) != 0))
        {
            continue;
        }

        actx->out = NULL;

        rc = ictx->entry.handler(ictx->entry.data, actx, ictx->ctx);

        if (obj == NULL) {
            return rc;
        }

        if (rc == NGX_DECLINED) {
            continue;
        }

        if (rc != NGX_OK) {
            return rc;
        }

        rc = ngx_data_object_add(obj, &ictx->entry.name, actx->out, actx->pool);
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (obj == NULL) {
        return NGX_API_NOT_FOUND;
    }

    actx->out = obj;

    return NGX_OK;
}


static ngx_int_t
ngx_api_next_segment(ngx_str_t *path, ngx_str_t *name)
{
    u_char  *p, *end;

    p = path->data;
    end = p + path->len;

    if (end - p <= 1) {
        return NGX_DECLINED;
    }

    p++; /* skip '/' */

    name->data = p;
    while (p < end && *p != '/') { p++; }
    name->len = p - name->data;

    path->len = end - p;
    path->data = p;

    return NGX_OK;
}


ngx_int_t
ngx_api_object_handler(ngx_api_entry_data_t data, ngx_api_ctx_t *actx,
    void *ctx)
{
    ngx_api_iter_ctx_t  ictx;

    ictx.ctx = ctx;
    ictx.elts = data.ents;

    return ngx_api_object_iterate(ngx_api_generic_iter, &ictx, actx);
}


static ngx_int_t
ngx_api_generic_iter(ngx_api_iter_ctx_t *ictx, ngx_api_ctx_t *actx)
{
    ngx_api_entry_t  *entry;

    entry = ictx->elts;

    if (ngx_api_is_null(entry)) {
        return NGX_DECLINED;
    }

    ictx->entry = *entry;
    ictx->elts = ++entry;

    return NGX_OK;
}


ngx_int_t
ngx_api_string_handler(ngx_api_entry_data_t data, ngx_api_ctx_t *actx,
    void *ctx)
{
    actx->out = ngx_data_new_string(data.str, actx->pool);

    return actx->out ? NGX_OK : NGX_ERROR;
}


ngx_int_t
ngx_api_struct_str_handler(ngx_api_entry_data_t data, ngx_api_ctx_t *actx,
    void *ctx)
{
    data.str = (ngx_str_t *) ((u_char *) ctx + data.off);

    return ngx_api_string_handler(data, actx, ctx);
}


static void *
ngx_api_create_conf(ngx_cycle_t *cycle)
{
    ngx_api_entry_t     *root;
    ngx_pool_cleanup_t  *cln;

    cln = ngx_pool_cleanup_add(cycle->pool, 0);
    if (cln == NULL) {
        return NULL;
    }

    root = ngx_alloc(sizeof(ngx_api_entry_t), cycle->log);
    if (root == NULL) {
        return NULL;
    }

    cln->data = root;
    cln->handler = ngx_api_cleanup;

    *root = ngx_api_root_entry;

    root->data.ents = ngx_api_entries_dup(root->data.ents, cycle->log);
    if (root->data.ents == NULL) {
        return NULL;
    }

    return root;
}


ngx_api_entry_t *
ngx_api_root(ngx_cycle_t *cycle)
{
    return (ngx_api_entry_t *) ngx_get_conf(cycle->conf_ctx, ngx_api_module);
}


static void
ngx_api_cleanup(void *data)
{
    ngx_api_entry_t *root = data;

    ngx_api_entries_free(root->data.ents);

    ngx_free(root);
}


static void
ngx_api_entries_free(ngx_api_entry_t *entry)
{
    void  *p;

    if (entry != NULL) {
        p = entry;

        while (!ngx_api_is_null(entry)) {
            if (entry->handler == &ngx_api_object_handler) {
                ngx_api_entries_free(entry->data.ents);
            }

            entry++;
        }

        ngx_free(p);
    }
}


static ngx_api_entry_t *
ngx_api_entries_dup(ngx_api_entry_t *entry, ngx_log_t *log)
{
    size_t            copy;
    ngx_uint_t        i;
    ngx_api_entry_t  *dup;

    for (i = 0; !ngx_api_is_null(&entry[i]); i++) { /* void */ }

    copy = sizeof(ngx_api_entry_t) * (i + 1);

    dup = ngx_alloc(copy, log);
    if (dup == NULL) {
        return NULL;
    }

    ngx_memcpy(dup, entry, copy);

    entry = dup;

    while (!ngx_api_is_null(entry)) {
        if (entry->handler == &ngx_api_object_handler) {
            entry->data.ents = ngx_api_entries_dup(entry->data.ents, log);
            if (entry->data.ents == NULL) {
                return NULL;
            }
        }

        entry++;
    }

    return dup;
}


ngx_int_t
ngx_api_add(ngx_cycle_t *cycle, const char *data, ngx_api_entry_t *child)
{
    ngx_str_t         path, name;
    ngx_uint_t        n;
    ngx_api_entry_t  *entry, *parent;

    entry = ngx_api_root(cycle);

    path.data = (u_char *) data;
    path.len = ngx_strlen(data);

    for ( ;; ) {
        if (entry->handler != &ngx_api_object_handler) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "cannot add at api path %s", data);
            return NGX_ERROR;
        }

        if (ngx_api_next_segment(&path, &name) != NGX_OK) {
            break;
        }

        entry = entry->data.ents;

        while (!ngx_api_is_null(entry)) {
            if (entry->name.len == name.len
                && ngx_strncmp(entry->name.data, name.data, name.len) == 0)
            {
                goto next;
            }

            entry++;
        }

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "api path %s not found", data);
        return NGX_ERROR;

    next:

        continue;
    }

    parent = entry;
    entry = entry->data.ents;

    name = child->name;

    while (!ngx_api_is_null(entry)) {
        if (entry->name.len == name.len
            && ngx_strncmp(entry->name.data, name.data, name.len) == 0)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "api path %s/%V already exists", data, &name);
            return NGX_ERROR;
        }

        entry++;
    }

    n = entry - parent->data.ents;

    entry = ngx_realloc(parent->data.ents, sizeof(ngx_api_entry_t) * (n + 2),
                        cycle->log);
    if (entry == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(entry + n, child, sizeof(ngx_api_entry_t));
    ngx_memzero(&entry[n + 1], sizeof(ngx_api_entry_t));

    parent->data.ents = entry;

    if (entry[n].handler == &ngx_api_object_handler) {
        entry[n].data.ents = ngx_api_entries_dup(entry[n].data.ents,
                                                 cycle->log);
        if (entry[n].data.ents == NULL) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}
