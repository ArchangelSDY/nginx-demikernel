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

ssize_t
ngx_demikernel_send_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{
    // TODO
    return 0;
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
