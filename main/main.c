#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "main.h"
#include "command_driver.h"
#include "esp_check.h"
#include <string.h>  // ✅ Inclusione necessaria per memcpy()

#define TAG "ESP_ZB_CALDAIA"

#define CMD_CALDAIA_GPIO 1
#define CMD_TABLET_GPIO 2
#define WIFI_ENABLE_GPIO       3   // GPIO3 -> abilita controllo switch antenna
#define WIFI_ANT_CONFIG_GPIO   14  // GPIO14 -> seleziona antenna

static esp_zb_ep_list_t *on_off_light_ep;
static bool zb_joined = false;
static volatile bool zb_is_joining = false;

/********************* Define functions **************************/
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    if (!zb_joined && !zb_is_joining) {
        zb_is_joining = true;
        ESP_LOGW(TAG, "Device not joined, retrying steering...");
        esp_zb_bdb_start_top_level_commissioning(mode_mask);
    }
}

static void enable_external_antenna(void)
{
    // Imposta GPIO3 LOW per abilitare il controllo dell'RF switch
    gpio_reset_pin(WIFI_ENABLE_GPIO);
    gpio_set_direction(WIFI_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(WIFI_ENABLE_GPIO, 0);

    // Attendi un attimo
    vTaskDelay(pdMS_TO_TICKS(100));

    // Imposta GPIO14 HIGH per usare l’antenna esterna
    gpio_reset_pin(WIFI_ANT_CONFIG_GPIO);
    gpio_set_direction(WIFI_ANT_CONFIG_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(WIFI_ANT_CONFIG_GPIO, 1);

    ESP_LOGI("ANTENNA", "Antenna esterna abilitata (GPIO3=LOW, GPIO14=HIGH)");
}

static void send_basic_cluster_attributes()
{
    ESP_LOGI("ZB", "Setting Manufacturer Name and Model Identifier");

    esp_err_t err;

    err = esp_zb_zcl_set_attribute_val(
        HA_ESP_LIGHT_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        (void *)"rikyru",
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING);
    
    if (err != ESP_OK) {
        ESP_LOGE("ZB", "Failed to set Manufacturer Name, error: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("ZB", "Successfully set Manufacturer Name: rikyru");
    }

    err = esp_zb_zcl_set_attribute_val(
        HA_ESP_LIGHT_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        (void *)"Smart_Thermostat",
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING);
    
    if (err != ESP_OK) {
        ESP_LOGE("ZB", "Failed to set Model Identifier, error: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("ZB", "Successfully set Model Identifier: Smart_Thermostat");
    }
}


static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    bool command_state = 0;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);

    if (message->info.dst_endpoint == HA_ESP_LIGHT_ENDPOINT) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                command_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : command_state;
                ESP_LOGI(TAG, "Command caldaia sets to %s", command_state ? "On" : "Off");
                gpio_set_level(CMD_CALDAIA_GPIO, command_state); // Control caldaia
            }
        }
    }
    else if (message->info.dst_endpoint == HA_ESP_TABLET_ENDPOINT) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                command_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : command_state;
                ESP_LOGI(TAG, "Command tablet sets to %s", command_state ? "On" : "Off");
                gpio_set_level(CMD_TABLET_GPIO, command_state); // Control caldaia
            }
        }
    }
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}


static void steering_retry_task(void *pvParameters)
{
    while (!zb_joined) {
        if (!zb_is_joining) {
            ESP_LOGW(TAG, "Device not joined, retrying steering...");
            zb_is_joining = true;
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));  // ogni 10 secondi
    }
    vTaskDelete(NULL);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            //ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                //send_basic_cluster_attributes(); 
            } else {
                ESP_LOGI(TAG, "Device rebooted");
                //configure_attribute_reporting();
            }
        } else {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        zb_is_joining = false;
        if (err_status == ESP_OK) {
            //send_basic_cluster_attributes();
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            zb_joined = true;
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY: 
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "ZDO config completed successfully");
        } else {
            ESP_LOGW(TAG, "ZDO config failed (status: %s), retrying in 3s", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm(
                (esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                ESP_ZB_BDB_MODE_NETWORK_STEERING,
                3000  // in millisecondi
            );
        }
    break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}







static void esp_zb_task(void *pvParameters)
{
    ESP_LOGI("ZB", "Inizializzazione Zigbee...");

    // Configurazione dello stack Zigbee
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    //esp_zb_set_network_channel(23);

    // Definizione degli attributi
    static uint8_t test_attr = 3;
    static uint8_t test_attr2 = 4;
    static uint8_t manufacturer_name[33] = {6, 'r', 'i', 'k', 'y', 'r', 'u'};
    static uint8_t model_id[33] = {17, 'S', 'm', 'a', 'r', 't', '_', 'T', 'h', 'e', 'r', 'm', 'o','s','t','a','t'};

    // Creazione del Basic Cluster
    esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &test_attr);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &test_attr2);
    esp_zb_cluster_update_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &test_attr);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, &model_id[0]);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, &manufacturer_name[0]);

    // Creazione del Basic Cluster
    esp_zb_attribute_list_t *esp_zb_basic_cluster2 = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster2, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &test_attr);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster2, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &test_attr2);
    esp_zb_cluster_update_attr(esp_zb_basic_cluster2, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &test_attr);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster2, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, &model_id[0]);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster2, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, &manufacturer_name[0]);

    // Creazione del Identify Cluster
    esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
    esp_zb_identify_cluster_add_attr(esp_zb_identify_cluster, ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID, &test_attr);

    // Creazione del Groups Cluster
    esp_zb_attribute_list_t *esp_zb_groups_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_GROUPS);
    esp_zb_groups_cluster_add_attr(esp_zb_groups_cluster, ESP_ZB_ZCL_ATTR_GROUPS_NAME_SUPPORT_ID, &test_attr);

    // Creazione del Scenes Cluster
    esp_zb_attribute_list_t *esp_zb_scenes_cluster = esp_zb_scenes_cluster_create(NULL);
    esp_zb_cluster_update_attr(esp_zb_scenes_cluster, ESP_ZB_ZCL_ATTR_SCENES_NAME_SUPPORT_ID, &test_attr);

    static bool on_off_ep1 = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE;
    static bool on_off_ep2 = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE;

    static esp_zb_on_off_cluster_cfg_t on_off_cfg1 = {
        .on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE,
    };

    static esp_zb_on_off_cluster_cfg_t on_off_cfg2 = {
        .on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *esp_zb_on_off_cluster = esp_zb_on_off_cluster_create(&on_off_cfg1);
    esp_zb_attribute_list_t *on_off_cluster2 = esp_zb_on_off_cluster_create(&on_off_cfg2);



    // Creazione della lista dei cluster
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_groups_cluster(esp_zb_cluster_list, esp_zb_groups_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_scenes_cluster(esp_zb_cluster_list, esp_zb_scenes_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_on_off_cluster(esp_zb_cluster_list, esp_zb_on_off_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    

    // Lista cluster per endpoint 2 (puoi anche mettere solo On/Off se vuoi)
    esp_zb_cluster_list_t *cluster_list_ep2 = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cluster_list_ep2, esp_zb_basic_cluster2, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_on_off_cluster(cluster_list_ep2, on_off_cluster2, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // Creazione della lista degli endpoint
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, HA_ESP_LIGHT_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    esp_zb_ep_list_add_ep(esp_zb_ep_list, cluster_list_ep2, HA_ESP_TABLET_ENDPOINT,
                      ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);

    // Registrazione del dispositivo
    esp_zb_device_register(esp_zb_ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);

    // set type device
    //esp_zb_set_device_type(ESP_ZB_DEVICE_TYPE_ROUTER);
    // Avvio dello stack Zigbee
    ESP_ERROR_CHECK(esp_zb_start(false));
    
    while (true)
    {
        esp_zb_main_loop_iteration();
        vTaskDelay(pdMS_TO_TICKS(10)); // se la funzione già loopa internamente, qui non arrivi mai
    }
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));


    command_driver_init();
    enable_external_antenna();


    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
