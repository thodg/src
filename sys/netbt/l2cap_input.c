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
#include <sys/task.h>
#include <sys/mbuf.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ifq.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>
#include <netbt/l2cap.h>

int
hci_acl_pr_input(struct mbuf **mp, int *offp, int proto,
		 int af, struct netstack *ns)
{
	struct mbuf     *m = *mp;
	struct hci_unit *unit;

	if (af != AF_BLUETOOTH)
		return (proto);

	unit = hci_tag_get_unit(m);
	if (unit == NULL) {
		m_freem(m);
		goto done;
	}
	if (*offp > 0)
		m_adj(m, *offp);

	hci_acl_recv(m, unit);

 done:
	*mp = NULL;			 /* we consumed the mbuf */
	return (IPPROTO_DONE);
}
