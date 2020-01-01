#include "jmesh_provisioning_client.h"
#include "jmesh_provisioning.h"
#include "../../secure/ECDH/uECC.h"
#include"../../jmesh/jmesh_task.h"
#include "../../onchip_system/os_timer_event.h"
#include "../../jmesh/jmesh_print.h"
#include "../../driver/jmesh_system.h"
#include "../../jmesh/jmesh_addr.h"
#include"../gatt/jmesh_gatt.h"
#include"../gatt/jmesh_proxy.h"
#include"stdbool.h"
#include"../../network/jmesh_network.h"
#include "../../jmesh/jmesh_device.h"
#include "../../network/jmesh_netkey.h"
#include"../../driver/jmesh_gpio.h"
#define UNPROVISION_PERIOD   1000
#define PROVISIONING_PERIOD   300
#define PROVISIONED_PERIOD   3000

static bool     prov_check_progress(uint8_t* dat);
static uint8_t  prov_get_current_progress(void);
static void     prov_reset_progress(void);

static uint16_t gatt_provisioning_invite_handler(uint8_t* dat, uint8_t* dat_out);
static uint16_t gatt_provisioning_capabilities(uint8_t* dat);
static uint16_t gatt_provisioning_start_handler(uint8_t* dat, uint8_t* dat_out);
static uint16_t gatt_provisioning_publickey(uint8_t* dat);
static uint16_t gatt_provisioning_publickey_handler(uint8_t* dat, uint8_t* dat_out);
//static uint16_t gatt_provisioning_input_complete(uint8_t* dat);
//static uint16_t gatt_provisioning_input_complete_handler(uint8_t* dat, uint8_t* dat_out);
static uint16_t gatt_provisioning_confirmation(uint8_t* dat);
static uint16_t gatt_provisioning_confirmation_handler(uint8_t* dat, uint8_t* dat_out);
static uint16_t gatt_provisioning_random(uint8_t* dat);
static uint16_t gatt_provisioning_random_handler(uint8_t* dat, uint8_t* dat_out);
static uint16_t gatt_provisioning_data_handler(uint8_t* dat, uint8_t* dat_out);
static uint16_t gatt_provisioning_complete(uint8_t* dat);
static uint16_t gatt_provisioning_failed(uint8_t* dat, uint8_t error_code);
static uint16_t gatt_provisioning_failed_handler(uint8_t* dat, uint8_t* dat_out);

//as
static provision_invite_format_t        stored_invite_data;                 //'Invite' frame from provisioner
static provision_capabilities_format_t  stored_capabilities_data;           //'Capabilities' frame from unprov device
static provision_start_format_t         stored_start_data;                  //'Start' frame from provisioner
static provision_public_key_format_t    stored_public_key;                  //'Public Key' from local
static provision_public_key_format_t    stored_peer_public_key;             //'Public Key' from remote

static uint8_t                          stored_authentication_value[16];    //'Authentication value' from OOB(for provisioner) or Generate by code(for unprov device)
static uint8_t                          stored_private_key[32];             //'Private Key' from local paired with local 'Public Key'
static uint8_t                          stored_secret_key[32];              //'Secret Key' generated by remote 'Public Key' & local 'Private Key'
static uint8_t                          stored_confirmation_salt[16];       //'Confirmation salt' generated by 'Invite'&'Capabilities'&'Start'&'PublicKey'&'PublicKey'
static uint8_t                          stored_session_key[16];
static uint8_t                          stored_session_nonce[16];
static uint8_t                          stored_confirmation_key[16];

static provision_confirmation_format_t  stored_confirmation_data;           //'Confirmation' frame from local
static provision_confirmation_format_t  stored_peer_confirmation_data;      //'Confirmation' frame from remote
static provision_random_format_t        stored_random_data;                 //'Random' frame from local
static provision_random_format_t        stored_peer_random_data;            //'Random' frame from remote
static provision_data_format_t          stored_provision_data;              //'Data' frame from provisioner
static uint8_t                          stored_device_key[16];              //device key at this network

static prov_pdu_type_t  CURRENT_STAGE   = Prov_Idle;

//timer
static os_timer_event_t link_hold_timer,ind_light_timer;
void ind_light_timer_handler(const void* argc)
{
		static unsigned char on_off = 1;
		os_timer_event_restart(&ind_light_timer);
		jmesh_gpio_set(JMESH_LED1, on_off);
		on_off ^= 1;
}
void network_light_indicate_init(void)
{
		unsigned short addr = jmesh_get_primary_addr();
		if(addr == JMESH_ADDR_UNASSIGNED)
		{
        os_timer_event_set(&ind_light_timer, UNPROVISION_PERIOD, ind_light_timer_handler, NULL);
		}else{
        os_timer_event_set(&ind_light_timer, PROVISIONED_PERIOD, ind_light_timer_handler, NULL);

		}
}

Device_OOB_Init(test2_profile,
                2,
                ECDH,
                None,
                StaticOOB,
                6,
                Blink,
                0,
                0);

static void link_hold_timeout_handler(os_data_t dat)
{
    prov_reset_progress();
    memset((uint8_t*)&stored_invite_data, 0, sizeof(provision_invite_format_t));
    memset((uint8_t*)&stored_capabilities_data, 0, sizeof(provision_capabilities_format_t));
    memset((uint8_t*)&stored_start_data, 0, sizeof(provision_start_format_t));
    memset((uint8_t*)&stored_public_key, 0, sizeof(provision_public_key_format_t));
    memset((uint8_t*)&stored_peer_public_key, 0, sizeof(provision_public_key_format_t));
    uint8_t dat_out[2];
    uint16_t len = gatt_provisioning_failed(dat_out, OutofResources);

}

static bool prov_check_progress(uint8_t* dat)
{
    //unprovsioned device action
    if (CURRENT_STAGE==Prov_Idle && dat[0]==Prov_Invite) {
        os_timer_event_set(&link_hold_timer, LINK_HOLD_DURATION, link_hold_timeout_handler, NULL);
        CURRENT_STAGE = Prov_Capabilities;
        return true;
    }
    if (CURRENT_STAGE==Prov_Capabilities && dat[0]==Prov_Start) {
        os_timer_event_restart(&link_hold_timer);
        CURRENT_STAGE = Prov_Start;
        return true;
    }
    if (CURRENT_STAGE==Prov_Start && dat[0]==Prov_Public_Key) {
        os_timer_event_restart(&link_hold_timer);
        CURRENT_STAGE = Prov_Public_Key;
        return true;
    }
    if (CURRENT_STAGE==Prov_Public_Key && dat[0]==Prov_Confirmation) {
        os_timer_event_restart(&link_hold_timer);
        CURRENT_STAGE = Prov_Confirmation;
        return true;
    }
    if (CURRENT_STAGE==Prov_Confirmation && dat[0]==Prov_Random) {
        os_timer_event_restart(&link_hold_timer);
        CURRENT_STAGE = Prov_Random;
        return true;
    }
    if (CURRENT_STAGE==Prov_Random && dat[0]==Prov_Data) {
        os_timer_event_restart(&link_hold_timer);
        CURRENT_STAGE = Prov_Complete;
        //TODO: finished being provisioned, now become a node , close link;
        return true;
    }
    if (dat[0] == Prov_Failed) {
        os_timer_event_remove(&link_hold_timer);
        CURRENT_STAGE = Prov_Idle;
        return true;
    }
    os_timer_event_remove(&link_hold_timer);
    print_note("provisioning now step=%x,receive unexpected pdu=%x\n",CURRENT_STAGE,dat[0]);
    //my_printf("unexcept PDU,C:%x--R:%x\n",CURRENT_STAGE, dat[0]);
    return false;
}

static inline uint8_t prov_get_current_progress()
{
    return CURRENT_STAGE;
}

static inline void prov_reset_progress()
{
    CURRENT_STAGE = Prov_Idle;
    os_timer_event_remove(&link_hold_timer);
}



//invite
static uint16_t gatt_provisioning_invite_handler(uint8_t* dat, uint8_t* dat_out)
{
    //my_printf("received Invite...\n");
    memcpy(&stored_invite_data,dat+1,sizeof(provision_invite_format_t));
//		os_timer_event_set(&ind_light_timer, PROVISIONING_PERIOD, ind_light_timer_handler, NULL);
    print_note("provisioning recv intive attention duration=%d\n",stored_invite_data.Attention_Duration);
    return gatt_provisioning_capabilities(dat_out);
}

//>>> capabilities
static uint16_t gatt_provisioning_capabilities(uint8_t* dat)
{
    //my_printf("sending capabilities...\n");
    print_note("provisioning send capabilities\n");//here suggest to print all capabilities
    dat[0] = prov_get_current_progress();
    memcpy(dat+1,&test2_profile,sizeof(provision_capabilities_format_t));
    stored_capabilities_data = test2_profile;
    return LEN_PROVISION_PARAMETERS[Prov_Capabilities] + 1;
}

//<<< start
static uint16_t gatt_provisioning_start_handler(uint8_t* dat, uint8_t* dat_out)
{
    //my_printf("received start...\n");
    memcpy(&stored_start_data,dat+1,sizeof(provision_start_format_t));
    print_note("provisioning recv start algorithm=%d,public key=%d,auth method=%d,auth action=%d,auth size=%d\n",
               stored_start_data.Algorithm,stored_start_data.PublicKey,stored_start_data.AuthenticationMethod,stored_start_data.AuthenticationAction,stored_start_data.AuthenticationSize);
    return 0;
}

//>>> public key
static uint16_t gatt_provisioning_publickey(uint8_t* dat)
{
    //my_printf("sending public key...\n");
    dat[0] = prov_get_current_progress();
    //make public key and private key
    if (!uECC_make_key((uint8_t*)&stored_public_key, stored_private_key, uECC_secp256r1()))
        return gatt_provisioning_failed(dat, OutofResources);
    if (!uECC_shared_secret((uint8_t*)&stored_peer_public_key, stored_private_key, stored_secret_key, uECC_secp256r1()))
        return gatt_provisioning_failed(dat, OutofResources);

    print_buffer_note(sizeof(provision_public_key_format_t),(uint8_t*)&stored_public_key,"provisioning make public key:\n");
    print_buffer_note(32,stored_private_key,"provisioning make private key:\n");

    memcpy(dat+1, (uint8_t*)&stored_public_key, 64);
    do_authentication_action(stored_invite_data.Attention_Duration, &stored_start_data, stored_authentication_value);
    return LEN_PROVISION_PARAMETERS[Prov_Public_Key] + 1;
}

///<<< public key
static uint16_t gatt_provisioning_publickey_handler(uint8_t* dat, uint8_t* dat_out)
{
   // my_printf("received public key...\n");
    memcpy(&stored_peer_public_key,dat+1,sizeof(provision_public_key_format_t));
    print_buffer_note(sizeof(provision_public_key_format_t),stored_peer_public_key.public_key_x,"provisioning recv public key:\n");
    return gatt_provisioning_publickey(dat_out);
}

//>>> input complete
//uint16-t gatt_provisioning_input_complete(uint8_t* dat)
//{
//
//}

//<<< input complete
//uint16_t gatt_provisioning_input_complete_handler(uint8_t* dat, uint8_t* dat_out)
//{
//
//}

//>>> confirmation (ONLY FOR unprovisioned device)
static uint16_t gatt_provisioning_confirmation(uint8_t* dat)
{
    //my_printf("sending confirmation...\n");
    provision_confirmation_format_t* prov_dat = (provision_confirmation_format_t*)(dat+1);
    dat[0] = prov_get_current_progress();

    calculate_confirmation(&stored_invite_data,
                            &stored_capabilities_data,
                            &stored_start_data,
                            &stored_peer_public_key,
                            &stored_public_key,
                            stored_secret_key,
                            stored_authentication_value,
                            &stored_random_data,
                            &stored_confirmation_data,
                            stored_confirmation_salt,
                            stored_confirmation_key);
    print_buffer_note(16,&stored_confirmation_data,"provisioning send confirmation :\n");

    *prov_dat = stored_confirmation_data;
    return LEN_PROVISION_PARAMETERS[Prov_Confirmation]+1;
}

//<<< confirmation
static uint16_t gatt_provisioning_confirmation_handler(uint8_t* dat, uint8_t* dat_out)
{
    //my_printf("received confirmation...\n");
    print_buffer_note(16,dat+1,"provisioning recv confirmation:\n");

    //verify_authentication_action(&stored_start_data, stored_authentication_value);
    //TODO: verify auth value
    memcpy(&stored_peer_confirmation_data,dat+1,sizeof(provision_confirmation_format_t));
    return gatt_provisioning_confirmation(dat_out);
}

//>>> random
static uint16_t gatt_provisioning_random(uint8_t* dat)
{
    //my_printf("sending random...\n");
    print_buffer_note(16,&stored_random_data,"provisioning send random:");

    dat[0] = prov_get_current_progress();
    memcpy(dat+1,&stored_random_data,16);
    return LEN_PROVISION_PARAMETERS[Prov_Random]+1;
}

//<<< random
static uint16_t gatt_provisioning_random_handler(uint8_t* dat, uint8_t* dat_out)
{
    //my_printf("received random...\n");
    print_buffer_note(sizeof(provision_random_format_t),dat+1,"provisioning recv random:\n");
    memcpy(&stored_peer_random_data,dat+1,sizeof(provision_random_format_t));
    //check confirmation
    if (check_confirmation(&stored_peer_random_data, stored_authentication_value, stored_confirmation_key, &stored_peer_confirmation_data)) {
        //calculate provision keys and nonce
        calculate_provision_key(stored_confirmation_salt,
                            &stored_random_data,
                            &stored_peer_random_data,
                            stored_secret_key,
                            stored_session_key,
                            stored_session_nonce,
                            stored_device_key);
        return gatt_provisioning_random(dat_out);
    }
    else {
        return gatt_provisioning_failed(dat_out, ConfirmationFailed);
    }
}
extern unsigned char phone_connect_id ;
extern unsigned char phone_mac[6] ;
//<<< data
static uint16_t gatt_provisioning_data_handler(uint8_t* dat, uint8_t* dat_out)
{
    //my_printf("received data...\n");
    unsigned short short_addr;
    unsigned long iv_index;
    unsigned short nid;

    //decript data (DecryptionFailed)
    stored_provision_data = *(provision_data_format_t*)(dat+1);
    if (decrypt_provision_data(stored_session_key, stored_session_nonce, &stored_provision_data) != 0) {
        print_note("provisioning decrypt data fail!!!\n");
        return gatt_provisioning_failed(dat_out, DecryptionFailed);
    }
    else {
        //TODO: join the network
        provision_data_format_t* param = &stored_provision_data;

        JMESH_LITTLE_ENDIAN_PICK2(short_addr,param->unicast_addr);
        JMESH_BIG_ENDIAN_PICK4(iv_index,param->iv_index);
        JMESH_BIG_ENDIAN_PICK2(nid,param->key_index);
		jmesh_set_primary_addr(short_addr);
        print_buffer_note(16,param->network_key,"provisioning short addr=%2x,nid=%4x,iv_index=%4x,flag=%x,netkey:",short_addr,nid,iv_index,param->flags);
        if(0){
            //failed, not enough memb, return failed with error-code:OutpofResources
            return gatt_provisioning_failed(dat_out, OutofResources);
        }
        else {
            //1. short addr 2. 6bytes mac 3. devkey
            uint8_t mac_addr[] = BX_DEV_ADDR;
			jmesh_device_clear();/**< join new net,provisioning server only save local device key */
            jmesh_device_new(short_addr, 2,mac_addr, stored_device_key);
			jmesh_network_init();  // clear the queue;
			//jmesh_routing_add(1,2,1,0);
			jmesh_routing_neighbor_add(1,phone_mac,200,0);
            if (jmesh_netkey_state_set(param->key_index[0]*0x100+param->key_index[1], param->network_key) !=0) {
                return gatt_provisioning_failed(dat_out, OutofResources);
            }
						jmesh_netkey_test_set_iv_index(0,iv_index);

            return gatt_provisioning_complete(dat_out);
            //TODO: send succeed msg to uart
           // my_printf("received data and decrypted provision data\n");
        }
    }
}

//>>> complete
static uint16_t gatt_provisioning_complete(uint8_t* dat)
{
    print_info("sending complete...\n");
    //provision_complete_format_t* prov_dat = (provision_complete_format_t*)(dat+1);
    dat[0] = prov_get_current_progress();

    prov_reset_progress();
    return LEN_PROVISION_PARAMETERS[Prov_Complete]+1;
}

//>>> failed
static uint16_t gatt_provisioning_failed(uint8_t* dat, uint8_t error_code)
{
    //my_printf("sending failed...\n");
    print_warning("provisioning failed error=%d\n",error_code);
    provision_failed_format_t* prov_dat = (provision_failed_format_t*)(dat+1);
    dat[0] = Prov_Failed;
    prov_dat->error_code = error_code;
    //TODO: close link(Mutex)
		if(JMESH_ADDR_UNASSIGNED == jmesh_get_primary_addr())
		{
//				os_timer_event_set(&ind_light_timer, UNPROVISION_PERIOD, ind_light_timer_handler, NULL);

		}else{
//				os_timer_event_set(&ind_light_timer, PROVISIONED_PERIOD, ind_light_timer_handler, NULL);

		}

    prov_reset_progress();
    return LEN_PROVISION_PARAMETERS[Prov_Failed]+1;
}

//<<< failed
static uint16_t gatt_provisioning_failed_handler(uint8_t* dat, uint8_t* dat_out)
{
    print_info("received failed...\n");
    if (!prov_check_progress(dat))
        return 0;

    return 1;
}

typedef uint16_t (*gatt_prov_device_handler_t)(uint8_t*, uint8_t*);
//prov server
static gatt_prov_device_handler_t gatt_provisioning_server_handler[Prov_Idle] = {
    [Prov_Invite]       = gatt_provisioning_invite_handler,
    [Prov_Start]        = gatt_provisioning_start_handler,
    [Prov_Public_Key]   = gatt_provisioning_publickey_handler,
    [Prov_Confirmation] = gatt_provisioning_confirmation_handler,
    [Prov_Random]       = gatt_provisioning_random_handler,
    [Prov_Data]         = gatt_provisioning_data_handler,
    [Prov_Failed]       = gatt_provisioning_failed_handler
};


int jmesh_provision_send_handler(jmesh_pdu_t* pdu)
{
		if(pdu->length > 0){
			jmesh_proxy_send(JMESH_SEND_TYPE_CONNECT,pdu->proxy.head,0xff,JMESH_PROXY_TYPE_PROVISION,pdu->length,pdu->pdu+2);
		}
		jmesh_pdu_free(pdu);
		return 0;
}
int jmesh_provision_recv_handler(jmesh_pdu_t* pdu)
{
		jmesh_pdu_t* pdu_out = jmesh_pdu_new(100);
		if(pdu_out == NULL) return -1;
		pdu_out->proxy.head = pdu->proxy.head;

    if(pdu->proxy.para[0] >= Prov_Idle){

				pdu_out->length = gatt_provisioning_failed(pdu_out->proxy.para, InvalidPDU);
				os_event_post(&jmesh_task,JMESH_EVENT_PROVISION_SEND,pdu_out);
				jmesh_pdu_free(pdu);
				return -1;
    }
    if(!prov_check_progress(pdu->proxy.para))
		{
				pdu_out->length = gatt_provisioning_failed(pdu_out->proxy.para, UnexpectedPDU);
				os_event_post(&jmesh_task,JMESH_EVENT_PROVISION_SEND,pdu_out);
				jmesh_pdu_free(pdu);
				return -1;
		}

    pdu_out->length = (*gatt_provisioning_server_handler[pdu->proxy.para[0]])(pdu->proxy.para, pdu_out->proxy.para);
		os_event_post(&jmesh_task,JMESH_EVENT_PROVISION_SEND,pdu_out);
		jmesh_pdu_free(pdu);
    return 0;
}