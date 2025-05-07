/*	$OpenBSD: rfcomm_socket.c,v 1.5 2009/11/21 13:05:32 guenther Exp $	*/
/*	$NetBSD: rfcomm_socket.c,v 1.10 2008/08/06 15:01:24 plunky Exp $	*/

/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Written by Iain Hibbert for Itronix Inc.
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
#include <netbt/rfcomm.h>

/****************************************************************************
 *
 *	RFCOMM SOCK_STREAM Sockets - serial line emulation
 *
 */

static void rfcomm_connecting(void *);
static void rfcomm_connected(void *);
static void rfcomm_disconnected(void *, int);
static void *rfcomm_newconn(void *, struct sockaddr_bt *, struct sockaddr_bt *);
static void rfcomm_complete(void *, int);
static void rfcomm_linkmode(void *, int);

static const struct btproto rfcomm_proto = {
	rfcomm_connecting,
	rfcomm_connected,
	rfcomm_disconnected,
	rfcomm_newconn,
	rfcomm_complete,
	rfcomm_linkmode,
	rfcomm_input,
};

/* sysctl variables */
int rfcomm_sendspace = 4096;
int rfcomm_recvspace = 4096;

int
rfcomm_socket_attach(struct socket *so, int proto, int waitflag)
{
	int err;

	if (so->so_pcb)
		return EINVAL;

	err = soreserve(so, rfcomm_sendspace, rfcomm_recvspace);
	if (err)
		return err;

	err = rfcomm_attach((struct rfcomm_dlc **)&so->so_pcb,
			    &rfcomm_proto, so);
	if (err)
		return err;

	/* prime receive window */
	err = rfcomm_rcvd(so->so_pcb, sbspace(&so->so_rcv));
	if (err) {
		rfcomm_detach((struct rfcomm_dlc **)&so->so_pcb);
		return err;
	}
	return 0;
}

int
rfcomm_socket_detach(struct socket *so)
{
	return rfcomm_detach((struct rfcomm_dlc **)&so->so_pcb);
}

int
rfcomm_socket_abort(struct socket *so)
{
	struct rfcomm_dlc *pcb = so->so_pcb;
	if (!pcb)
		return ENOTCONN;

	rfcomm_disconnect(pcb, 0);
	soisdisconnected(so);
	return rfcomm_socket_detach(so);
}

int
rfcomm_socket_disconnect(struct socket *so)
{
	struct rfcomm_dlc *pcb = so->so_pcb;
	if (!pcb)
		return ENOTCONN;

	soisdisconnecting(so);
	return rfcomm_disconnect(pcb, so->so_linger);
}

int
rfcomm_socket_shutdown(struct socket *so)
{
	socantsendmore(so);
	return 0;
}

int
rfcomm_socket_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;
	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;
	return rfcomm_bind(so->so_pcb, sa);
}

int
rfcomm_socket_connect(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;
	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	soisconnecting(so);
	return rfcomm_connect(so->so_pcb, sa);
}

int
rfcomm_socket_listen(struct socket *so)
{
	return rfcomm_listen(so->so_pcb);
}

int
rfcomm_socket_accept(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	nam->m_len = sizeof(struct sockaddr_bt);
	return rfcomm_peeraddr(so->so_pcb, sa);
}

int
rfcomm_socket_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	nam->m_len = sizeof(struct sockaddr_bt);
	return rfcomm_peeraddr(so->so_pcb, sa);
}

int
rfcomm_socket_sockaddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_bt *sa = mtod(nam, struct sockaddr_bt *);
	nam->m_len = sizeof(struct sockaddr_bt);
	return rfcomm_sockaddr(so->so_pcb, sa);
}

int
rfcomm_socket_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
		   struct mbuf *control)
{
	struct rfcomm_dlc *pcb = so->so_pcb;
	struct mbuf *m0;

	if (m == NULL)
		return EINVAL;

	if (control)
		m_freem(control);

	m0 = m_copym(m, 0, M_COPYALL, M_DONTWAIT);
	if (m0 == NULL)
		return ENOMEM;

	sbappendstream(&so->so_snd, m);
	return rfcomm_send(pcb, m0);
}

void
rfcomm_socket_rcvd(struct socket *so)
{
	rfcomm_rcvd(so->so_pcb, sbspace(&so->so_rcv));
}

int
rfcomm_socket_control(struct socket *so, u_long cmd, caddr_t data,
		      struct ifnet *ifp)
{
	return EPASSTHROUGH;
}

/*
 * rfcomm_ctloutput(request, socket, level, optname, opt)
 *
 */
int
rfcomm_ctloutput(int req, struct socket *so, int level,
		int optname, struct mbuf *opt)
{
	struct rfcomm_dlc *pcb = so->so_pcb;
	struct mbuf *m;
	int err = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "%s\n", prcorequests[req]);
#endif

	if (pcb == NULL)
		return EINVAL;

	if (level != BTPROTO_RFCOMM) {
		err = EINVAL;
		if (req == PRCO_SETOPT && opt)
			m_free(opt);
	} else switch(req) {
	case PRCO_GETOPT:
		m = m_get(M_WAIT, MT_SOOPTS);
		m->m_len = rfcomm_getopt(pcb, optname, mtod(m, void *));
		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			err = ENOPROTOOPT;
		}
		opt = m; // XXX *opt = m
		break;

	case PRCO_SETOPT:
		m = opt;
		err = rfcomm_setopt(pcb, optname, m);
		m_freem(m);
		break;

	default:
		err = ENOPROTOOPT;
		break;
	}

	return err;
}

/**********************************************************************
 *
 * RFCOMM callbacks
 */

void
rfcomm_connecting(void *arg)
{
	/* struct socket *so = arg; */

	KASSERT(arg != NULL);
	DPRINTF("Connecting\n");
}

void
rfcomm_connected(void *arg)
{
	struct socket *so = arg;

	KASSERT(so != NULL);
	DPRINTF("Connected\n");
	soisconnected(so);
}

void
rfcomm_disconnected(void *arg, int err)
{
	struct socket *so = arg;

	KASSERT(so != NULL);
	DPRINTF("Disconnected\n");

	so->so_error = err;
	soisdisconnected(so);
}

void *
rfcomm_newconn(void *arg, struct sockaddr_bt *laddr,
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

/*
 * rfcomm_complete(rfcomm_dlc, length)
 *
 * length bytes are sent and may be removed from socket buffer
 */
void
rfcomm_complete(void *arg, int length)
{
	struct socket *so = arg;

	sbdrop(&so->so_snd, length);
	sowwakeup(so);
}

/*
 * rfcomm_linkmode(rfcomm_dlc, new)
 *
 * link mode change notification.
 */
void
rfcomm_linkmode(void *arg, int new)
{
	struct socket *so = arg;
	int mode;

	DPRINTF("auth %s, encrypt %s, secure %s\n",
		(new & RFCOMM_LM_AUTH ? "on" : "off"),
		(new & RFCOMM_LM_ENCRYPT ? "on" : "off"),
		(new & RFCOMM_LM_SECURE ? "on" : "off"));

	(void)rfcomm_getopt(so->so_pcb, SO_RFCOMM_LM, &mode);
	if (((mode & RFCOMM_LM_AUTH) && !(new & RFCOMM_LM_AUTH))
	    || ((mode & RFCOMM_LM_ENCRYPT) && !(new & RFCOMM_LM_ENCRYPT))
	    || ((mode & RFCOMM_LM_SECURE) && !(new & RFCOMM_LM_SECURE)))
		rfcomm_disconnect(so->so_pcb, 0);
}

/*
 * rfcomm_input(rfcomm_dlc, mbuf)
 */
void
rfcomm_input(void *arg, struct mbuf *m)
{
	struct socket *so = arg;

	KASSERT(so != NULL);

	if (m->m_pkthdr.len > sbspace(&so->so_rcv)) {
		printf("%s: %d bytes dropped (socket buffer full)\n",
			__func__, m->m_pkthdr.len);
		m_freem(m);
		return;
	}

	DPRINTFN(10, "received %d bytes\n", m->m_pkthdr.len);

	sbappendstream(&so->so_rcv, m);
	sorwakeup(so);
}
