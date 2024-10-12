//
// Created by Eugenio Moro on 04/24/23.
//

#include "gnb_message_handlers.h"
#include <stdbool.h>
#define CONNECTED_UES 4

int gnb_id = 0;
bool is_initialized = false;
typedef struct {
    int rnti;
    // bool prop_1;
    // float prop_2;
    float ue_rsrp;
    float ue_ber_up;
    float ue_ber_down;
    float ue_mcs_up;
    float ue_mcs_down;
    float cell_load;
} ue_struct;
ue_struct connected_ue_list[CONNECTED_UES];

/*
 * initialize the ues
 */
void initialize_ues_if_needed(){
    if(is_initialized)
        return;
    for (int ue=0;ue<CONNECTED_UES;ue++){
        connected_ue_list[ue].rnti = rand();
        // connected_ue_list[ue].prop_1 = false;
        // connected_ue_list[ue].prop_2 = -1;
    }
    is_initialized = true;
}

/*
this function has not been implemented and it won't be needed in the foreseeable future
*/
void handle_subscription(RANMessage* in_mess){
    printf("Not implemented\n");
    ran_message__free_unpacked(in_mess,NULL);
    initialize_ues_if_needed();
    assert(0!=0);
}
/*
this function just basically prints out the parameters in the request and passes the in_mess to the response generator
*/
void handle_indication_request(RANMessage* in_mess,int out_socket, sockaddr_in peeraddr){
    initialize_ues_if_needed();
    printf("Indication request for %lu parameters:\n", in_mess->ran_indication_request->n_target_params);
    for(int par_i=0; par_i<in_mess->ran_indication_request->n_target_params; par_i++){
        printf("\tParameter id %d requested (a.k.a %s)\n",\
        in_mess->ran_indication_request->target_params[par_i],\
        get_enum_name(in_mess->ran_indication_request->target_params[par_i]));
    }
    build_indication_response(in_mess, out_socket, peeraddr);
}

/*
this function builds and sends the indication response based on the map inside the in_mess
in_mess is cleared here
*/
void build_indication_response(RANMessage* in_mess, int out_socket, sockaddr_in servaddr){

    RANIndicationResponse rsp = RAN_INDICATION_RESPONSE__INIT;
    RANParamMapEntry **map;
    void* buf;
    unsigned buflen, i;

    // allocate space for the pointers inside the map, which is NULL terminated so it needs 1 additional last pointer
    map = malloc(sizeof(RANParamMapEntry*) * (in_mess->ran_indication_request->n_target_params + 1));

    // now build every element inside the map
    for(i=0; i<in_mess->ran_indication_request->n_target_params; i++){

        // allocate space for this entry and initialize
        map[i] = malloc(sizeof(RANParamMapEntry));
        ran_param_map_entry__init(map[i]);

        // assign key
        map[i]->key=in_mess->ran_indication_request->target_params[i];

        // read the parameter and save it in the map
        ran_read(map[i]->key, map[i]);
    }
    // the map is ready, add the null terminator
    map[in_mess->ran_indication_request->n_target_params] = NULL;

    rsp.n_param_map=in_mess->ran_indication_request->n_target_params;
    rsp.param_map=map;
    buflen = ran_indication_response__get_packed_size(&rsp);
    buf = malloc(buflen);
    ran_indication_response__pack(&rsp,buf);
    printf("Sending indication response\n");
    unsigned slen = sizeof(servaddr);
    int rev = sendto(out_socket, (const char *)buf, buflen,
                     MSG_CONFIRM, (const struct sockaddr *) &servaddr,
                     slen);
    printf("Sent %d bytes, buflen was %u\n",rev, buflen);

    // free map and buffer (rsp not freed because in the stack)
    free_ran_param_map(map);
    free(buf);
    // free incoming ran message
    ran_message__free_unpacked(in_mess,NULL);
}

/*
this function frees a map through introspection, maps !!MUST!! be NULL terminated
*/
void free_ran_param_map(RANParamMapEntry **map){
    int i = 0;
    while(map[i] != NULL){
        // we first need to clear whatever is inside the map entry, we need to consider all the possible value types
        switch(map[i]->value_case){
            case RAN_PARAM_MAP_ENTRY__VALUE_INT64_VALUE:
                // there is no pointer inside the entry to free in this case
                break;
            case RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE:
                // free the string and then the entry
                free(map[i]->string_value);
                break;
            case RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST:
                // in this case we free the ue list first
                free_ue_list(map[i]->ue_list);
                break;
            case RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET:
                // nothing to do here, skip to default
            default:
                break;
        }
        // now we can free the entry
        free(map[i]);
        i++;
    }
}

void handle_control(RANMessage* in_mess){
    initialize_ues_if_needed();
    // loop tarhet params and apply
    for(int i=0; i<in_mess->ran_control_request->n_target_param_map; i++){
        printf("Applying target parameter %s with value %s\n",\
        get_enum_name(in_mess->ran_control_request->target_param_map[i]->key),\
        in_mess->ran_control_request->target_param_map[i]->string_value);
        ran_write(in_mess->ran_control_request->target_param_map[i]);
    }
    // free incoming ran message
    ran_message__free_unpacked(in_mess,NULL);
}

const char* get_enum_name(RANParameter ran_par_enum){
    switch (ran_par_enum)
    {
        case RAN_PARAMETER__GNB_ID:
            return "gnb_id";
        case RAN_PARAMETER__UE_LIST:
            return "ue_list";
        default:
            return "unrecognized param";
    }
}

void ran_write(RANParamMapEntry* target_param_map_entry){
    switch (target_param_map_entry->key)
    {
        case RAN_PARAMETER__GNB_ID:
            gnb_id = atoi(target_param_map_entry->string_value);
            break;
        case RAN_PARAMETER__UE_LIST: // if we receive a ue list message we need to apply its content
            apply_properties_to_ue_list(target_param_map_entry->ue_list);
            break;
        default:
            printf("ERROR: cannot write RAN, unrecognized target param %d\n", target_param_map_entry->key);
    }
}

void apply_properties_to_ue_list(UeListM* ue_list){
    // loop the ues and apply what needed to each, according to what is inside the list received from the xapp
    for(int ue=0; ue<ue_list->n_ue_info; ue++){
        // apply generic properties (example)
        set_ue_properties(ue_list->ue_info[ue]->rnti);
                        //   ue_list->ue_info[ue]->prop_1,
                        //   ue_list->ue_info[ue]->prop_2
        // more stuff later when needed     
    }
}

// void set_ue_properties(int rnti, bool prop_1, float prop_2){
void set_ue_properties(int rnti){

    // iterate ue list until rnti is found
    bool rnti_not_found = true;
    for(int ue=0; ue<CONNECTED_UES; ue++) {
        if(connected_ue_list[ue].rnti == rnti){
            printf("RNTI found\n");
            // connected_ue_list[ue].prop_1 = prop_1;
            // connected_ue_list[ue].prop_2 = prop_2;
            rnti_not_found = false;
            break;
        } else {
            continue;
        }
    }
    if(rnti_not_found){
        printf("RNTI %u not found\n", rnti);
    }
}

char* int_to_charray(int i){
    int length = (snprintf(NULL, 0,"%d",i)+1);
    char* ret = malloc(length*sizeof(char));
    sprintf(ret, "%d", i);
    return ret;
}

void handle_master_message(void* buf, int buflen, int out_socket, struct sockaddr_in servaddr){
    initialize_ues_if_needed();
    RANMessage* in_mess = ran_message__unpack(NULL, (size_t)buflen, buf);
    if (!in_mess){
        printf("Error decoding received message, printing for debug:\n");
        for(int i=0;i<buflen; i++){
            uint8_t* tempbuf = (uint8_t*) buf;
            printf(" %hhx ", tempbuf[i]);
        }
        printf("\n");
        return;
    }
    printf("ran message id %d\n", in_mess->msg_type);
    switch(in_mess->msg_type){
        case RAN_MESSAGE_TYPE__SUBSCRIPTION:
            printf("Subcription message received\n");
            handle_subscription(in_mess);
            break;
        case RAN_MESSAGE_TYPE__INDICATION_REQUEST:
            printf("Indication request message received\n");
            handle_indication_request(in_mess, out_socket, servaddr);
            break;
        case RAN_MESSAGE_TYPE__INDICATION_RESPONSE:
            printf("Indication response message received\n");
            build_indication_response(in_mess, out_socket, servaddr);
            break;
        case RAN_MESSAGE_TYPE__CONTROL:
            printf("Control message received\n");
            handle_control(in_mess);
            break;
        default:
            printf("Unrecognized message type\n");
            ran_message__free_unpacked(in_mess,NULL);
            break;
    }
}


UeListM* build_ue_list_message(){
    // init ue list protobuf message
    UeListM* ue_list_m = malloc(sizeof(UeListM));
    ue_list_m__init(ue_list_m);

    // insert n ues
    ue_list_m->connected_ues = CONNECTED_UES;
    ue_list_m->n_ue_info = CONNECTED_UES;

    // if no ues are connected then we can stop and just return the message
    if(CONNECTED_UES == 0){
        return ue_list_m;
    }
    // build list of ue_info_m (this is also a protobuf message)
    UeInfoM** ue_info_list;
    ue_info_list = malloc(sizeof(UeInfoM*)*(CONNECTED_UES+1)); // allocating space for 1 additional element which will be NULL (terminator element)
    for(int i = 0; i<CONNECTED_UES; i++){
        // init list
        ue_info_list[i] = malloc(sizeof(UeInfoM));
        ue_info_m__init(ue_info_list[i]);

        // read rnti and add to message
        ue_info_list[i]->rnti = connected_ue_list[i].rnti;

        // read mesures and add to message (actually just send random data)

        // measures
        // ue_info_list[i]->has_meas_type_1 = 1;
        // ue_info_list[i]->meas_type_1 = rand();
        // ue_info_list[i]->has_meas_type_2 = 1;
        // ue_info_list[i]->meas_type_2 = rand();
        // ue_info_list[i]->has_meas_type_3 = 1;
        // ue_info_list[i]->meas_type_3 = rand();
        ue_info_list[i]->has_ue_rsrp = 1;
        ue_info_list[i]->ue_rsrp = ((float)rand() / 100) * (-70.0 - (-120.0)) + (-120.0);
        ue_info_list[i]->has_ue_ber_up = 1;
        ue_info_list[i]->ue_ber_up = ((float)(rand() % 100)) / 100.0;
        ue_info_list[i]->has_ue_ber_down = 1;
        ue_info_list[i]->ue_ber_down = ((float)(rand() % 100)) / 100.0;
        ue_info_list[i]->has_ue_mcs_up = 1;
        ue_info_list[i]->ue_mcs_up = rand() % 28;
        ue_info_list[i]->has_ue_mcs_down = 1;
        ue_info_list[i]->ue_mcs_down = rand() % 28;
        ue_info_list[i]->has_cell_load = 1;
        ue_info_list[i]->cell_load = rand() % 100;

        // properties
        // ue_info_list[i]->has_prop_1 = 1;
        // ue_info_list[i]->prop_1 = connected_ue_list[i].prop_1;
        // if(connected_ue_list[i].prop_2 > -1){
        //     ue_info_list[i]->has_prop_2 = 1;
        //     ue_info_list[i]->prop_2 = connected_ue_list[i].prop_2;
        // }


    }
    // add a null terminator to the list
    ue_info_list[CONNECTED_UES] = NULL;
    // assgin ue info pointer to actually fill the field
    ue_list_m->ue_info = ue_info_list;
    return ue_list_m;
}

// careful, this function leaves dangling pointers - not a big deal in this case though 
void free_ue_list(UeListM* ue_list_m){
    if(ue_list_m->connected_ues > 0){
        // free the ue list content first
        int i=0;
        while(ue_list_m->ue_info[i] != NULL){ // when we reach NULL we have found the terminator (no need to free the terminator because it hasn't been allocated)
            free(ue_list_m->ue_info[i]);
            i++;
        }
        // then free the list
        free(ue_list_m->ue_info);
    }
    // finally free the outer data structure
    free(ue_list_m);
}

void ran_read(RANParameter ran_par_enum, RANParamMapEntry* map_entry){
    switch (ran_par_enum)
    {
        case RAN_PARAMETER__GNB_ID:
            map_entry->value_case=RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE;
            map_entry->string_value = int_to_charray(gnb_id);
            break;
        case RAN_PARAMETER__UE_LIST:
            map_entry->value_case=RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST;
            map_entry->ue_list = build_ue_list_message();
            break;
        default:
            printf("Unrecognized param %d\n",ran_par_enum);
            assert(0!=0);
    }
}