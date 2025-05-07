/*	$OpenBSD$	*/
/* Copyright (c) 2025 
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/timeout.h>
#include <sys/mbuf.h>

#include <net/netisr.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/ifq.h>

#include <netbt/bluetooth.h>
#include <netbt/l2cap.h>

int
l2cap_ctloutput(int req, struct socket *so,
                int level, int optname, struct mbuf *opt)
{
        struct l2cap_channel *pcb = so->so_pcb;
        int error = 0;

        if (pcb == NULL) {/* not attached */
                if (opt != NULL)
                        m_freem(opt);
                return (EINVAL);
        }

        if (level != BTPROTO_L2CAP) {/* nothing else understood */
                if (opt != NULL && req == PRCO_SETOPT)
                        m_freem(opt);
                return (ENOPROTOOPT);
        }

        switch (req) {
        case PRCO_GETOPT:
                /*
                 * l2cap_getopt() allocates a result mbuf (or re‑uses |opt|
                 * if one was supplied) and stores the value in it.
                 * Caller expects that mbuf to be returned in |opt|.
                 */
                error = l2cap_getopt(pcb, optname, &opt);
                break;

        case PRCO_SETOPT:
                /*
                 * Caller handed us an mbuf that contains the new value.
                 * l2cap_setopt() will free it on success or error,
                 * so do not touch |opt| afterwards.
                 */
                error = l2cap_setopt(pcb, optname, opt);
                break;

        default:
                if (opt != NULL)
                        m_freem(opt);
                error = ENOPROTOOPT;
                break;
        }

        return (error);
}
