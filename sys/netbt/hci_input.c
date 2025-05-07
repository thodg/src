/*	$OpenBSD$	*/
/* Copyright (c) 2025 Thomas de Grivel <thoxdg@gmail.com>
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
#include <sys/mbuf.h>
#include <sys/timeout.h>
#include <netbt/bluetooth.h>
#include <netbt/hci.h>

#define PACKET_TAG_BTUNIT	 0x4442	  /* “BD” reversed, unused value */

struct hci_unit *
hci_get_unit(struct mbuf *m)
{
	struct m_tag *t = m_tag_find(m, PACKET_TAG_BTUNIT, NULL);
	return t ? *(struct hci_unit **)(t + 1) : NULL;
}

int
hci_input(struct mbuf **mp, int *offp, int proto, int flags,
	  struct netstack *ns)
{
	struct mbuf *m = *mp;

	(void)offp;
	(void)proto;
	(void)flags;
	(void)ns;

	hci_event(m, hci_get_unit(m));
	return 0;
}

void
hci_set_unit(struct mbuf *m, struct hci_unit *unit)
{
	struct m_tag *t;

	while ((t = m_tag_find(m, PACKET_TAG_BTUNIT, NULL)) != NULL)
		m_tag_delete(m, t);

	if (unit == NULL)
		return;

	t = m_tag_get(PACKET_TAG_BTUNIT,
	sizeof(struct hci_unit *), M_NOWAIT);
	if (t == NULL)
		return;
	*(struct hci_unit **)(t + 1) = unit;
	m_tag_prepend(m, t);
}
