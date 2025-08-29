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
static uint16_t iter = REPEATS+1;

esp_zb_apsde_data_req_t create_aps_request(uint16_t dest_addr, uint8_t dst_endpoint, uint8_t src_endpoint,
                                           uint16_t profile_id, uint16_t cluster_id, uint8_t *asdu, uint32_t asdu_length,
                                           uint8_t tx_options, bool use_alias, uint16_t alias_src_addr, int alias_seq_num,
                                           uint8_t radius)
{
    esp_zb_apsde_data_req_t req = {
        .dst_addr_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .dst_addr.addr_short = dest_addr,
        .dst_endpoint = dst_endpoint,
        .profile_id = profile_id,
        .cluster_id = cluster_id,
        .src_endpoint = src_endpoint,
        .asdu_length = asdu_length,
        .asdu = asdu,
        .tx_options = tx_options,
        .use_alias = use_alias,
        .alias_src_addr = alias_src_addr,
        .alias_seq_num = alias_seq_num,
        .radius = radius
    };
    return req;
}


void traffic_reporter_init(){
    byte_counter = 0;
    byte_count = 0;
    while (1) {
        ESP_LOGI(TAG, "Byte count in last 10 seconds: %ld", byte_count);
        vTaskDelay(pdMS_TO_TICKS(10000)); // Wait for 10 seconds
        byte_count = byte_counter; // Store the current byte count
        byte_counter = 0; // Reset the counter after sending the report
        send_traffic_report();
    }    
}

//function creatiing 68 bytes payload and sending it to the destination address
void create_ping(uint16_t dest_addr);
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

void esp_zigbee_include_show_tables(void)
{
    ESP_LOGI(TAG, "Zigbee Network Tables:");
    esp_show_neighbor_table();
    esp_show_route_table();
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
        // ESP_LOG_BUFFER_CHAR_LEVEL("APSDE CONFIRM", confirm.asdu, confirm.asdu_length, ESP_LOG_INFO);
        
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
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING); // Rejoin the network if the destination is the coordinator
            }
        }
    }
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
            create_ping(ind.src_short_addr); // Respond to the received data
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
        .tx_options = 0,                            // Example transmission options
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


    //ESP_LOGI(TAG, "Sending APS data request to 0x%04hx with %ld bytes", dest_addr, data_length);
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_aps_data_request(&req);
    esp_zb_lock_release();
    free(req.asdu); // Free the allocated memory for ASDU
}

void create_network_load(uint16_t dest_addr, uint8_t repetitions)
{
    for(int8_t i = 0; i < repetitions; i++) {
        create_ping(dest_addr);
    }
}

void create_network_load_64bit(uint64_t dest_addr, uint8_t repetitions)
{
    for(int8_t i = 0; i < repetitions; i++) {
        create_ping_64(dest_addr);
    }
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
        iter = 0;

        // create_network_load(0x0000);
        // create_network_load_64bit(0x404ccafffe5fae8c, 3);
        // ESP_LOGI("empty line", "");
        // create_network_load_64bit(0x404ccafffe5fb4d4, 3);
        // ESP_LOGI("empty line", "");
        // create_network_load_64bit(0x404ccafffe5de2a8, 3);
        // ESP_LOGI("empty line", "");
    }
}

bool deferred_driver_init(void)
{
    uint8_t button_num = PAIR_SIZE(button_func_pair);

    bool is_initialized = switch_driver_init(button_func_pair, button_num, button_handler);
    return is_initialized;
}


void send_traffic_report(void)
{
    esp_zb_network_traffic_report_t traffic_report = {
        .traffic_count = 0, // Initialize traffic count
    };

    esp_zb_nwk_info_iterator_t itor = ESP_ZB_NWK_INFO_ITERATOR_INIT;
    esp_zb_nwk_neighbor_info_t neighbor = {};
    
    const uint8_t traffic_report_endpoint = 70;


    while (ESP_OK == esp_zb_nwk_get_next_neighbor(&itor, &neighbor)) {
        if( neighbor.relationship == ESP_ZB_NWK_RELATIONSHIP_CHILD){
            esp_zb_apsde_data_req_t req = create_aps_request(neighbor.short_addr, traffic_report_endpoint, traffic_report_endpoint, ESP_ZB_AF_HA_PROFILE_ID,
                               ESP_ZB_ZCL_CLUSTER_ID_BASIC, (uint8_t *)&traffic_report, sizeof(esp_zb_network_traffic_report_t),
                               0, false, 0, 0, 3);
            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_aps_data_request(&req);
            esp_zb_lock_release();
        }
    }

}

void beacon_task(void *pvParameters)
{

    uint32_t time_start = 0;
    while(!esp_zb_bdb_dev_joined()){
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    while (1) {
        while(iter >= REPEATS){vTaskDelay(pdMS_TO_TICKS(100));} // Block task
        time_start = pdTICKS_TO_MS(xTaskGetTickCount());
        while(iter < REPEATS){
            create_ping(DEST_ADDR);
            vTaskDelay(pdMS_TO_TICKS(DELAY_MS)); // Wait for 0 milliseconds
            iter++;
        }
        ESP_LOGI(TAG, "Start time: %ld, End time: %ld, Passed time: %ld", time_start, pdTICKS_TO_MS(xTaskGetTickCount()), pdTICKS_TO_MS(xTaskGetTickCount()) - time_start);
    }
}


void refresh_routes(void)
{
    esp_zb_nwk_info_iterator_t itor = ESP_ZB_NWK_INFO_ITERATOR_INIT;
    esp_zb_nwk_route_info_t route = {};

    ESP_LOGI(TAG, "Refreshing Zigbee Network Routes:");
    while (ESP_OK == esp_zb_nwk_get_next_route(&itor, &route)) {
        create_ping(route.dest_addr);
    }
}
