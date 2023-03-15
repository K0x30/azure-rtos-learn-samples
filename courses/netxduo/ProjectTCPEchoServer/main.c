/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/

#include   "tx_api.h"
#include   "nx_api.h"

/* Make sure IPv6 is enabled.  */
#if !defined(FEATURE_NX_IPV6) || !defined(NX_ENABLE_IPV6_ADDRESS_CHANGE_NOTIFY)
#error "IPv6 and address change notify must be enabled to run this project"
#endif /* NX_FEATURE_IPv6 */

/* Define sample IP address.  */
#define SAMPLE_IPV4_ADDRESS             IP_ADDRESS(192, 168, 1, 2)
#define SAMPLE_IPV4_MASK                0xFFFFFF00UL
#define SAMPLE_PRIMARY_INTERFACE        0
#define SAMPLE_IPV6_ADDRESS_0           0x20010000
#define SAMPLE_IPV6_ADDRESS_1           0x00000000
#define SAMPLE_IPV6_ADDRESS_2           0x00000000
#define SAMPLE_IPV6_ADDRESS_3           0x00005678
#define SAMPLE_IPV6_ADDRESS_PREFIX      64

/* Define ECHO server address and port.  */
#define ECHO_SERVER_ADDRESS_0           0x20010000
#define ECHO_SERVER_ADDRESS_1           0x00000000
#define ECHO_SERVER_ADDRESS_2           0x00000000
#define ECHO_SERVER_ADDRESS_3           0x00001234
#define ECHO_SERVER_PORT                7
#define ECHO_DATA                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ "
#define ECHO_RECEIVE_TIMEOUT            NX_IP_PERIODIC_RATE

/* Define packet pool.  */
#define PACKET_SIZE                     1536
#define PACKET_COUNT                    30
#define PACKET_POOL_SIZE                ((PACKET_SIZE + sizeof(NX_PACKET)) * PACKET_COUNT)

/* Define IP stack size.   */
#define IP_STACK_SIZE                   2048

/* Define IP thread priority.  */
#define IP_THREAD_PRIORITY              1

/* Define stack size of sample thread.  */
#define SAMPLE_THREAD_STACK_SIZE        2048

/* Define priority of sample thread.  */
#define SAMPLE_THREAD_PRIORITY          4

/* Define ARP pool.  */
#define ARP_POOL_SIZE                   1024

/* Define TCP socket listen queue size, TTL and window size.  */
#define SAMPLE_SOCKET_LISTEN_QUEUE_SIZE 5
#define SAMPLE_SOCKET_TTL               0x80
#define SAMPLE_SOCKET_WINDOW_SIZE       65535

/* Define time wait for IPv6 DAD process.  */
#define SAMPLE_DAD_WAIT                 (3 * NX_IP_PERIODIC_RATE)

/* Define the ThreadX and NetX object control blocks...  */
NX_PACKET_POOL          default_pool;
NX_IP                   default_ip;
NX_TCP_SOCKET           tcp_server;
TX_THREAD               server_thread;

/* Define memory buffers.  */
ULONG                   pool_area[PACKET_POOL_SIZE >> 2];
ULONG                   ip_stack[IP_STACK_SIZE >> 2];
ULONG                   arp_area[ARP_POOL_SIZE >> 2];
ULONG                   server_thread_stack[SAMPLE_THREAD_STACK_SIZE >> 2];

/* Define the counters used in the demo application...  */
ULONG                   error_counter;

/***** Substitute your ethernet driver entry function here *********/
extern  VOID _nx_linux_network_driver(NX_IP_DRIVER*);

/* Define function prototypes.  */
void server_thread_entry(ULONG thread_input);
static VOID print_ipv6_address(ULONG *ipv6_address);
static VOID ipv6_address_DAD_notify(NX_IP *ip_ptr, UINT status, UINT interface_index,
                                    UINT ipv6_addr_index, ULONG *ipv6_address);

/* Define main entry point.  */
int main()
{

    /* Enter the ThreadX kernel.  */
    tx_kernel_enter();
}


/* Define what the initial system looks like.  */
void    tx_application_define(void *first_unused_memory)
{

UINT    status;

    NX_PARAMETER_NOT_USED(first_unused_memory);

    /* Initialize the NetX system.  */
    nx_system_initialize();

    /* Create the sample thread.  */
    tx_thread_create(&server_thread, "Server Thread", server_thread_entry, 0,
                     server_thread_stack, sizeof(server_thread_stack),
                     SAMPLE_THREAD_PRIORITY, SAMPLE_THREAD_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

    /* Create a packet pool.  */
    status = nx_packet_pool_create(&default_pool, "NetX Main Packet Pool",
                                   PACKET_SIZE, pool_area, sizeof(pool_area));

    /* Check for packet pool create errors.  */
    if (status)
        error_counter++;

    /* Create an IP instance.  */
    status = nx_ip_create(&default_ip, "NetX IP Instance 0", SAMPLE_IPV4_ADDRESS, SAMPLE_IPV4_MASK,
                          &default_pool, _nx_linux_network_driver,
                          (void *)ip_stack, sizeof(ip_stack), IP_THREAD_PRIORITY);

    /* Check for IP create errors.  */
    if (status)
        error_counter++;

    /* Enable ARP and supply ARP cache memory for IP Instance 0.  */
    status =  nx_arp_enable(&default_ip, (void *)arp_area, sizeof(arp_area));

    /* Check for ARP enable errors.  */
    if (status)
        error_counter++;

    /* Enable IPv6. */
    status = nxd_ipv6_enable(&default_ip);

    /* Check for IPv6 enable errors.  */
    if(status)
        error_counter++;

    /* Enable ICMP for both ICMPv4 and ICMPv6. */
    status = nxd_icmp_enable(&default_ip);

    /* Check for ICMP enable errors.  */
    if(status)
        error_counter++;

    /* Enable TCP */
    status = nx_tcp_enable(&default_ip);

    /* Check for TCP enable errors.  */
    if(status)
        error_counter++;

    /* Output IP address and network mask.  */
    printf("NetXDuo is running\r\n");
    printf("IPv4 address: %lu.%lu.%lu.%lu\r\n",
           (SAMPLE_IPV4_ADDRESS >> 24),
           (SAMPLE_IPV4_ADDRESS >> 16 & 0xFF),
           (SAMPLE_IPV4_ADDRESS >> 8 & 0xFF),
           (SAMPLE_IPV4_ADDRESS & 0xFF));
    printf("Mask: %lu.%lu.%lu.%lu\r\n",
           (SAMPLE_IPV4_MASK >> 24),
           (SAMPLE_IPV4_MASK >> 16 & 0xFF),
           (SAMPLE_IPV4_MASK >> 8 & 0xFF),
           (SAMPLE_IPV4_MASK & 0xFF));
}


/* Server thread entry.  */
void server_thread_entry(ULONG thread_input)
{
UINT       status;
NX_PACKET *packet_ptr;
NXD_ADDRESS sample_ipv6_address;
NXD_ADDRESS client_ipv6_address;
ULONG       client_port;

    /* Set link local address by stateless address auto configuration.  */
    sample_ipv6_address.nxd_ip_version = NX_IP_VERSION_V6;
    sample_ipv6_address.nxd_ip_address.v6[0] = SAMPLE_IPV6_ADDRESS_0;
    sample_ipv6_address.nxd_ip_address.v6[1] = SAMPLE_IPV6_ADDRESS_1;
    sample_ipv6_address.nxd_ip_address.v6[2] = SAMPLE_IPV6_ADDRESS_2;
    sample_ipv6_address.nxd_ip_address.v6[3] = SAMPLE_IPV6_ADDRESS_3;
    status = nxd_ipv6_address_set(&default_ip, SAMPLE_PRIMARY_INTERFACE,
                                  &sample_ipv6_address, SAMPLE_IPV6_ADDRESS_PREFIX, NX_NULL);

    /* Check status.  */
    if (status)
    {
        error_counter++;
        return;
    }

    /* Set the IPv6 address change callback function.  */
    nxd_ipv6_address_change_notify(&default_ip, ipv6_address_DAD_notify);  

    /* Suspend current thread for the IPv6 stack to finish DAD process. */
    tx_thread_suspend(tx_thread_identify());

    /* Create a TCP socket.  */
    status = nx_tcp_socket_create(&default_ip, &tcp_server, "TCP Echo Server", NX_IP_NORMAL, NX_DONT_FRAGMENT,
                                  SAMPLE_SOCKET_TTL, SAMPLE_SOCKET_WINDOW_SIZE, NX_NULL, NX_NULL);

    /* Check status.  */
    if (status)
    {
        error_counter++;
        return;
    }

    /* Listen the TCP socket to port 7.  */
    status =  nx_tcp_server_socket_listen(&default_ip, ECHO_SERVER_PORT, &tcp_server,
                                          SAMPLE_SOCKET_LISTEN_QUEUE_SIZE, NX_NULL);

    /* Check status.  */
    if (status)
    {
        error_counter++;
        return;
    }

    /* Accept connection from client.  */
    printf("Waiting for connection\r\n");
    status = nx_tcp_server_socket_accept(&tcp_server, NX_WAIT_FOREVER);

    /* Check status.  */
    if (status)
    {
        printf("Not connected\r\n");
        error_counter++;
        return;
    }

    /* Get peer information.  */
    nxd_tcp_socket_peer_info_get(&tcp_server, &client_ipv6_address, &client_port);
    printf("Client connected from: ");
    print_ipv6_address(client_ipv6_address.nxd_ip_address.v6);

    /* Loop to send data to echo server.  */
    for (;;)
    {
        
        /* Receive a packet.  */
        status =  nx_tcp_socket_receive(&tcp_server, &packet_ptr, NX_WAIT_FOREVER);

        /* Check status.  */
        if (status != NX_SUCCESS)
        {
            error_counter++;
            break;
        }

        /* Echo data to client.  */
        status =  nx_tcp_socket_send(&tcp_server, packet_ptr, NX_WAIT_FOREVER);
        
        /* Check status.  */
        if (status != NX_SUCCESS)
        {
            nx_packet_release(packet_ptr);
            error_counter++;
            break;
        }
    }

    /* Cleanup the TCP socket.  */
    nx_tcp_socket_disconnect(&tcp_server, NX_WAIT_FOREVER);
    nx_tcp_client_socket_unbind(&tcp_server);
    nx_tcp_socket_delete(&tcp_server);
}

static VOID print_ipv6_address(ULONG *ipv6_address)
{
    printf("%X:%X:%X:%X:%X:%X:%X:%X\r\n",
           (UINT)(ipv6_address[0] >> 16),
           (UINT)(ipv6_address[0] & 0xFFFF),
           (UINT)(ipv6_address[1] >> 16),
           (UINT)(ipv6_address[1] & 0xFFFF),
           (UINT)(ipv6_address[2] >> 16),
           (UINT)(ipv6_address[2] & 0xFFFF),
           (UINT)(ipv6_address[3] >> 16),
           (UINT)(ipv6_address[3] & 0xFFFF));
}

static VOID ipv6_address_DAD_notify(NX_IP *ip_ptr, UINT status, UINT interface_index,
                                    UINT ipv6_addr_index, ULONG *ipv6_address)
{                 
    NX_PARAMETER_NOT_USED(ip_ptr);
    NX_PARAMETER_NOT_USED(interface_index);
    NX_PARAMETER_NOT_USED(ipv6_addr_index);                              

    /* Check the status.  */
    switch(status)
    {
        case NX_IPV6_ADDRESS_DAD_SUCCESSFUL:
        {
            printf("DAD successful\r\nIPv6 address: ");
            print_ipv6_address(ipv6_address);
            tx_thread_resume(&server_thread);
            break;
        }
        case NX_IPV6_ADDRESS_DAD_FAILURE:
        {
            printf("DAD failure\r\nIPv6 address: ");
            print_ipv6_address(ipv6_address);
            break;
        }
        default:
        {
            break;
        }
    }
}
