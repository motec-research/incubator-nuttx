/****************************************************************************
 * net/icmp/icmp_input.c
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *   Copyright (C) 2007-2009, 2012, 2014-2015, 2017, 2019 Gregory Nutt. All
 *     rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Adapted for NuttX from logic in uIP which also has a BSD-like license:
 *
 *   Original author Adam Dunkels <adam@dunkels.com>
 *   Copyright () 2001-2003, Adam Dunkels.
 *   All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#ifdef CONFIG_NET

#include <stdint.h>
#include <string.h>
#include <debug.h>
#include <sys/time.h>

#include <net/if.h>
#include <arpa/inet.h>

#include <nuttx/net/netconfig.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netstats.h>
#include <nuttx/net/ip.h>

#include "devif/devif.h"
#include "icmp/icmp.h"
#include "utils/utils.h"

#ifdef CONFIG_NET_ICMP

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_NET_ICMP_SOCKET

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct icmp_deliver_s
{
  FAR struct net_driver_s *dev; /* Current network device */
  uint16_t iphdrlen;            /* The size of the IPv4 header */
  bool delivered;               /* Whether the message is delivered */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool icmp_filter(uint32_t filter, uint8_t type)
{
  if (type < 32)
    {
      return ((1u << type) & filter) != 0;
    }

  /* Do not block unknown ICMP types */

  return 0;
}

/****************************************************************************
 * Name: icmp_datahandler
 *
 * Description:
 *   Handle ICMP echo replies that are not accepted by the application.
 *
 * Input Parameters:
 *   dev    - Device instance only the input packet in d_buf, length = d_len;
 *   conn   - A pointer to the ICMP connection structure
 *   iphdrlen - The size of the IPv4 header
 *
 * Returned Value:
 *   The number of bytes actually buffered is returned.  This will be either
 *   zero or equal to buflen; partial packets are not buffered.
 *
 ****************************************************************************/

static uint16_t icmp_datahandler(FAR struct net_driver_s *dev,
                                 FAR struct icmp_conn_s *conn,
                                 uint16_t iphdrlen)
{
  FAR struct ipv4_hdr_s *ipv4;
  struct sockaddr_in inaddr;
  FAR struct iob_s *iob;
  uint16_t buflen;
  int ret;

  iob = iob_tryalloc(false);
  if (iob == NULL)
    {
      return -ENOMEM;
    }

  /* Put the IPv4 address at the beginning of the read-ahead buffer */

  ipv4              = IPv4BUF;
  inaddr.sin_family = AF_INET;
  inaddr.sin_port   = 0;

  net_ipv4addr_copy(inaddr.sin_addr.s_addr,
                    net_ip4addr_conv32(ipv4->srcipaddr));
  memset(inaddr.sin_zero, 0, sizeof(inaddr.sin_zero));

  /* Copy the src address info into the front of I/O buffer chain which
   * overwrites the contents of the packet header field.
   */

  memcpy(iob->io_data, &inaddr, sizeof(struct sockaddr_in));

  iob_reserve(iob, sizeof(struct sockaddr_in));

  /* Copy the ICMP message into the I/O buffer chain (without waiting) */

  ret = iob_clone_partial(dev->d_iob, dev->d_iob->io_pktlen,
                          0, iob, 0, true, false);
  if (ret < 0)
    {
      iob_free_chain(iob);
      return ret;
    }

  buflen = dev->d_len;

  /* Add the new I/O buffer chain to the tail of the read-ahead queue (again
   * without waiting).
   */

  ret = iob_tryadd_queue(iob, &conn->readahead);
  if (ret < 0)
    {
      nerr("ERROR: Failed to queue the I/O buffer chain: %d\n", ret);
      iob_free_chain(iob);
    }
  else
    {
      ninfo("Buffered %d bytes\n", buflen);
    }

  return buflen;
}

/****************************************************************************
 * Name: icmp_delivery_callback
 *
 * Description:
 *   Copy the icmp package to the application according to the filter
 *   conditions, but ICMP_ECHO_REPLY is a special message type, if there
 *   is an application waiting, it will also copy.
 *
 * Input Parameters:
 *   conn - A pointer to the ICMP connection structure.
 *   arg - The context information
 *
 ****************************************************************************/

static int icmp_delivery_callback(FAR struct icmp_conn_s *conn,
                                    FAR void *arg)
{
  FAR struct icmp_deliver_s *info = arg;
  FAR struct net_driver_s   *dev  = info->dev;
  FAR struct icmp_hdr_s     *icmp = IPBUF(info->iphdrlen);

  if (icmp_filter(conn->filter, icmp->type) &&
      (icmp->type != ICMP_ECHO_REPLY || conn->id != icmp->id ||
       conn->dev != dev))
    {
      return 0;
    }

  info->delivered = true;
  if (devif_conn_event(dev, ICMP_NEWDATA, conn->sconn.list) == ICMP_NEWDATA)
    {
      icmp_datahandler(dev, conn, info->iphdrlen);
    }

  return 0;
}

/****************************************************************************
 * Name: icmp_deliver
 *
 * Description:
 *   Copy the icmp package to the application according to the filter
 *   conditions, but ICMP_ECHO_REPLY is a special message type, if there
 *   is an application waiting, it will also copy.
 *
 * Input Parameters:
 *   dev - Reference to a device driver structure.
 *   iphdrlen - The size of the IP header.  This may be larger than
 *              IPv4_HDRLEN if IP option are present.
 *
 ****************************************************************************/

static bool icmp_deliver(FAR struct net_driver_s *dev, uint16_t iphdrlen)
{
  struct icmp_deliver_s info;

  info.dev       = dev;
  info.iphdrlen  = iphdrlen;
  info.delivered = false;

  icmp_foreach(icmp_delivery_callback, &info);

  return info.delivered;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: icmp_input
 *
 * Description:
 *   Handle incoming ICMP input
 *
 * Input Parameters:
 *   dev - The device driver structure containing the received ICMP
 *         packet
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

void icmp_input(FAR struct net_driver_s *dev)
{
  FAR struct ipv4_hdr_s *ipv4 = IPv4BUF;
  FAR struct icmp_hdr_s *icmp;

  /* Get the IP header length (accounting for possible options). */

  uint16_t iphdrlen = (ipv4->vhl & IPv4_HLMASK) << 2;
#ifdef CONFIG_NET_ICMP_SOCKET
  bool delivered = icmp_deliver(dev, iphdrlen);
#endif

#ifdef CONFIG_NET_STATISTICS
  g_netstats.icmp.recv++;
#endif

  /* The ICMP header immediately follows the IP header */

  icmp = IPBUF(iphdrlen);

  /* ICMP echo (i.e., ping) processing. This is simple, we only change the
   * ICMP type from ECHO to ECHO_REPLY and adjust the ICMP checksum before
   * we return the packet.
   */

  if (icmp->type == ICMP_ECHO_REQUEST)
    {
      /* Change the ICMP type */

      icmp->type = ICMP_ECHO_REPLY;

      /* Swap IP addresses. */

      net_ipv4addr_hdrcopy(ipv4->destipaddr, ipv4->srcipaddr);
      net_ipv4addr_hdrcopy(ipv4->srcipaddr, &dev->d_ipaddr);

      /* Recalculate the ICMP checksum */

#if 0
      /* Get the IP header length (accounting for possible options). */

      iphdrlen = (ipv4->vhl & IPv4_HLMASK) << 2;

      /* The slow way... sum over the ICMP message */

      icmp->icmpchksum = 0;
      icmp->icmpchksum = ~icmp_chksum(dev,
                                     (((uint16_t)ipv4->len[0] << 8) |
                                       (uint16_t)ipv4->len[1]) - iphdrlen);
      if (icmp->icmpchksum == 0)
        {
          icmp->icmpchksum = 0xffff;
        }
#else
      /* The quick way -- Since only the type has changed, just adjust the
       * checksum for the change of type
       */
#ifdef CONFIG_NET_ICMP_CHECKSUMS
      if (icmp->icmpchksum >= HTONS(0xffff - (ICMP_ECHO_REQUEST << 8)))
        {
          icmp->icmpchksum += HTONS(ICMP_ECHO_REQUEST << 8) + 1;
        }
      else
        {
          icmp->icmpchksum += HTONS(ICMP_ECHO_REQUEST << 8);
        }
#else
      icmp->icmpchksum = 0;
#endif

#endif

      ninfo("Outgoing ICMP packet length: %d (%d)\n",
            dev->d_len, (ipv4->len[0] << 8) | ipv4->len[1]);

#ifdef CONFIG_NET_STATISTICS
      g_netstats.icmp.sent++;
      g_netstats.ipv4.sent++;
#endif
    }

#if CONFIG_NET_ICMP_PMTU_ENTRIES > 0
  else if (icmp->type == ICMP_DEST_UNREACHABLE)
    {
      if (icmp->icode == ICMP_FRAG_NEEDED)
        {
          FAR struct icmp_pmtu_entry *entry;
          FAR struct ipv4_hdr_s *inner;
          int mtu;

          mtu = ntohs(icmp->data[0]) << 16 | ntohs(icmp->data[1]);
          if (mtu <= 0)
            {
              goto typeerr;
            }

          inner = (FAR struct ipv4_hdr_s *)(icmp + 1);
          entry = icmpv4_find_pmtu_entry(
                        net_ip4addr_conv32(inner->destipaddr));
          if (entry == NULL)
            {
              icmpv4_add_pmtu_entry(
                net_ip4addr_conv32(inner->destipaddr), mtu);
            }
          else
            {
              entry->pmtu = mtu;
            }

          goto icmp_send_nothing;
        }
    }
#endif

  /* Otherwise the ICMP input was not processed */

  else
    {
#ifdef CONFIG_NET_ICMP_SOCKET
      if (delivered)
        {
          goto icmp_send_nothing;
        }
      else if (icmp->type == ICMP_ECHO_REQUEST)
        {
          goto drop;
        }
#endif

      nwarn("WARNING: Unknown ICMP cmd: %d\n", icmp->type);
      goto typeerr;
    }

  return;

typeerr:
#ifdef CONFIG_NET_STATISTICS
  g_netstats.icmp.typeerr++;
#endif

#ifdef CONFIG_NET_ICMP_SOCKET
drop:
#ifdef CONFIG_NET_STATISTICS
  g_netstats.icmp.drop++;
#endif
#endif

#if defined(CONFIG_NET_ICMP_SOCKET) || CONFIG_NET_ICMP_PMTU_ENTRIES > 0
icmp_send_nothing:
#endif

  dev->d_len = 0;
}

#endif /* CONFIG_NET_ICMP */
#endif /* CONFIG_NET */
