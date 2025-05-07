/*	$OpenBSD: bt_proto.c,v 1.6 2008/11/24 20:19:51 uwe Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/malloc.h>

#include <netbt/bluetooth.h>
#include <netbt/bt_var.h>
#include <netbt/hci.h>
#include <netbt/l2cap.h>
#include <netbt/rfcomm.h>
#include <netbt/sco.h>

struct domain btdomain;

void bt_init(void);

static const struct pr_usrreqs hci_usrreqs = {
	.pru_attach	= hci_socket_attach,
	.pru_detach	= hci_socket_detach,
	.pru_bind	= NULL,
	.pru_connect	= NULL,
	.pru_disconnect = hci_socket_disconnect,
	.pru_send	= hci_socket_send,
	.pru_peeraddr	= hci_socket_peeraddr,
	.pru_sockaddr	= hci_socket_sockaddr,
	.pru_control	= hci_socket_control,
};

static const struct pr_usrreqs l2cap_usrreqs = {
	.pru_attach	= l2cap_socket_attach,
	.pru_detach	= l2cap_socket_detach,
	.pru_bind	= l2cap_socket_bind,
	.pru_connect	= l2cap_socket_connect,
	.pru_disconnect = l2cap_socket_disconnect,
	.pru_shutdown	= l2cap_socket_shutdown,
	.pru_send	= l2cap_socket_send,
	.pru_rcvd	= NULL,
	.pru_peeraddr	= l2cap_socket_peeraddr,
	.pru_sockaddr	= l2cap_socket_sockaddr,
	.pru_control	= l2cap_socket_control
};

static const struct pr_usrreqs sco_usrreqs = {
	.pru_attach	= sco_socket_attach,
	.pru_detach	= sco_socket_detach,
	.pru_bind	= sco_socket_bind,
	.pru_connect	= sco_socket_connect,
	.pru_disconnect = sco_socket_disconnect,
	.pru_shutdown	= sco_socket_shutdown,
	.pru_send	= sco_socket_send,
	.pru_peeraddr	= sco_socket_peeraddr,
	.pru_sockaddr	= sco_socket_sockaddr,
	.pru_control	= sco_socket_control
};

static const struct pr_usrreqs rfcomm_usrreqs = {
	.pru_attach	= rfcomm_socket_attach,
	.pru_detach	= rfcomm_socket_detach,
	.pru_bind	= rfcomm_socket_bind,
	.pru_connect	= rfcomm_socket_connect,
	.pru_disconnect = rfcomm_socket_disconnect,
	.pru_shutdown	= rfcomm_socket_shutdown,
	.pru_send	= rfcomm_socket_send,
	.pru_rcvd	= rfcomm_socket_rcvd,
	.pru_peeraddr	= rfcomm_socket_peeraddr,
	.pru_sockaddr	= rfcomm_socket_sockaddr,
	.pru_control	= rfcomm_socket_control,
	.pru_listen	= rfcomm_socket_listen
};

const struct protosw btsw[] = {
	{
		.pr_type	= SOCK_RAW,
		.pr_domain	= NULL,
		.pr_protocol	= BTPROTO_HCI,
		.pr_flags	= PR_ATOMIC | PR_ADDR | PR_MPINPUT,
		.pr_input	= hci_input,
		.pr_ctloutput	= hci_ctloutput,
		.pr_usrreqs	= &hci_usrreqs,
		.pr_init	= NULL
	},
	{
		.pr_type	= SOCK_SEQPACKET,
		.pr_domain	= NULL,
		.pr_protocol	= BTPROTO_L2CAP,
		.pr_flags	= PR_CONNREQUIRED | PR_WANTRCVD | PR_MPINPUT,
		.pr_input	= hci_acl_pr_input,
		.pr_ctloutput	= l2cap_ctloutput,
		.pr_usrreqs	= &l2cap_usrreqs,
		.pr_init	= l2cap_init
	},
	{
		.pr_type	= SOCK_SEQPACKET,
		.pr_domain	= NULL,
		.pr_protocol	= BTPROTO_SCO,
		.pr_flags	= PR_CONNREQUIRED | PR_WANTRCVD | PR_MPINPUT,
		.pr_input	= sco_input_pr,
		.pr_ctloutput	= sco_ctloutput,
		.pr_usrreqs	= &sco_usrreqs,
                .pr_init	= NULL
	},
	{
		.pr_type	= SOCK_STREAM,
		.pr_domain	= NULL,
		.pr_protocol	= BTPROTO_RFCOMM,
		.pr_flags	= PR_CONNREQUIRED | PR_WANTRCVD | PR_MPINPUT,
		.pr_input	= rfcomm_input_pr,
		.pr_ctloutput	= rfcomm_ctloutput,
		.pr_usrreqs	= &rfcomm_usrreqs,
                .pr_init	= rfcomm_init
	}
};

struct domain btdomain = {
	.dom_family = AF_BLUETOOTH,
        .dom_name = "bluetooth",
	.dom_init = bt_init,
        .dom_externalize = NULL,
        .dom_dispose = NULL,
	.dom_protosw = btsw,
        .dom_protoswNPROTOSW = &btsw[nitems(btsw)],
        .dom_sasize = sizeof(struct sockaddr_bt),
        .dom_rtoffset = 32,
	.dom_maxplen = 64
};

struct mutex bt_lock;

void
bt_init(void)
{
	/*
	 * In accordance with mutex(9), since hci_intr() uses the
	 * lock, we associate the subsystem lock with IPL_SOFTNET.
	 * For unknown reasons, in NetBSD the interrupt level is
	 * IPL_NONE.
	 */
	mtx_init(&bt_lock, IPL_BIO);
}
