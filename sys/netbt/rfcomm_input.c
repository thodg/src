/*	$OpenBSD$	*/
/* Copyright (c) 2025 Thomas de Grivel
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
#include <sys/mbuf.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ifq.h>
#include <net/netisr.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>
#include <netbt/rfcomm.h>

int
rfcomm_input_pr(struct mbuf **mp, int *offp, int proto, int af, struct netstack *ns)
{
	struct mbuf	      *m = *mp;
	struct rfcomm_session *rs;
	struct rfcomm_dlc     *dlc;
	struct hci_link	      *link;
	struct socket *so;
	struct sockaddr_bt     src = {0};
	struct sockaddr_bt     dst = {0};
	uint8_t		       hdr, dlci;

	(void)proto;
	(void)af;
	(void)ns;

	if (m->m_pkthdr.len < *offp + 3) {
		m_freem(m);
		*mp = NULL;
		return EINVAL;
	}
	if ((m = m_pullup(m, *offp + 3)) == NULL) {
		*mp = NULL;
		return ENOBUFS;
	}

	hdr  = mtod(m, uint8_t *)[*offp];
	dlci = (hdr & 0xfc) >> 2;

	link = hci_tag_get_link(m);
	if (link == NULL || link->hl_unit == NULL) {
		m_freem(m);
		*mp = NULL;
		return ENODEV;
	}

	src.bt_len = sizeof(src);
	src.bt_family = AF_BLUETOOTH;
	bdaddr_copy(&src.bt_bdaddr, &link->hl_unit->hci_bdaddr);
	dst.bt_len = sizeof(dst);
	dst.bt_family = AF_BLUETOOTH;
	bdaddr_copy(&dst.bt_bdaddr, &link->hl_bdaddr);
	rs = rfcomm_session_lookup(&src, &dst);
	if (rs == NULL) {
		m_freem(m);
		*mp = NULL;
		return ENOTCONN;
	}

	dlc = rfcomm_dlc_lookup(rs, dlci);
	if (dlc == NULL) {
		m_freem(m);
		*mp = NULL;
		return ENOTCONN;
	}

	so = dlc->rd_upper;
	if (so == NULL) {
		m_freem(m);
		*mp = NULL;
		return ENOTCONN;
	}

	m_adj(m, *offp + 3);
	*offp = 0;

	rfcomm_input(so, m);
	*mp = NULL;

	return IPPROTO_DONE;
}
