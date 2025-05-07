/*	$OpenBSD: l2cap_socket.c,v 1.5 2012/12/05 23:20:23 deraadt Exp $	*/
/*	$NetBSD: l2cap_socket.c,v 1.9 2008/08/06 15:01:24 plunky Exp $	*/

/*-
 * Copyright (c) 2005 Iain Hibbert.
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* load symbolic names */
#ifdef BLUETOOTH_DEBUG
#define PRUREQUESTS
#define PRCOREQUESTS
#endif

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>		/* XXX for EPASSTHROUGH */
#include <netbt/l2cap.h>

/*
 * L2CAP Sockets
 *
 *	SOCK_SEQPACKET - normal L2CAP connection
 *
 *	SOCK_DGRAM - connectionless L2CAP - XXX not yet
 */

static void l2cap_connecting(void *);
static void l2cap_connected(void *);
static void l2cap_disconnected(void *, int);
static void *l2cap_newconn(void *, struct sockaddr_bt *, struct sockaddr_bt *);
static void l2cap_complete(void *, int);
static void l2cap_linkmode(void *, int);
static void l2cap_input(void *, struct mbuf *);

static const struct btproto l2cap_proto = {
	l2cap_connecting,
	l2cap_connected,
	l2cap_disconnected,
	l2cap_newconn,
	l2cap_complete,
	l2cap_linkmode,
	l2cap_input,
};

/* sysctl variables */
int l2cap_sendspace = 4096;
int l2cap_recvspace = 4096;

int
l2cap_socket_attach(struct socket *so, int proto, int waitflag)
{
	if (so->so_pcb != NULL)
		return EINVAL;

	/* Reserve buffer space first */
	int err = soreserve(so, l2cap_sendspace, l2cap_recvspace);
	if (err)
		return err;

	/* l2cap_attach allocates & initialises the channel */
	return l2cap_attach((struct l2cap_channel **)&so->so_pcb,
			    &l2cap_proto, so);
}

int
l2cap_socket_detach(struct socket *so)
{
	struct l2cap_channel *pcb = so->so_pcb;
	if (pcb == NULL)
		return ENOTCONN;
	return l2cap_detach((struct l2cap_channel **)&so->so_pcb);
}

int
l2cap_socket_abort(struct socket *so)
{
	struct l2cap_channel *pcb = so->so_pcb;
	if (pcb == NULL)
		return ENOTCONN;
	l2cap_disconnect(pcb, 0);
	soisdisconnected(so);
	return l2cap_detach((struct l2cap_channel **)&so->so_pcb);
}

int
l2cap_socket_disconnect(struct socket *so)
{
	struct l2cap_channel *pcb = so->so_pcb;
	if (pcb == NULL)
		return ENOTCONN;

	soisdisconnecting(so);
	return l2cap_disconnect(pcb, so->so_linger);
}

int
l2cap_socket_shutdown(struct socket *so)
{
	socantsendmore(so);
	return 0;
}

int
l2cap_socket_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;
	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	return l2cap_bind(so->so_pcb, sa);
}

int
l2cap_socket_connect(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;
	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	soisconnecting(so);
	return l2cap_connect(so->so_pcb, sa);
}

int
l2cap_socket_listen(struct socket *so)
{
	return l2cap_listen(so->so_pcb);
}

int
l2cap_socket_accept(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	nam->m_len = sizeof(struct sockaddr_bt);
	return l2cap_peeraddr(so->so_pcb, sa);
}

int
l2cap_socket_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	nam->m_len = sizeof(struct sockaddr_bt);
	return l2cap_peeraddr(so->so_pcb, sa);
}

int
l2cap_socket_sockaddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	nam->m_len = sizeof(struct sockaddr_bt);
	return l2cap_sockaddr(so->so_pcb, sa);
}

int
l2cap_socket_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
		  struct mbuf *control)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct mbuf *m0;

	if (m == NULL || m->m_pkthdr.len == 0)
		return 0;

	if (m->m_pkthdr.len > pcb->lc_omtu)
		return EMSGSIZE;

	m0 = m_copym(m, 0, M_COPYALL, M_DONTWAIT);
	if (m0 == NULL)
		return ENOMEM;

	if (control)
		m_freem(control);

	sbappendrecord(&so->so_snd, m);
	return l2cap_send(pcb, m0);
}

int
l2cap_socket_control(struct socket *so, u_long cmd, caddr_t data,
		     struct ifnet *ifp)
{
	return EPASSTHROUGH;
}

/*
 * l2cap_ctloutput(request, socket, level, optname, opt)
 *
 *	Apply configuration commands to channel. This corresponds to
 *	"Reconfigure Channel Request" in the L2CAP specification.
 */
int
l2cap_ctloutput(int req, struct socket *so, int level,
		int optname, struct mbuf *opt)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct mbuf *m;
	int err = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "%s\n", prcorequests[req]);
#endif

	if (pcb == NULL)
		return EINVAL;

	if (level != BTPROTO_L2CAP) {
		err = EINVAL;
		if (req == PRCO_SETOPT && opt)
			m_free(opt);
	} else switch(req) {
	case PRCO_GETOPT:
		m = m_get(M_WAIT, MT_SOOPTS);
		m->m_len = l2cap_getopt(pcb, optname, mtod(m, void *));
		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			err = ENOPROTOOPT;
		}
                else if (opt->m_len >= m->m_len) {
                        memcpy(mtod(opt, void *), mtod(m, void *), m->m_len);
                        opt->m_len = m->m_len;
                } else {
                        err = EMSGSIZE;
                }
                m_freem(m);
                break;

	case PRCO_SETOPT:
		err = l2cap_setopt(pcb, optname, opt);
		m_freem(opt);
		break;

	default:
		err = ENOPROTOOPT;
		break;
	}

	return err;
}

/**********************************************************************
 *
 *	L2CAP Protocol socket callbacks
 *
 */

static void
l2cap_connecting(void *arg)
{
	struct socket *so = arg;

	DPRINTF("Connecting\n");
	soisconnecting(so);
}

static void
l2cap_connected(void *arg)
{
	struct socket *so = arg;

	DPRINTF("Connected\n");
	soisconnected(so);
}

static void
l2cap_disconnected(void *arg, int err)
{
	struct socket *so = arg;

	DPRINTF("Disconnected (%d)\n", err);

	so->so_error = err;
	soisdisconnected(so);
}

static void *
l2cap_newconn(void *arg, struct sockaddr_bt *laddr,
              struct sockaddr_bt *raddr)
{
	struct socket *so = arg;

	DPRINTF("New Connection\n");
	so = sonewconn(so, 0, M_WAIT);
	if (so == NULL)
		return NULL;

	soisconnecting(so);

	return so->so_pcb;
}

static void
l2cap_complete(void *arg, int count)
{
	struct socket *so = arg;

	while (count-- > 0)
		sbdroprecord(&so->so_snd);

	sowwakeup(so);
}

static void
l2cap_linkmode(void *arg, int new)
{
	struct socket *so = arg;
	int mode;

	DPRINTF("auth %s, encrypt %s, secure %s\n",
		(new & L2CAP_LM_AUTH ? "on" : "off"),
		(new & L2CAP_LM_ENCRYPT ? "on" : "off"),
		(new & L2CAP_LM_SECURE ? "on" : "off"));

	(void)l2cap_getopt(so->so_pcb, SO_L2CAP_LM, &mode);
	if (((mode & L2CAP_LM_AUTH) && !(new & L2CAP_LM_AUTH))
	    || ((mode & L2CAP_LM_ENCRYPT) && !(new & L2CAP_LM_ENCRYPT))
	    || ((mode & L2CAP_LM_SECURE) && !(new & L2CAP_LM_SECURE)))
		l2cap_disconnect(so->so_pcb, 0);
}

static void
l2cap_input(void *arg, struct mbuf *m)
{
	struct socket *so = arg;

	if (m->m_pkthdr.len > sbspace(&so->so_rcv)) {
		printf("%s: packet (%d bytes) dropped (socket buffer full)\n",
			__func__, m->m_pkthdr.len);
		m_freem(m);
		return;
	}

	DPRINTFN(10, "received %d bytes\n", m->m_pkthdr.len);

	sbappendrecord(&so->so_rcv, m);
	sorwakeup(so);
}
