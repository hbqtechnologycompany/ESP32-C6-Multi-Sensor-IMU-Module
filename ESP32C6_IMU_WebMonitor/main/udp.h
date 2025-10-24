#ifndef UDP_H
#define UDP_H

#define UDP_PORT 12345
#define UDP_BROADCAST_IP "255.255.255.255"


void udp_broadcast_task(void *pvParameters);

#endif