#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

ssize_t
ngx_demikernel_recv(ngx_connection_t *c, u_char *buf, size_t size)
{
    ssize_t       n;
    ngx_event_t  *rev;

    rev = c->read;

    if (rev->available == 0) {
        rev->ready = 0;
        if (rev->pending_eof) {
            rev->eof = 1;
            return 0;
        } else {
            return NGX_AGAIN;
        }
    }

    if (size >= (size_t) rev->available) {
        n = rev->available;
        rev->available = 0;
        rev->ready = 0;

        memcpy(buf, rev->dmkr.qr_value.sga.sga_segs[0].sgaseg_buf, n);

        demi_sgafree(&rev->dmkr.qr_value.sga);

        return n;
    } else {
        n = size;
        rev->available -= n;

        memcpy(buf, rev->dmkr.qr_value.sga.sga_segs[0].sgaseg_buf, n);

        return n;
    }
}

ssize_t
ngx_demikernel_recv_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{
    // TODO
    return 0;
}

ssize_t
ngx_demikernel_send(ngx_connection_t *c, u_char *buf, size_t size)
{
    ngx_int_t        rc;
    ngx_event_t     *wev;
    demi_sgarray_t   sga;

    wev = c->write;

    /*
    if ((ngx_event_flags & NGX_USE_KQUEUE_EVENT) && wev->pending_eof) {
        (void) ngx_connection_error(c, wev->kq_errno,
                               "kevent() reported about an closed connection");
        wev->error = 1;
        return NGX_ERROR;
    }
    */

    if (!wev->ready) {
        return NGX_AGAIN;
    }

    sga = demi_sgaalloc(size);
    if (sga.sga_numsegs == 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "demi_sgaalloc() returned a null array");
        return NGX_ERROR;
    }

    memcpy(sga.sga_segs[0].sgaseg_buf, buf, size);
    wev->dmkr.qr_value.sga = sga;
    wev->available = size;
    wev->ready = 0;

    rc = ngx_handle_write_event(wev, 0);

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, rc, "todo");
        return NGX_ERROR;
    }

    return size;
}

ngx_chain_t *
ngx_output_chain_to_alloc_size(size_t *alloc_size, ngx_chain_t *in, size_t limit,
    ngx_log_t *log)
{
    size_t         total, size;

    total = 0;

    for ( /* void */ ; in && total < limit; in = in->next) {

        if (ngx_buf_special(in->buf)) {
            continue;
        }

        if (in->buf->in_file) {
            break;
        }

        if (!ngx_buf_in_memory(in->buf)) {
            ngx_log_error(NGX_LOG_ALERT, log, 0,
                          "bad buf in output chain "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          in->buf->temporary,
                          in->buf->recycled,
                          in->buf->in_file,
                          in->buf->start,
                          in->buf->pos,
                          in->buf->last,
                          in->buf->file,
                          in->buf->file_pos,
                          in->buf->file_last);

            ngx_debug_point();

            return NGX_CHAIN_ERROR;
        }

        size = in->buf->last - in->buf->pos;

        if (size > limit - total) {
            size = limit - total;
        }

        total += size;
    }

    *alloc_size = total;

    return in;
}

void
ngx_output_chain_to_sga(demi_sgarray_t *sga, size_t size, ngx_chain_t *in)
{
    size_t copied, chain_size;

    for (copied = 0; copied < size && in; in = in->next) {
        chain_size = in->buf->last - in->buf->pos;
        if (copied + chain_size > size) {
            chain_size = size - copied;
        }

        memcpy((u_char *)sga->sga_segs[0].sgaseg_buf + copied, in->buf->pos, chain_size);
        copied += chain_size;
    }
}

ngx_chain_t *
ngx_demikernel_send_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{
    size_t         send;
    ngx_chain_t   *cl;
    ngx_event_t   *wev;
    ngx_int_t      rc;

    ngx_log_debug0(NGX_LOG_DEBUG, c->log, 0, "dk_send_chain");

    wev = c->write;

    if (!wev->ready) {
        return in;
    }

    /* the maximum limit size is the maximum size_t value - the page size */

    if (limit == 0 || limit > (off_t) (NGX_MAX_SIZE_T_VALUE - ngx_pagesize)) {
        limit = NGX_MAX_SIZE_T_VALUE - ngx_pagesize;
    }

    send = 0;

    cl = ngx_output_chain_to_alloc_size(&send, in, limit - send, c->log);

    if (cl == NGX_CHAIN_ERROR) {
        return NGX_CHAIN_ERROR;
    }

    if (cl && cl->buf->in_file) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "file buf in writev "
                      "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                      cl->buf->temporary,
                      cl->buf->recycled,
                      cl->buf->in_file,
                      cl->buf->start,
                      cl->buf->pos,
                      cl->buf->last,
                      cl->buf->file,
                      cl->buf->file_pos,
                      cl->buf->file_last);

        ngx_debug_point();

        return NGX_CHAIN_ERROR;
    }

    wev->dmkr.qr_value.sga = demi_sgaalloc(send);

    if (wev->dmkr.qr_value.sga.sga_numsegs == 0) {
        return NGX_CHAIN_ERROR;
    }

    ngx_output_chain_to_sga(&wev->dmkr.qr_value.sga, send, in);

    wev->available = send;
    wev->ready = 0;

    rc = ngx_handle_write_event(wev, 0);

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, rc, "todo");
        return NGX_CHAIN_ERROR;
    }

    in = ngx_chain_update_sent(in, send);

    return in;
}

int
ngx_demikernel_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    return -1;
}

int
ngx_demikernel_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return 0;
}

int
ngx_demikernel_socket(int domain, int type, int protocol)
{
    int sockqd_out;
    int rc;

    rc = demi_socket(&sockqd_out, domain, type, protocol);

    if (rc != 0) {
        ngx_set_socket_errno(rc);
        return NGX_ERROR;
    }

    return sockqd_out;
}
