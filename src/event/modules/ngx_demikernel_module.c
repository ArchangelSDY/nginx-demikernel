
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


static ngx_int_t ngx_demikernel_init(ngx_cycle_t *cycle, ngx_msec_t timer);
static void ngx_demikernel_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_demikernel_add_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_demikernel_del_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_demikernel_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
    ngx_uint_t flags);
static char *ngx_demikernel_init_conf(ngx_cycle_t *cycle, void *conf);


static demi_qtoken_t    *qts;
static ngx_event_t     **evs;
static ngx_uint_t        nqts;


static ngx_str_t           demikernel_name = ngx_string("demikernel");

static ngx_event_module_t  ngx_demikernel_module_ctx = {
    &demikernel_name,
    NULL,                                        /* create configuration */
    ngx_demikernel_init_conf,                    /* init configuration */

    {
        ngx_demikernel_add_event,                /* add an event */
        ngx_demikernel_del_event,                /* delete an event */
        ngx_demikernel_add_event,                /* enable an event */
        ngx_demikernel_del_event,                /* disable an event */
        NULL,                                    /* add an connection */
        NULL,                                    /* delete an connection */
        NULL,                                    /* trigger a notify */
        ngx_demikernel_process_events,           /* process the events */
        ngx_demikernel_init,                     /* init the events */
        ngx_demikernel_done                      /* done the events */
    }

};

ngx_module_t  ngx_demikernel_module = {
    NGX_MODULE_V1,
    &ngx_demikernel_module_ctx,                  /* module context */
    NULL,                                        /* module directives */
    NGX_EVENT_MODULE,                            /* module type */
    NULL,                                        /* init master */
    NULL,                                        /* init module */
    NULL,                                        /* init process */
    NULL,                                        /* init thread */
    NULL,                                        /* exit thread */
    NULL,                                        /* exit process */
    NULL,                                        /* exit master */
    NGX_MODULE_V1_PADDING
};



static ngx_int_t
ngx_demikernel_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    demi_qtoken_t        *qts2;
    ngx_event_t         **evs2;

    if (qts == NULL) {
        nqts = 0;
    }

    if (ngx_process >= NGX_PROCESS_WORKER
        || cycle->old_cycle == NULL
        || cycle->old_cycle->connection_n < cycle->connection_n)
    {
        qts2 = ngx_alloc(sizeof(demi_qtoken_t) * cycle->connection_n,
                         cycle->log);
        evs2 = ngx_alloc(sizeof(ngx_event_t *) * cycle->connection_n,
                         cycle->log);
        if (qts2 == NULL || evs2 == NULL) {
            return NGX_ERROR;
        }

        if (qts) {
            ngx_memcpy(qts2, qts, sizeof(demi_qtoken_t) * nqts);
            ngx_memcpy(evs2, evs, sizeof(ngx_event_t *) * nqts);
            ngx_free(qts);
            ngx_free(evs);
        }

        qts = qts2;
        evs = evs2;
    }

    // TODO
    ngx_io = ngx_os_io;

    ngx_event_actions = ngx_demikernel_module_ctx.actions;

    // ngx_event_flags = NGX_USE_LEVEL_EVENT|NGX_USE_FD_EVENT;
    ngx_event_flags = NGX_USE_DEMIKERNEL_EVENT;

    return NGX_OK;
}


static void
ngx_demikernel_done(ngx_cycle_t *cycle)
{
    ngx_free(qts);
    ngx_free(evs);

    qts = NULL;
    evs = NULL;
}


static ngx_int_t
ngx_demikernel_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    ngx_connection_t  *c;
    ngx_err_t          err;

    c = ev->data;

    ev->active = 1;

    if (ev->index != NGX_INVALID_INDEX) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "demikernel event fd:%d ev:%i is already set", c->fd, event);
        return NGX_OK;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "demikernel add event: fd:%d ev:%i fl:%d", c->fd, event, flags);

    if (event == NGX_READ_EVENT) {
        if (flags & NGX_DEMIKERNEL_ACCEPT) {
            err = demi_accept(&qts[nqts], c->fd);
        } else {
            err = demi_pop(&qts[nqts], c->fd);
        }
    } else if (event == NGX_WRITE_EVENT) {
        err = demi_push(&qts[nqts], c->fd, &ev->dmkr.qr_value.sga);
    } else {
        // todo: log
        return NGX_ERROR;
    }

    if (err) {
        // todo: log
        return NGX_ERROR;
    }

    ev->index = nqts;
    evs[nqts] = ev;
    nqts++;

    return NGX_OK;
}


static ngx_int_t
ngx_demikernel_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    ev->active = 0;

    if (ev->index != NGX_INVALID_INDEX && ev->index < nqts) {
        nqts--;
        qts[ev->index] = qts[nqts];
        evs[ev->index] = evs[nqts];
        evs[ev->index]->index = ev->index;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_demikernel_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    ngx_err_t           err;
    ngx_event_t        *ev;
    ngx_queue_t        *queue;
    demi_qresult_t      qr;
    int                 offset;

    /* NGX_TIMER_INFINITE == INFTIM */

#if (NGX_DEBUG0)
    if (cycle->log->log_level & NGX_LOG_DEBUG_ALL) {
        for (i = 0; i < nqts; i++) {
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "demikernel: %ui: qtoken:%d",
                           i, qts[i]);
        }
    }
#endif

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "demikernel timer: %M", timer);

    err = demi_wait_any(&qr, &offset, qts, nqts);

    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
        ngx_time_update();
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "demikernel ready at offset %d of %ui", offset, nqts);

    if (err) {
        // if (err == NGX_EINTR) {

        //     if (ngx_event_timer_alarm) {
        //         ngx_event_timer_alarm = 0;
        //         return NGX_OK;
        //     }

        //     level = NGX_LOG_INFO;

        // } else {
        //     level = NGX_LOG_ALERT;
        // }

        ngx_log_error(NGX_LOG_ALERT, cycle->log, err, "demikernel() failed");
        return NGX_ERROR;
    }

    ev = evs[offset];
    nqts--;
    qts[offset] = qts[nqts];
    evs[offset] = evs[nqts];
    evs[offset]->index = offset;

    switch (qr.qr_opcode) {

    case DEMI_OPC_PUSH:
        // TODO
        break;

    case DEMI_OPC_POP:
        ev->dmkr.qr_value.sga = qr.qr_value.sga;
        ev->available = qr.qr_value.sga.sga_segs[0].sgaseg_len;
        ev->ready = 1;
        // TODO
        break;

    case DEMI_OPC_ACCEPT:
        ev->dmkr.qr_value.ares = qr.qr_value.ares;
        // TODO
        break;

    case DEMI_OPC_CONNECT:
        // TODO
        break;

    default:
        /* DEMI_OPC_FAILED, DEMI_OPC_INVALID, others */
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "unexpected demi_opcode %d",
                      qr.qr_opcode);
        return NGX_OK;
    }

    if (flags & NGX_POST_EVENTS) {
        queue = ev->accept ? &ngx_posted_accept_events
                           : &ngx_posted_events;

        ngx_post_event(ev, queue);

        return NGX_OK;
    }

    ev->handler(ev);

    return NGX_OK;
}


static char *
ngx_demikernel_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_event_conf_t  *ecf;

    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);

    if (ecf->use != ngx_demikernel_module.ctx_index) {
        return NGX_CONF_OK;
    }

    return NGX_CONF_OK;
}
