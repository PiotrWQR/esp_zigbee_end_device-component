#include <stdio.h>
#include "Helpers.h"


#include "esp_check.h"
#include "esp_log.h"
#include "switch_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_zigbee_core.h"
#include "aps/esp_zigbee_aps.h"

#include <memory.h>



static const char *TAG = "esp_zigbee_include";


static uint32_t byte_counter = 0;
static uint32_t byte_count = 0;

static uint16_t successful_ping_count = 0;
static uint16_t failed_ping_count = 0;
static uint32_t bytes = 0;
static TaskHandle_t beacon_task_handle = NULL;

//function creatiing 68 bytes payload and sending it to the destination address
void create_ping(uint16_t dest_addr);
void create_ping_seq(uint16_t dest_addr, uint32_t seq_num);
void create_ping_64bit(uint64_t dest_addr);
void create_network_load(uint16_t dest_addr, uint8_t repetitions);
void create_network_load_64bit(uint64_t dest_addr, uint8_t repetitions);


//wyświetla sąsiadów
void esp_show_neighbor_table()
{

    esp_zb_nwk_info_iterator_t itor = ESP_ZB_NWK_INFO_ITERATOR_INIT;
    esp_zb_nwk_neighbor_info_t neighbor = {};
    
    ESP_LOGI(TAG,"Network Neighbors:");
    while (ESP_OK == esp_zb_nwk_get_next_neighbor(&itor, &neighbor)) {
        ESP_LOGI(TAG,"Index: %3d", itor);
        ESP_LOGI(TAG,"  Age: %3d", neighbor.age);
        ESP_LOGI(TAG,"  Neighbor: 0x%04hx", neighbor.short_addr);
        ESP_LOGI(TAG,"  IEEE: 0x%016" PRIx64, *(uint64_t *)neighbor.ieee_addr);
        ESP_LOGI(TAG,"  Type: %3s", dev_type_name[neighbor.device_type]);   
        ESP_LOGI(TAG,"  Rel: %c", rel_name[neighbor.relationship]);
        ESP_LOGI(TAG,"  Depth: %3d", neighbor.depth);
        ESP_LOGI(TAG,"  RSSI: %3d", neighbor.rssi);
        ESP_LOGI(TAG,"  LQI: %3d", neighbor.lqi);
        ESP_LOGI(TAG,"  Cost: o:%d", neighbor.outgoing_cost);

        ESP_LOGI(TAG," ");
    }
}

//wyswietla trasy
void esp_show_route_table()
{

    esp_zb_nwk_info_iterator_t itor = ESP_ZB_NWK_INFO_ITERATOR_INIT;
    esp_zb_nwk_route_info_t route = {};
    
    ESP_LOGI(TAG, "Zigbee Network Routes:");
    while (ESP_OK == esp_zb_nwk_get_next_route(&itor, &route)) {
        ESP_LOGI(TAG,"Index: %3d", itor);
        ESP_LOGI(TAG, "  DestAddr: 0x%04hx", route.dest_addr);
        ESP_LOGI(TAG, "  NextHop: 0x%04hx", route.next_hop_addr);
        ESP_LOGI(TAG, "  Expiry: %4d", route.expiry);
        ESP_LOGI(TAG, "  State: %6s", route_state_name[route.flags.status]);
        ESP_LOGI(TAG, "  Flags: 0x%02x", *(uint8_t *)&route.flags);
        ESP_LOGI(TAG," ");
    }
} 


void esp_show_record_route_table()
{   
    esp_zb_nwk_route_record_info_t route_record;
    esp_zb_nwk_info_iterator_t itor = ESP_ZB_NWK_INFO_ITERATOR_INIT;

    while (ESP_OK == esp_zb_nwk_get_next_route_record(&itor, &route_record)) {
        ESP_LOGI(TAG,"Index: %3d", itor);
        ESP_LOGI(TAG,"  DestAddr: 0x%04hx", route_record.dest_address);
        ESP_LOGI(TAG,"  NextHop: 0x%04hx", route_record.expiry);
        ESP_LOGI(TAG,"  Expiry: %4d", route_record.expiry);
        ESP_LOGI(TAG,"  State: %6s", route_state_name[route_record.relay_count]);
        for(int i = 0; i < route_record.relay_count; i++) {
            ESP_LOGI(TAG,"  Path[%d]: 0x%04hx", i, route_record.path[i]);
        }
        ESP_LOGI(TAG," ");
    }
}
void esp_zigbee_include_show_tables(void)
{
    ESP_LOGI(TAG, "Zigbee Network Tables:");
    esp_show_neighbor_table();
    esp_show_route_table();
    esp_show_record_route_table();
}

static switch_func_pair_t button_func_pair[] = {
    {GPIO_INPUT_IO_TOGGLE_SWITCH, SWITCH_ONOFF_TOGGLE_CONTROL}
};



void esp_zb_aps_data_confirm_handler(esp_zb_apsde_data_confirm_t confirm)
{
     if (confirm.status == 0x00) {
        successful_ping_count++;
        ESP_LOGI("APSDE CONFIRM",
                "Sent successfully from endpoint %d, source address 0x%04hx to endpoint %d,"
                "destination address 0x%04hx, tx_time %d ms",
                confirm.src_endpoint, esp_zb_get_short_address(), confirm.dst_endpoint, confirm.dst_addr.addr_short,
                confirm.tx_time);
    } else {
        failed_ping_count++;
        if(confirm.dst_addr_mode == ESP_ZB_APS_ADDR_MODE_64_ENDP_PRESENT || confirm.dst_addr_mode == ESP_ZB_APS_ADDR_MODE_64_PRESENT_ENDP_NOT_PRESENT) {
            ESP_LOGW("APSDE CONFIRM", "Failed to send APSDE-DATA request to 0x%016" PRIx64 ", error code: %d, tx time %d ms",
                     *(uint64_t *)confirm.dst_addr.addr_long, confirm.status, confirm.tx_time);
        } else if(confirm.dst_addr_mode == ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT || confirm.dst_addr_mode == ESP_ZB_APS_ADDR_MODE_16_GROUP_ENDP_NOT_PRESENT) {
            ESP_LOGW("APSDE CONFIRM", "Failed to send APSDE-DATA request to 0x%04hx, error code: %d, tx time %d ms",
                     confirm.dst_addr.addr_short, confirm.status, confirm.tx_time);
            if (confirm.dst_addr.addr_short == 0x0000) {
                ESP_LOGW("APSDE CONFIRM", "Failed to send APSDE-DATA request to coordinator, trying to rejoin the network");
                //esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING); // Rejoin the network if the destination is the coordinator
            }
        }
    }
}

void send_information_to_coordinator(data_to_send_t *data){
    esp_zb_apsde_data_req_t req = {
        .dst_addr_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .dst_endpoint = 20,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        .src_endpoint = 10,
        .asdu_length = sizeof(data_to_send_t),
        .tx_options = ESP_ZB_APSDE_TX_OPT_FRAG_PERMITTED | ESP_ZB_APSDE_TX_OPT_ACK_TX,
        .use_alias = false,
        .alias_src_addr = 0,
        .alias_seq_num = 0,
        .radius = 3,
    };
    req.asdu = malloc(req.asdu_length * sizeof(uint8_t));
    memcpy(req.asdu, data, sizeof(data_to_send_t));
    // ESP_LOGI(TAG, "Sending data to coordinator, start time: %ld, end time: %ld, asdu length: %ld", ((data_to_send_t *)req.asdu)->start_time,
    //     ((data_to_send_t *)req.asdu)->end_time, req.asdu_length);
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_aps_data_request(&req);
    esp_zb_lock_release();
}

bool zb_apsde_data_indication_handler(esp_zb_apsde_data_ind_t ind)
{
    bool processed = false;
    if (ind.status == 0x00) {
        byte_counter += ind.asdu_length + sizeof(esp_zb_apsde_data_ind_t); // Increment the byte counter by the length of the ASDU and the indication structure
        if (ind.dst_endpoint == 70 && ind.profile_id == ESP_ZB_AF_HA_PROFILE_ID && ind.cluster_id == ESP_ZB_ZCL_CLUSTER_ID_BASIC) {
            ESP_LOGI("APSDE INDICATION", "Received APSDE-DATA indication about traffic, source address 0x%04hx,"
                     ", tx_time %d ms", ind.src_short_addr, ind.rx_time);
            ESP_LOG_BUFFER_CHAR_LEVEL("APSDE INDICATION", ind.asdu, ind.asdu_length, ESP_LOG_INFO);
            processed = true; // Mark as processed
        } 
        if (ind.dst_endpoint == 27 && ind.profile_id == ESP_ZB_AF_HA_PROFILE_ID && ind.cluster_id == ESP_ZB_ZCL_CLUSTER_ID_BASIC) {
            vTaskResume(beacon_task_handle);
            processed = true; // Mark as processed
        }
        if(ind.dst_endpoint == 30 && ind.profile_id == ESP_ZB_AF_HA_PROFILE_ID && ind.cluster_id == ESP_ZB_ZCL_CLUSTER_ID_BASIC) {
            setting_change_t *setting_change = (setting_change_t *)ind.asdu;
            ESP_LOGI("APSDE INDICATION", "Received settings change: REPEATS=%d, DEST_ADDR=0x%04hx, DELAY_MS=%ld, DELAY_TICK=%ld",
                     setting_change->new_repeats, setting_change->new_dest_addr, setting_change->new_delay_ms, setting_change->new_delay_tick);
            ESP_LOGI("APSDE_INDICATION", "Received mac config: CSMA_MIN_BE=%d, CSMA_MAX_BE=%d, CSMA_MAX_BACKOFFS=%d",
                     setting_change->csma_min_be, setting_change->csma_max_be, setting_change->csma_max_backoffs);
            REPEATS = setting_change->new_repeats;
            DELAY_MS = setting_change->new_delay_ms;
            DEST_ADDR = setting_change->new_dest_addr;
            DELAY_TICK = setting_change->new_delay_tick;
            esp_zb_platform_mac_config_t mac_config;
                mac_config.csma_min_be = setting_change->csma_min_be;
                mac_config.csma_max_be = setting_change->csma_max_be;
                mac_config.csma_max_backoffs = setting_change->csma_max_backoffs;
            ESP_ERROR_CHECK(esp_zb_platform_mac_config_set(&mac_config));
            // Respond to the received data
        }
    } else {
        ESP_LOGE("APSDE INDICATION", "Invalid status of APSDE-DATA indication, error code: %d", ind.status);
        byte_counter += ind.asdu_length;
        processed = false;
    }
    return processed;
}

void create_ping_64(uint64_t dest_addr)
{
    uint32_t data_length = 70;
    esp_zb_ieee_addr_t ieee_addr;
    memcpy(ieee_addr, &dest_addr, sizeof(esp_zb_ieee_addr_t)); // Copy the 64-bit address into the ieee_addr variable

    esp_zb_apsde_data_req_t req = {
        .dst_addr_mode = ESP_ZB_APS_ADDR_MODE_64_ENDP_PRESENT,
        .dst_endpoint = 10,                                 // Example endpoint
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,              // Example profile ID
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_BASIC,          // Example cluster ID (On/Off cluster)
        .src_endpoint = 10,                                 // Example source endpoint
        .asdu_length = data_length,                         // No payload for ping
        .asdu = malloc(data_length * sizeof(uint8_t)),      // No payload for ping
        .tx_options = 0,                                    // Example transmission options
        .use_alias = false,
        .alias_src_addr = 0,
        .alias_seq_num = 0,
        .radius = 3,                                        // Example radius
    };
    memcpy(req.dst_addr.addr_long, ieee_addr, sizeof(esp_zb_ieee_addr_t)); // Copy the 64-bit address

    for(uint8_t i = 0; i < data_length; i++) {
        req.asdu[i] = i % 256; 
    }

    ESP_LOGI(TAG, "Sending APS data request to 0x%016" PRIx64 " with %ld bytes", dest_addr, data_length);
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_aps_data_request(&req);
    esp_zb_lock_release();
    free(req.asdu); // Free the allocated memory for ASDU
}
//TODO sprawdz większy payload, czy działa
void create_ping(uint16_t dest_addr)
{
    uint32_t data_length = 50; // Example payload length
    esp_zb_apsde_data_req_t req = {
        .dst_addr_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .dst_addr.addr_short = dest_addr,
        .dst_endpoint = 10,                          // Example endpoint
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,      // Example profile ID
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_BASIC,  // Example cluster ID (On/Off cluster)
        .src_endpoint = 10,                          // Example source endpoint
        .asdu_length = data_length,                  // No payload for ping
        .asdu = malloc(data_length * sizeof(uint8_t)), // Allocate memory for ASDU if needed
        .tx_options = ESP_ZB_APSDE_TX_OPT_FRAG_PERMITTED | ESP_ZB_APSDE_TX_OPT_ACK_TX,// Example transmission options
        .use_alias = false,
        .alias_src_addr = 0,
        .alias_seq_num = 0,
        .radius = 3,                                 // Example radius
    };

    if (req.asdu == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for ASDU");
        return;
    } else {
        for (uint8_t i = 0; i < data_length; i++) {
            req.asdu[i] = i % 256; // Fill with some data, e.g., incrementing values
        }
    }
    //ESP_LOGI(TAG, "Size of request: %ld bytes", data_length+ 37);

    bytes += data_length + 37;
    //ESP_LOGI(TAG, "Sending APS data request to 0x%04hx with %ld bytes", dest_addr, data_length);
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_aps_data_request(&req);
    esp_zb_lock_release();
    free(req.asdu); // Free the allocated memory for ASDU
}

void create_ping_seq(uint16_t dest_addr, uint32_t seq_num)
{
    uint32_t data_length = PAYLOAD_SIZE; // Example payload length
    esp_zb_apsde_data_req_t req = {
        .dst_addr_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .dst_addr.addr_short = dest_addr,
        .dst_endpoint = 10,                          // Example endpoint
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,      // Example profile ID
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_BASIC,  // Example cluster ID (On/Off cluster)
        .src_endpoint = 10,                          // Example source endpoint
        .asdu_length = data_length,                  // No payload for ping
        .asdu = malloc(data_length * sizeof(uint8_t)), // Allocate memory for ASDU if needed
        .tx_options = ESP_ZB_APSDE_TX_OPT_FRAG_PERMITTED | ESP_ZB_APSDE_TX_OPT_ACK_TX,// Example transmission options
        .use_alias = false,
        .alias_src_addr = 0,
        .alias_seq_num = 0,
        .radius = 4,                                 // Example radius
    };

    ping_payload_t ping_payload;

    if (req.asdu == NULL) {
        const char *TAG = "BEACON TASK";
        ESP_LOGE(TAG, "Failed to allocate memory for ASDU");
        return;
    } 
    ping_payload.seq_num = seq_num;
    ping_payload.send_time = pdTICKS_TO_MS(xTaskGetTickCount());
    ping_payload.max_ping_count = REPEATS;
    for (uint16_t i = 0; i < PING_PAYLOAD_SIZE; i++) {
        ping_payload.payload[i] = i % 256; // Fill with some data, e.g., incrementing values
    }
    memcpy(req.asdu, &ping_payload, sizeof(ping_payload_t));
    //ESP_LOGI(TAG, "Size of request: %ld bytes", data_length+ 37);

    bytes += data_length + 37;
    //ESP_LOGI(TAG, "Sending APS data request to 0x%04hx with %ld bytes", dest_addr, data_length);
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_aps_data_request(&req);
    esp_zb_lock_release();
    free(req.asdu); // Free the allocated memory for ASDU
}

void display_statistics(void)
{
    ESP_LOGI(TAG, "Failed ping count: %d", failed_ping_count);
    ESP_LOGI(TAG, "Successful ping count: %d", successful_ping_count);
    ESP_LOGI(TAG, "Total ping attempts: %d", failed_ping_count + successful_ping_count);
    // Add code to display relevant statistics
}

void button_handler(switch_func_pair_t *button_func_pair)
{
    if(button_func_pair->func == SWITCH_ONOFF_TOGGLE_CONTROL) {
        display_statistics();
        esp_zigbee_include_show_tables();
        vTaskResume(beacon_task_handle);

    }
}

bool deferred_driver_init(void)
{
    uint8_t button_num = PAIR_SIZE(button_func_pair);

    bool is_initialized = switch_driver_init(button_func_pair, button_num, button_handler);
    return is_initialized;
}


void beacon_task(void *pvParameters)
{
    const char *TAG = "BEACON TASK";
    data_to_send_t data;
    beacon_task_handle  = xTaskGetCurrentTaskHandle();
    
    while (1) {
        
        vTaskSuspend(beacon_task_handle);
        ESP_LOGI(TAG, "Beacon task resumed");
        uint32_t passed_time = 0;
        data.start_time = pdTICKS_TO_MS(xTaskGetTickCount());
        for(int i=0; i < REPEATS; i++){
            create_ping_seq(DEST_ADDR, i);
            vTaskDelay(pdMS_TO_TICKS(DELAY_MS)); // Wait for 0 milliseconds

        }
        data.end_time = pdTICKS_TO_MS(xTaskGetTickCount());
        passed_time = data.end_time - data.start_time;
        data.failed_ping_count = failed_ping_count;
        data.successful_ping_count = successful_ping_count;
        failed_ping_count = 0;
        successful_ping_count = 0;

        send_information_to_coordinator(&data);
        ESP_LOGI(TAG, "Start time: %ld, End time: %ld, Passed time: %ld", data.start_time, data.end_time, passed_time);
        ESP_LOGI(TAG, "Bytes : %ld, payload size: %d", bytes, PAYLOAD_SIZE);
        // ESP_LOGI(TAG, "One millisecond: %ld", pdMS_TO_TICKS(1));
    }
}
