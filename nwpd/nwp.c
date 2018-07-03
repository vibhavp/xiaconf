#include "nwp.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

bool read_announce(char *buf, struct nwp_announce *packet, int msglen)
{
        size_t elem_len = sizeof(struct nwp_common_hdr)
                + sizeof(uint8_t)
                + sizeof(uint8_t);
        size_t data_len = packet->haddr_len + XIA_XID_MAX * packet->hid_count;
        if (elem_len + data_len > msglen)
                return false;

        memcpy(packet, buf, elem_len);

        buf = buf + elem_len;
        packet->haddr = malloc(packet->haddr_len);
        memcpy(packet->haddr, buf, packet->haddr_len);

        buf = buf + packet->haddr_len;
        packet->addr_begin = malloc(XIA_XID_MAX * packet->hid_count);
        memcpy(packet->addr_begin, buf, XIA_XID_MAX * packet->hid_count);
        return true;
}

void announce_set_ha(struct nwp_announce *packet, uint8_t *ha)
{
        assert(packet->haddr_len != 0);
        memmove(packet->haddr, ha, packet->haddr_len);
}

void announce_add_xid(struct nwp_announce *packet, uint8_t *xid)
{
        memmove(packet->addr_begin + ((packet->hid_count++) * XIA_XID_MAX),
                xid, XIA_XID_MAX);
}

int announce_size(struct nwp_announce *packet)
{
        return sizeof(struct nwp_common_hdr) + 2*sizeof(uint8_t)
                + packet->haddr_len + packet->hid_count * XIA_XID_MAX;
}

bool read_neighbor_list(char *buf, struct nwp_neigh_list *packet, int msglen)
{
        size_t elem_len = sizeof(struct nwp_common_hdr)
                + sizeof(uint8_t)
                + sizeof(uint8_t);
        int i, i2;
        char *addr = buf + elem_len;

        if (elem_len > msglen)
                return false;
        packet->addrs = malloc(sizeof(struct nwp_neighbor *) * packet->hid_count);

        memcpy(packet, buf, elem_len);
        for (i = 0; i < packet->hid_count; i++) {
                struct nwp_neighbor *neigh = malloc(sizeof(struct nwp_neighbor));
                size_t neigh_init_size = XIA_XID_MAX + sizeof(uint8_t);
                if ((addr + neigh_init_size) - buf > msglen) {
                        /* TODO: free memory */
                        return false;
                }
                memcpy(neigh, addr, neigh_init_size);
                if ((addr + neigh->num * packet->haddr_len) - buf > msglen) {
                        /* TODO: free memory */
                        return false;
                }

                addr += neigh_init_size;
                neigh->haddrs = malloc(sizeof(uint8_t *) * neigh->num);
                for (i2 = 0; i2 < neigh->num; i2++) {
                        neigh->haddrs[i2] = malloc(packet->haddr_len);
                        memcpy(neigh->haddrs[i2], addr, packet->haddr_len);
                        addr += packet->haddr_len;
                }
                packet->addrs[i] = neigh;
        }

        return true;
}
