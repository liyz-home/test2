#include"../../jmesh/jmesh_pdu.h"
#include"../../jmesh/jmesh_addr.h"
#include"../../upper/control/jmesh_control.h"
#include"jmesh_routing_func.h"
#include"jmesh_route.h"
#include"jmesh_routing_heartbeat.h"
#include"jmesh_routing_find.h"
#include"jmesh_routing_remove.h"
#include"jmesh_routing_disconnect_request.h"
#include"../../jmesh/jmesh_print.h"
#include"jmesh_routing_neighbor.h"
void jmesh_routing_recv(unsigned short src,unsigned short dst,unsigned char ttl,unsigned char opcode,unsigned char interface,unsigned char* pdu)
{
    switch(opcode){
        case(JMESH_CONTROL_ROUTING_HEARTBEAT):
            jmesh_routing_heartbeat_recv(src,dst,ttl,interface,(jmesh_routing_heartbeat_t*)pdu);
            break;
        case(JMESH_CONTROL_ROUTING_FIND_PROXY):
            jmesh_routing_find_proxy_recv(src,dst,ttl,interface,(jmesh_routing_find_proxy_t*)pdu);
            break;
        case(JMESH_CONTROL_ROUTING_FIND):
            jmesh_routing_find_recv(src,dst,ttl,interface,(jmesh_routing_find_t*)pdu);
            break;
        case(JMESH_CONTROL_ROUTING_REMOVE):
            jmesh_routing_remove_recv(src,dst,ttl,interface,(jmesh_routing_remove_t*)pdu);
            break;
        case(JMESH_CONTROL_ROUTING_SHARE):
            if(jmesh_get_primary_addr()==src)
            {
                return;
            }
            jmesh_routing_neig_lifetime_refresh(src);
            //JMESH_LOGI("exc recv","src:%d,dst:%d,ttl:%d,id:%d\n",src,dst,ttl,interface);
            jmesh_route_exchange_recv(interface,pdu);
            break;
        case(JMESH_CONTROL_ROUTING_DISCONNECT_REQUEST):
            jmesh_routing_disconnect_request_recv(src,dst,ttl,interface);
            break;

    }
}
