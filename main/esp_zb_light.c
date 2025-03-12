#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zb_light.h"
#include "light_driver.h"
#include "esp_check.h"
#include <string.h>  // ✅ Inclusione necessaria per memcpy()

#define TAG "ESP_ZB_LIGHT"
#define BUTTON_GPIO 9
#define BLUE_LED_GPIO 7
#define RGB_LED_GPIO 20
#define RGB_LED_ENABLE_GPIO 19

static esp_zb_ep_list_t *on_off_light_ep;

/********************* Define functions **************************/
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

static void configure_gpio()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BLUE_LED_GPIO) | (1ULL << RGB_LED_ENABLE_GPIO) | (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Configurazione per il pulsante
    io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_set_level(BLUE_LED_GPIO, 0);
    gpio_set_level(RGB_LED_ENABLE_GPIO, 1); // Enable RGB LED
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    bool light_state = 0;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);

    if (message->info.dst_endpoint == HA_ESP_LIGHT_ENDPOINT) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                light_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : light_state;
                ESP_LOGI(TAG, "Light sets to %s", light_state ? "On" : "Off");
                gpio_set_level(BLUE_LED_GPIO, light_state); // Control blue LED
                light_driver_set_power(light_state);
            }
        }
    }
    return ret;
}

static void update_rgb_led_state(esp_zb_app_signal_type_t sig_type)
{
    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
            light_driver_set_rgb(255, 0, 0); // Red: Not paired
            break;
        case ESP_ZB_BDB_SIGNAL_STEERING:
            light_driver_set_rgb(0, 0, 255); // Blue: Pairing in progress
            break;
        case ESP_ZB_ZDO_SIGNAL_DEVICE_AUTHORIZED:
            light_driver_set_rgb(0, 255, 0); // Green: Paired
            break;
        default:
            ESP_LOGI(TAG, "Unhandled signal type: %d", sig_type);
            break;
    }
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
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            update_rgb_led_state(ESP_ZB_ZDO_SIGNAL_DEVICE_AUTHORIZED);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
        case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
            ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled Zigbee action: %d", callback_id);
            break;
    }
    return ret;
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
        (void *)"ESP32-C6_test",
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING);
    
    if (err != ESP_OK) {
        ESP_LOGE("ZB", "Failed to set Model Identifier, error: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("ZB", "Successfully set Model Identifier: ESP32-C6_test");
    }
}


static void button_task(void *pvParameters)
{
    while (1) {
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            ESP_LOGI(TAG, "Button pressed. Starting network steering...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            send_basic_cluster_attributes();
            vTaskDelay(pdMS_TO_TICKS(1000)); // Debounce delay
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


static void esp_zb_task(void *pvParameters)
{
    ESP_LOGI("ZB", "Inizializzazione Zigbee...");

    // Configurazione dello stack Zigbee
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    // Creazione dell'endpoint per la luce On/Off
    esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
    on_off_light_ep = esp_zb_on_off_light_ep_create(HA_ESP_LIGHT_ENDPOINT, &light_cfg);

    // Creazione della lista dei cluster
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    if (!cluster_list) {
        ESP_LOGE("ZB", "Errore: impossibile creare la lista cluster!");
        return;
    }

    // Creazione del Basic Cluster
    esp_zb_basic_cluster_cfg_t basic_cfg = {};
    esp_zb_attribute_list_t *basic_cluster_attr_list = esp_zb_basic_cluster_create(&basic_cfg);
    if (!basic_cluster_attr_list) {
        ESP_LOGE("ZB", "Errore: impossibile creare la lista attributi per il Basic Cluster!");
        return;
    }

    // Definizione degli attributi
    static uint8_t manufacturer_name[33] = {6, 'r', 'i', 'k', 'y', 'r', 'u'};
    static uint8_t model_id[33] = {12, 'E', 'S', 'P', '3', '2', '-', 'C', '6', '_', 'T', 'e', 's', 't'};

    // Imposta la lunghezza corretta delle stringhe Zigbee
    manufacturer_name[0] = strlen((char *)&manufacturer_name[1]);
    model_id[0] = strlen((char *)&model_id[1]);

    // Aggiunta degli attributi al Basic Cluster
    esp_err_t err;

    err = esp_zb_basic_cluster_add_attr(
        basic_cluster_attr_list,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        (void *) manufacturer_name);
    if (err != ESP_OK) {
        ESP_LOGE("ZB", "Errore nell'aggiunta del Manufacturer Name: %d", err);
        return;
    }

    err = esp_zb_basic_cluster_add_attr(
        basic_cluster_attr_list,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        (void *) model_id);
    if (err != ESP_OK) {
        ESP_LOGE("ZB", "Errore nell'aggiunta del Model Identifier: %d", err);
        return;
    }

    // Aggiunta del Basic Cluster alla lista dei cluster
    err = esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE("ZB", "Errore nell'aggiunta del Basic Cluster: %d", err);
        return;
    }

    // Registrazione dell'endpoint con la lista dei cluster
    err = esp_zb_ep_list_add_ep(on_off_light_ep, cluster_list, HA_ESP_LIGHT_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID);
    if (err != ESP_OK) {
        ESP_LOGE("ZB", "Errore nella registrazione dell'endpoint: %d", err);
        return;
    }

    // Registrazione del dispositivo
    ESP_ERROR_CHECK(esp_zb_device_register(on_off_light_ep));

    // Aspettiamo prima di recuperare gli attributi
    vTaskDelay(pdMS_TO_TICKS(500));

    // Debug: Verifica se gli attributi sono stati registrati
    ESP_LOGI("ZB", "Verifica attributi nel Basic Cluster...");
    esp_zb_zcl_attr_t *attr;

    attr = esp_zb_zcl_get_attribute(HA_ESP_LIGHT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID);
    if (attr) {
        ESP_LOGI("ZB", "Attributo Manufacturer Name trovato! Valore: %s", (char *)&attr->data_p[1]);
    } else {
        ESP_LOGE("ZB", "Errore: impossibile trovare Manufacturer Name!");
    }

    attr = esp_zb_zcl_get_attribute(HA_ESP_LIGHT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID);
    if (attr) {
        ESP_LOGI("ZB", "Attributo Model Identifier trovato! Valore: %s", (char *)&attr->data_p[1]);
    } else {
        ESP_LOGE("ZB", "Errore: impossibile trovare Model Identifier!");
    }

    // Avvio dello stack Zigbee
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}







void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    configure_gpio();
    light_driver_init(LIGHT_DEFAULT_OFF);

    light_driver_set_rgb(255, 0, 0); // Initialize RGB LED to red

    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL); // Add button task
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
