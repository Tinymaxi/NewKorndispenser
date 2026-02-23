#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Platform-specific locking for thread safety
#define NO_SYS                      1
#define NO_SYS_NO_TIMERS            1
#define LWIP_TIMERS                 1
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUED         10
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_HTTPD_CGI              0
#define LWIP_HTTPD_SSI              0
#define LWIP_HTTPD_SSI_INCLUDE_TAG  0
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0
#define LWIP_CALLBACK_API           1

#define ETHARP_TRUST_IP_MAC         0
#define LWIP_NUM_NETIF_CLIENT_DATA  1

// Checksum
#define LWIP_CHKSUM_ALGORITHM       3

#endif // _LWIPOPTS_H
