/* Copyright (C) by agentzh */

#define DDEBUG 0

#include "ddebug.h"
#include "ngx_http_echo_module.h"

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_log.h>
#include <stdlib.h>

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;

static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

static ngx_flag_t ngx_http_echo_filter_used = 0;

static ngx_buf_t ngx_http_echo_space_buf;

static ngx_buf_t ngx_http_echo_newline_buf;

static ngx_int_t ngx_http_echo_init_ctx(ngx_http_request_t *r,
        ngx_http_echo_ctx_t **ctx_ptr);

/* (optional) filters initialization */
static ngx_int_t ngx_http_echo_filter_init(ngx_conf_t *cf);

static ngx_int_t ngx_http_echo_header_filter(ngx_http_request_t *r);

static ngx_int_t ngx_http_echo_body_filter(ngx_http_request_t *r, ngx_chain_t *in);

/* module init headler */
static ngx_int_t ngx_http_echo_init(ngx_conf_t *cf);

/* config init handler */
static void* ngx_http_echo_create_conf(ngx_conf_t *cf);

/* config directive handlers */
static char* ngx_http_echo_echo(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char* ngx_http_echo_echo_client_request_headers(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static char* ngx_http_echo_echo_sleep(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);

static char* ngx_http_echo_echo_flush(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char* ngx_http_echo_echo_blocking_sleep(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static char* ngx_http_echo_echo_reset_timer(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static char* ngx_http_echo_echo_before_body(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static char* ngx_http_echo_echo_after_body(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static char* ngx_http_echo_echo_location_async(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static char* ngx_http_echo_helper(ngx_http_echo_opcode_t opcode,
        ngx_http_echo_cmd_category_t cat,
        ngx_conf_t *cf, ngx_command_t *cmd, void* conf);

/* main content handler */
static ngx_int_t ngx_http_echo_handler(ngx_http_request_t *r);

static ngx_int_t ngx_http_echo_exec_echo(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args);

static ngx_int_t ngx_http_echo_exec_echo_client_request_headers(
        ngx_http_request_t* r, ngx_http_echo_ctx_t *ctx);

static ngx_int_t ngx_http_echo_exec_echo_sleep(
        ngx_http_request_t *r, ngx_http_echo_ctx_t *ctx,
        ngx_array_t *computed_args);

static ngx_int_t ngx_http_echo_exec_echo_flush(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx);

static ngx_int_t ngx_http_echo_exec_echo_blocking_sleep(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args);

static ngx_int_t ngx_http_echo_exec_echo_reset_timer(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx);

static ngx_int_t ngx_http_echo_exec_echo_location_async(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args);

static ngx_int_t ngx_http_echo_eval_cmd_args(ngx_http_request_t *r,
        ngx_http_echo_cmd_t *cmd, ngx_array_t *computed_args);

static ngx_int_t ngx_http_echo_send_header_if_needed(ngx_http_request_t* r,
        ngx_http_echo_ctx_t *ctx);

static ngx_int_t ngx_http_echo_send_chain_link(ngx_http_request_t* r,
        ngx_http_echo_ctx_t *ctx, ngx_chain_t *cl);

/* write event handler for echo_sleep */
static void ngx_http_echo_post_sleep(ngx_http_request_t *r);

/* filter handlers */
static ngx_int_t ngx_http_echo_exec_filter_cmds(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *cmds, ngx_uint_t *iterator);

/* variable handler */
static ngx_int_t ngx_http_echo_timer_elapsed_variable(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_echo_add_variables(ngx_conf_t *cf);

static ngx_http_variable_t ngx_http_echo_variables[] = {
    { ngx_string("echo_timer_elapsed"), NULL,
      ngx_http_echo_timer_elapsed_variable, 0,
      NGX_HTTP_VAR_NOCACHEABLE, 0 }
};

static ngx_command_t  ngx_http_echo_commands[] = {

    { ngx_string("echo"),
      NGX_HTTP_LOC_CONF|NGX_CONF_ANY,
      ngx_http_echo_echo,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, handler_cmds),
      NULL },
    /* TODO echo_client_request_headers should take an
     * optional argument to change output format to
     * "html" or other things */
    { ngx_string("echo_client_request_headers"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_echo_echo_client_request_headers,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, handler_cmds),
      NULL },
    { ngx_string("echo_sleep"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_echo_echo_sleep,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, handler_cmds),
      NULL },
    { ngx_string("echo_flush"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_echo_echo_flush,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, handler_cmds),
      NULL },
    { ngx_string("echo_blocking_sleep"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_echo_echo_blocking_sleep,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, handler_cmds),
      NULL },
    { ngx_string("echo_reset_timer"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_echo_echo_reset_timer,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, handler_cmds),
      NULL },
    { ngx_string("echo_before_body"),
      NGX_HTTP_LOC_CONF|NGX_CONF_ANY,
      ngx_http_echo_echo_before_body,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, before_body_cmds),
      NULL },
    { ngx_string("echo_after_body"),
      NGX_HTTP_LOC_CONF|NGX_CONF_ANY,
      ngx_http_echo_echo_after_body,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_echo_loc_conf_t, after_body_cmds),
      NULL },
    { ngx_string("echo_location_async"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_echo_echo_location_async,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    /* TODO
    { ngx_string("echo_location"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_echo_echo_location,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
    */

      ngx_null_command
};

static ngx_http_module_t ngx_http_echo_module_ctx = {
    /* TODO we could add our own variables here... */
    ngx_http_echo_init,                         /* preconfiguration */
    ngx_http_echo_filter_init,                  /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    ngx_http_echo_create_conf, /* create location configuration */
    NULL                           /* merge location configuration */
};

ngx_module_t ngx_http_echo_module = {
    NGX_MODULE_V1,
    &ngx_http_echo_module_ctx, /* module context */
    ngx_http_echo_commands,   /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_echo_init(ngx_conf_t *cf) {
    static u_char space_str[]   = " ";
    static u_char newline_str[] = "\n";

    DD("global init...");

    ngx_memzero(&ngx_http_echo_space_buf, sizeof(ngx_buf_t));
    ngx_http_echo_space_buf.memory = 1;
    ngx_http_echo_space_buf.start =
        ngx_http_echo_space_buf.pos =
            space_str;
    ngx_http_echo_space_buf.end =
        ngx_http_echo_space_buf.last =
            space_str + sizeof(space_str) - 1;

    ngx_memzero(&ngx_http_echo_newline_buf, sizeof(ngx_buf_t));
    ngx_http_echo_newline_buf.memory = 1;
    ngx_http_echo_newline_buf.start =
        ngx_http_echo_newline_buf.pos =
            newline_str;
    ngx_http_echo_newline_buf.end =
        ngx_http_echo_newline_buf.last =
            newline_str + sizeof(newline_str) - 1;

    return ngx_http_echo_add_variables(cf);
}

static ngx_int_t
ngx_http_echo_filter_init (ngx_conf_t *cf) {
    if (ngx_http_echo_filter_used) {
        DD("top header filter: %ld", (unsigned long) ngx_http_top_header_filter);
        ngx_http_next_header_filter = ngx_http_top_header_filter;
        ngx_http_top_header_filter  = ngx_http_echo_header_filter;

        DD("top body filter: %ld", (unsigned long) ngx_http_top_body_filter);
        ngx_http_next_body_filter = ngx_http_top_body_filter;
        ngx_http_top_body_filter  = ngx_http_echo_body_filter;
    }

    return NGX_OK;
}

static void*
ngx_http_echo_create_conf(ngx_conf_t *cf) {
    ngx_http_echo_loc_conf_t *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_echo_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    return conf;
}

static char*
ngx_http_echo_helper(ngx_http_echo_opcode_t opcode,
        ngx_http_echo_cmd_category_t cat,
        ngx_conf_t *cf, ngx_command_t *cmd, void* conf) {
    ngx_http_core_loc_conf_t        *clcf;
    /* ngx_http_echo_loc_conf_t        *ulcf = conf; */
    ngx_array_t                     **args_ptr;
    ngx_http_script_compile_t       sc;
    ngx_str_t                       *raw_args;
    ngx_http_echo_arg_template_t    *arg;
    ngx_array_t                     **cmds_ptr;
    ngx_http_echo_cmd_t             *echo_cmd;
    ngx_uint_t                       i, n;

    /* cmds_ptr points to ngx_http_echo_loc_conf_t's
     * handler_cmds, before_body_cmds, or after_body_cmds
     * array, depending on the actual offset */
    cmds_ptr = (ngx_array_t**)(((u_char*)conf) + cmd->offset);
    if (*cmds_ptr == NULL) {
        *cmds_ptr = ngx_array_create(cf->pool, 1,
                sizeof(ngx_http_echo_cmd_t));
        if (*cmds_ptr == NULL) {
            return NGX_CONF_ERROR;
        }
        if (cat == echo_handler_cmd) {
            DD("registering the content handler");
            /* register the content handler */
            clcf = ngx_http_conf_get_module_loc_conf(cf,
                    ngx_http_core_module);
            if (clcf == NULL) {
                return NGX_CONF_ERROR;
            }
            DD("registering the content handler (2)");
            clcf->handler = ngx_http_echo_handler;
        } else {
            DD("filter used = 1");
            ngx_http_echo_filter_used = 1;
        }
    }
    echo_cmd = ngx_array_push(*cmds_ptr);
    if (echo_cmd == NULL) {
        return NGX_CONF_ERROR;
    }
    echo_cmd->opcode = opcode;
    args_ptr = &echo_cmd->args;
    *args_ptr = ngx_array_create(cf->pool, 1,
            sizeof(ngx_http_echo_arg_template_t));
    if (*args_ptr == NULL) {
        return NGX_CONF_ERROR;
    }
    raw_args = cf->args->elts;
    /* we skip the first arg and start from the second */
    for (i = 1 ; i < cf->args->nelts; i++) {
        arg = ngx_array_push(*args_ptr);
        if (arg == NULL) {
            return NGX_CONF_ERROR;
        }
        arg->raw_value = raw_args[i];
        DD("found raw arg %s", raw_args[i].data);
        arg->lengths = NULL;
        arg->values  = NULL;
        n = ngx_http_script_variables_count(&arg->raw_value);
        if (n > 0) {
            ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
            sc.cf = cf;
            sc.source = &arg->raw_value;
            sc.lengths = &arg->lengths;
            sc.values = &arg->values;
            sc.variables = n;
            sc.complete_lengths = 1;
            sc.complete_values = 1;
            if (ngx_http_script_compile(&sc) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    } /* end for */
    return NGX_CONF_OK;
}

static char*
ngx_http_echo_echo(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    DD("in echo_echo...");
    return ngx_http_echo_helper(echo_opcode_echo,
            echo_handler_cmd,
            cf, cmd, conf);
}

static char*
ngx_http_echo_echo_client_request_headers(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf) {
    return ngx_http_echo_helper(
            echo_opcode_echo_client_request_headers,
            echo_handler_cmd,
            cf, cmd, conf);
}

static char*
ngx_http_echo_echo_sleep(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    DD("in echo_sleep...");
    return ngx_http_echo_helper(echo_opcode_echo_sleep,
            echo_handler_cmd,
            cf, cmd, conf);
}

static char*
ngx_http_echo_echo_flush(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    DD("in echo_flush...");
    return ngx_http_echo_helper(echo_opcode_echo_flush,
            echo_handler_cmd,
            cf, cmd, conf);
}

static char*
ngx_http_echo_echo_blocking_sleep(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    DD("in echo_blocking_sleep...");
    return ngx_http_echo_helper(echo_opcode_echo_blocking_sleep,
            echo_handler_cmd,
            cf, cmd, conf);
}

static char*
ngx_http_echo_echo_reset_timer(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf) {
    return ngx_http_echo_helper(echo_opcode_echo_reset_timer,
            echo_handler_cmd,
            cf, cmd, conf);
}

static char*
ngx_http_echo_echo_before_body(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf) {
    DD("processing echo_before_body directive...");
    return ngx_http_echo_helper(echo_opcode_echo_before_body,
            echo_filter_cmd,
            cf, cmd, conf);
}

static char*
ngx_http_echo_echo_after_body(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf) {
    return ngx_http_echo_helper(echo_opcode_echo_after_body,
            echo_filter_cmd,
            cf, cmd, conf);
}

static char*
 ngx_http_echo_echo_location_async(ngx_conf_t *cf, ngx_command_t *cmd,
         void *conf) {

#if ! defined(nginx_version)

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Directive echo_location_async does not work with nginx "
            "versions ealier than 0.7.46.");

    return NGX_CONF_ERROR;

#else

    return ngx_http_echo_helper(echo_opcode_echo_location_async,
            echo_handler_cmd,
            cf, cmd, conf);

#endif

}

static ngx_int_t
ngx_http_echo_init_ctx(ngx_http_request_t *r, ngx_http_echo_ctx_t **ctx_ptr) {
    *ctx_ptr = ngx_pcalloc(r->pool, sizeof(ngx_http_echo_ctx_t));
    if (*ctx_ptr == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_handler(ngx_http_request_t *r) {
    ngx_http_echo_loc_conf_t    *elcf;
    ngx_http_echo_ctx_t         *ctx;
    ngx_int_t                   rc;
    ngx_array_t                 *cmds;
    ngx_array_t                 *computed_args = NULL;
    ngx_http_echo_cmd_t         *cmd;
    ngx_http_echo_cmd_t         *cmd_elts;

    elcf = ngx_http_get_module_loc_conf(r, ngx_http_echo_module);
    cmds = elcf->handler_cmds;
    if (cmds == NULL) {
        return NGX_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_echo_module);
    if (ctx == NULL) {
        rc = ngx_http_echo_init_ctx(r, &ctx);
        if (rc != NGX_OK) {
            return rc;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_echo_module);
    }

    DD("exec handler: %s: %u", r->uri.data, ctx->next_handler_cmd);

    cmd_elts = cmds->elts;
    for (; ctx->next_handler_cmd < cmds->nelts; ctx->next_handler_cmd++) {
        cmd = &cmd_elts[ctx->next_handler_cmd];

        /* evaluate arguments for the current cmd (if any) */
        if (cmd->args) {
            computed_args = ngx_array_create(r->pool, cmd->args->nelts,
                    sizeof(ngx_str_t));
            if (computed_args == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            rc = ngx_http_echo_eval_cmd_args(r, cmd, computed_args);
            if (rc != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "Failed to evaluate arguments for "
                        "the directive.");
                return rc;
            }
        }

        /* do command dispatch based on the opcode */
        switch (cmd->opcode) {
            case echo_opcode_echo:
                /* XXX moved the following code to a separate
                 * function */
                DD("found echo opcode");
                rc = ngx_http_echo_exec_echo(r, ctx, computed_args);
                if (rc != NGX_OK) {
                    return rc;
                }
                break;
            case echo_opcode_echo_client_request_headers:
                rc = ngx_http_echo_exec_echo_client_request_headers(r,
                        ctx);
                if (rc != NGX_OK) {
                    return rc;
                }
                break;
            case echo_opcode_echo_location_async:
                DD("found opcode echo location async...");
                rc = ngx_http_echo_exec_echo_location_async(r, ctx,
                        computed_args);
                if (rc != NGX_OK) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "Failed to issue subrequest for "
                            "echo_location_async");
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
                break;
            case echo_opcode_echo_sleep:
                return ngx_http_echo_exec_echo_sleep(r, ctx, computed_args);
                break;
            case echo_opcode_echo_flush:
                rc = ngx_http_echo_exec_echo_flush(r, ctx);

                if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
                    return rc;
                }

                if (rc == NGX_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "Failed to flush the output", cmd->opcode);
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                break;
            case echo_opcode_echo_blocking_sleep:
                rc = ngx_http_echo_exec_echo_blocking_sleep(r, ctx,
                        computed_args);
                if (rc == NGX_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "Failed to run blocking sleep", cmd->opcode);
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                break;
            case echo_opcode_echo_reset_timer:
                rc = ngx_http_echo_exec_echo_reset_timer(r, ctx);
                if (rc != NGX_OK) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
                break;
            default:
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "Unknown opcode: %d", cmd->opcode);
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
                break;
        }
    }

    return ngx_http_echo_send_chain_link(r, ctx, NULL /* indicate LAST */);
}

static ngx_int_t
ngx_http_echo_exec_echo(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args) {
    ngx_uint_t                  i;

    ngx_buf_t                   *space_buf;
    ngx_buf_t                   *newline_buf;
    ngx_buf_t                   *buf;

    ngx_str_t                   *computed_arg;
    ngx_str_t                   *computed_arg_elts;

    ngx_chain_t *cl  = NULL; /* the head of the chain link */
    ngx_chain_t **ll = NULL;  /* always point to the address of the last link */

    DD("now exec echo...");

    if (computed_args == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    computed_arg_elts = computed_args->elts;
    for (i = 0; i < computed_args->nelts; i++) {
        computed_arg = &computed_arg_elts[i];
        buf = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
        if (buf == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        buf->start = buf->pos = computed_arg->data;
        buf->last = buf->end = computed_arg->data +
            computed_arg->len;
        buf->memory = 1;

        if (cl == NULL) {
            cl = ngx_alloc_chain_link(r->pool);
            if (cl == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            cl->buf  = buf;
            cl->next = NULL;
            ll = &cl->next;
        } else {
            /* append a space first */
            *ll = ngx_alloc_chain_link(r->pool);
            if (*ll == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            space_buf = ngx_calloc_buf(r->pool);
            if (space_buf == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            /* nginx clears buf flags at the end of each request handling,
             * so we have to make a clone here. */
            *space_buf = ngx_http_echo_space_buf;

            (*ll)->buf = space_buf;
            (*ll)->next = NULL;

            ll = &(*ll)->next;

            /* then append the buf */
            *ll = ngx_alloc_chain_link(r->pool);
            if (*ll == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            (*ll)->buf  = buf;
            (*ll)->next = NULL;

            ll = &(*ll)->next;
        }
    } /* end for */

    /* append the newline character */
    /* TODO add support for -n option to suppress
     * the trailing newline */
    newline_buf = ngx_calloc_buf(r->pool);
    if (newline_buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    *newline_buf = ngx_http_echo_newline_buf;
    newline_buf->last_in_chain = 1;

    if (cl == NULL) {
        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        cl->buf = newline_buf;
        cl->next = NULL;
        /* ll = &cl->next; */
    } else {
        *ll = ngx_alloc_chain_link(r->pool);
        if (*ll == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        (*ll)->buf  = newline_buf;
        (*ll)->next = NULL;
        /* ll = &(*ll)->next; */
    }

    return ngx_http_echo_send_chain_link(r, ctx, cl);
}

static ngx_int_t
ngx_http_echo_send_header_if_needed(ngx_http_request_t* r,
        ngx_http_echo_ctx_t *ctx) {
    ngx_int_t   rc;

    if ( ! ctx->headers_sent ) {
        ctx->headers_sent = 1;
        r->headers_out.status = NGX_HTTP_OK;
        if (ngx_http_set_content_type(r) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        rc = ngx_http_send_header(r);
    }
    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_send_chain_link(ngx_http_request_t* r,
        ngx_http_echo_ctx_t *ctx, ngx_chain_t *cl) {
    ngx_int_t   rc;

    rc = ngx_http_echo_send_header_if_needed(r, ctx);
    if (r->header_only || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    if (cl == NULL) {

#if defined(nginx_version) && nginx_version <= 8004

        /* earlier versions of nginx does not allow subrequests
            to send last_buf themselves */
        if (r != r->main) {
            return NGX_OK;
        }

#endif

        rc = ngx_http_send_special(r, NGX_HTTP_LAST);
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }

        return NGX_OK;
    }

    return ngx_http_output_filter(r, cl);
}

static ngx_int_t
ngx_http_echo_header_filter(ngx_http_request_t *r) {
    ngx_http_echo_loc_conf_t    *conf;
    ngx_http_echo_ctx_t         *ctx;
    ngx_int_t                   rc;

    DD("We're in the header filter...");

    ctx = ngx_http_get_module_ctx(r, ngx_http_echo_module);

    /* XXX we should add option to insert contents for responses
     * of non-200 status code here... */
    if (r->headers_out.status != NGX_HTTP_OK) {
        if (ctx != NULL) {
            ctx->skip_filter = 1;
        }
        return ngx_http_next_header_filter(r);
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_echo_module);
    if (conf->before_body_cmds == NULL && conf->after_body_cmds == NULL) {
        if (ctx != NULL) {
            ctx->skip_filter = 1;
        }
        return ngx_http_next_header_filter(r);
    }

    if (ctx == NULL) {
        rc = ngx_http_echo_init_ctx(r, &ctx);
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }
        ctx->headers_sent = 1;
        ngx_http_set_ctx(r, ctx, ngx_http_echo_module);
    }

    /* enable streaming here (use chunked encoding) */
    ngx_http_clear_content_length(r);
    ngx_http_clear_accept_ranges(r);

    return ngx_http_next_header_filter(r);
}

static ngx_int_t
ngx_http_echo_body_filter(ngx_http_request_t *r, ngx_chain_t *in) {
    ngx_http_echo_ctx_t         *ctx;
    ngx_int_t                   rc;
    ngx_http_echo_loc_conf_t    *conf;
    ngx_flag_t                  last;
    ngx_chain_t                 *cl;

    if (in == NULL || r->header_only) {
        return ngx_http_next_body_filter(r, in);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_echo_module);

    if (ctx == NULL || ctx->skip_filter) {
        return ngx_http_next_body_filter(r, in);
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_echo_module);

    if (!ctx->before_body_sent) {
        ctx->before_body_sent = 1;

        if (conf->before_body_cmds != NULL) {
            rc = ngx_http_echo_exec_filter_cmds(r, ctx, conf->before_body_cmds,
                    &ctx->next_before_body_cmd);
            if (rc != NGX_OK) {
                return NGX_ERROR;
            }
        }
    }

    if (conf->after_body_cmds == NULL) {
        ctx->skip_filter = 1;
        return ngx_http_next_body_filter(r, in);
    }

    last = 0;

    for (cl = in; cl; cl = cl->next) {
        if (cl->buf->last_buf) {
            cl->buf->last_buf = 0;
            cl->buf->sync = 1;
            last = 1;
        }
    }

    rc = ngx_http_next_body_filter(r, in);

    if (rc == NGX_ERROR || !last) {
        return rc;
    }

    DD("exec filter cmds for after body cmds");
    rc = ngx_http_echo_exec_filter_cmds(r, ctx, conf->after_body_cmds, &ctx->next_after_body_cmd);
    if (rc != NGX_OK) {
        DD("FAILED: exec filter cmds for after body cmds");
        return NGX_ERROR;
    }

    ctx->skip_filter = 1;

    DD("after body cmds executed...terminating...");

    return ngx_http_send_special(r, NGX_HTTP_LAST);
}

static ngx_int_t
ngx_http_echo_exec_filter_cmds(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *cmds,
        ngx_uint_t *iterator) {
    ngx_int_t                   rc;
    ngx_array_t                 *computed_args = NULL;
    ngx_http_echo_cmd_t         *cmd;
    ngx_http_echo_cmd_t         *cmd_elts;

    cmd_elts = cmds->elts;
    for (; *iterator < cmds->nelts; (*iterator)++) {
        cmd = &cmd_elts[*iterator];

        /* evaluate arguments for the current cmd (if any) */
        if (cmd->args) {
            computed_args = ngx_array_create(r->pool, cmd->args->nelts,
                    sizeof(ngx_str_t));
            if (computed_args == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            rc = ngx_http_echo_eval_cmd_args(r, cmd, computed_args);
            if (rc != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "Failed to evaluate arguments for "
                        "the directive.");
                return rc;
            }
        }

        /* do command dispatch based on the opcode */
        switch (cmd->opcode) {
            case echo_opcode_echo_before_body:
            case echo_opcode_echo_after_body:
                DD("exec echo_before_body or echo_after_body...");
                rc = ngx_http_echo_exec_echo(r, ctx, computed_args);
                if (rc != NGX_OK) {
                    return rc;
                }
                break;
            default:
                break;
        }
    }

    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_eval_cmd_args(ngx_http_request_t *r,
        ngx_http_echo_cmd_t *cmd, ngx_array_t *computed_args) {
    ngx_uint_t                      i;
    ngx_array_t                     *args = cmd->args;
    ngx_str_t                       *computed_arg;
    ngx_http_echo_arg_template_t    *arg, *arg_elts;

    arg_elts = args->elts;
    for (i = 0; i < args->nelts; i++) {
        computed_arg = ngx_array_push(computed_args);
        if (computed_arg == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        arg = &arg_elts[i];
        if (arg->lengths == NULL) { /* does not contain vars */
            DD("Using raw value \"%s\"", arg->raw_value.data);
            *computed_arg = arg->raw_value;
        } else {
            if (ngx_http_script_run(r, computed_arg, arg->lengths->elts,
                        0, arg->values->elts) == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
        }
    }
    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_exec_echo_client_request_headers(
        ngx_http_request_t* r, ngx_http_echo_ctx_t *ctx) {
    size_t                      size;
    u_char                      *c;
    ngx_buf_t                   *header_in;
    ngx_chain_t                 *cl  = NULL;
    ngx_buf_t                   *buf;

    DD("echo_client_request_headers triggered!");
    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cl->next = NULL;

    if (r != r->main) {
        header_in = r->main->header_in;
    } else {
        header_in = r->header_in;
    }
    if (header_in == NULL) {
        DD("header_in is NULL");
        return NGX_HTTP_BAD_REQUEST;
    }
    size = header_in->pos - header_in->start;
    //DD("!!! size: %lu", (unsigned long)size);

    buf = ngx_create_temp_buf(r->pool, size);
    buf->last = ngx_cpymem(buf->start, header_in->start, size);
    buf->memory = 1;

    /* fix \0 introduced by the nginx header parser */
    for (c = (u_char*)buf->start; c != buf->last; c++) {
        if (*c == '\0') {
            if (c + 1 != buf->last && *(c + 1) == LF) {
                *c = CR;
            } else {
                *c = ':';
            }
        }
    }

    cl->buf = buf;
    return ngx_http_echo_send_chain_link(r, ctx, cl);
}

static ngx_int_t
ngx_http_echo_exec_echo_sleep(
        ngx_http_request_t *r, ngx_http_echo_ctx_t *ctx,
        ngx_array_t *computed_args) {
    ngx_str_t                   *computed_arg;
    ngx_str_t                   *computed_arg_elts;
    float                       delay; /* in sec */
    ngx_event_t                 *rev;

    computed_arg_elts = computed_args->elts;
    computed_arg = &computed_arg_elts[0];
    delay = atof( (char*) computed_arg->data );
    if (delay < 0.001) { /* should be bigger than 1 msec */
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                   "invalid sleep duration \"%V\"", &computed_arg_elts[0]);
        return NGX_HTTP_BAD_REQUEST;
    }

    DD("DELAY = %.02lf sec", delay);
    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* r->read_event_handler = ngx_http_test_reading; */
    r->read_event_handler = ngx_http_echo_post_sleep;

#if defined(nginx_version) && nginx_version >= 8011
    r->main->count++;
    DD("request main count : %d", r->main->count);
#endif

    rev = r->connection->read;
    ngx_add_timer(rev, (ngx_msec_t) (1000 * delay));

    return NGX_DONE;
}

static void
ngx_http_echo_post_sleep(ngx_http_request_t *r) {
    ngx_event_t                 *rev;
    ngx_http_echo_ctx_t         *ctx;

    rev = r->connection->read;

    if (!rev->timedout) {
        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
        return;
    }

    rev->timedout = 0;

    if (rev->timer_set) {
        ngx_del_timer(rev);
    }

    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_echo_module);
    if (ctx == NULL) {
        return ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }

    ctx->next_handler_cmd++;

    r->read_event_handler = ngx_http_block_reading;
    /* r->write_event_handler = ngx_http_request_empty_handler; */

    ngx_http_finalize_request(r, ngx_http_echo_handler(r));
    return;
}

static ngx_int_t
ngx_http_echo_exec_echo_flush(ngx_http_request_t *r, ngx_http_echo_ctx_t *ctx) {
    return ngx_http_send_special(r, NGX_HTTP_FLUSH);
}

static ngx_int_t
ngx_http_echo_exec_echo_blocking_sleep(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args) {
    ngx_str_t                   *computed_arg;
    ngx_str_t                   *computed_arg_elts;
    float                       delay; /* in sec */

    computed_arg_elts = computed_args->elts;
    computed_arg = &computed_arg_elts[0];
    delay = atof( (char*) computed_arg->data );
    if (delay < 0.001) { /* should be bigger than 1 msec */
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                   "invalid sleep duration \"%V\"", &computed_arg_elts[0]);
        return NGX_HTTP_BAD_REQUEST;
    }

    DD("blocking DELAY = %.02lf sec", delay);

    ngx_msleep((ngx_msec_t) (1000 * delay));
    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_timer_elapsed_variable(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data) {
    ngx_http_echo_ctx_t     *ctx;
    ngx_msec_int_t          ms;
    u_char                  *p;
    ngx_time_t              *tp;
    size_t                  size;

    ctx = ngx_http_get_module_ctx(r, ngx_http_echo_module);
    if (ctx->timer_begin.sec == 0) {
        ctx->timer_begin.sec  = r->start_sec;
        ctx->timer_begin.msec = (ngx_msec_t) r->start_msec;
    }

    ngx_time_update(0, 0); /* force the ngx timer to update */
    tp = ngx_timeofday();

    DD("old sec msec: %ld %d\n", ctx->timer_begin.sec, ctx->timer_begin.msec);
    DD("new sec msec: %ld %d\n", tp->sec, tp->msec);

    ms = (ngx_msec_int_t)
             ((tp->sec - ctx->timer_begin.sec) * 1000 +
              (tp->msec - ctx->timer_begin.msec));
    ms = (ms >= 0) ? ms : 0;

    size = sizeof("-9223372036854775808.000") - 1;
    p = ngx_palloc(r->pool, size);
    v->len = ngx_snprintf(p, size, "%T.%03M",
             ms / 1000, ms % 1000) - p;
    v->data = p;

    v->valid = 1;
    v->no_cacheable = 1;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_add_variables(ngx_conf_t *cf) {
    ngx_http_variable_t *var, *v;
    for (v = ngx_http_echo_variables; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }
        var->get_handler = v->get_handler;
        var->data = v->data;
    }
    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_exec_echo_reset_timer(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx) {
    DD("Exec timer...");
    ngx_time_update(0, 0); /* force the ngx timer to update */

    ctx->timer_begin = *ngx_timeofday();
    return NGX_OK;
}

static ngx_int_t
ngx_http_echo_exec_echo_location_async(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args) {
    ngx_int_t                   rc;
    ngx_http_request_t          *sr; /* subrequest object */
    ngx_str_t                   *computed_arg_elts;
    ngx_str_t                   location;
    ngx_str_t                   *url_args;

    computed_arg_elts = computed_args->elts;

    location = computed_arg_elts[0];

    if (computed_args->nelts > 1) {
        url_args = &computed_arg_elts[1];
    } else {
        url_args = NULL;
    }

    DD("location: %s", location.data);
    DD("location args: %s", (char*) (url_args ? url_args->data : (u_char*)"NULL"));

    rc = ngx_http_echo_send_header_if_needed(r, ctx);
    if (r->header_only || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    rc = ngx_http_subrequest(r, &location, url_args, &sr, NULL, 0);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }
    return NGX_OK;
}

