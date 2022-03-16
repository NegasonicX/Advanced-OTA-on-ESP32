
#include <string.h>
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "enc28j60.h"
#include "esp_eth_enc28j60.h"
#include "driver/spi_master.h"  
#include "driver/gpio.h"

#include "mdns.h"
#include "ping.h"

#include "lwip/err.h"
#include "lwip/sys.h"


#include "mqtt_client.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

/**
 * Brief:
 * This code shows a basic logic to perform inter-task communication using multiple queues
 *
 * GPIO status:
 * GPIO2   : output ( built-in LED on Devkit V1 )
 *
 */

//.............Indication Declarations.....................
#define builtin 2                                                       //++ Built-in LED of Devkit v1

char mac_json[40];                                                      //++ Array to store MAC Address of ESP32
uint8_t base_mac_addr[6] = {0};

//.............Ping Declarations.....................
#define ping_interval_ms 1500                                           //++ Pinging the host in every 1500ms interval
#define ping_priority 1                                                 //++ Setting the priorty for Ping task
#define ping_count 5                                                    //++ Number of times ESP pings the host
#define ping_loss_tolerance 35                                          //++ Setting maximum value for loss % 

char *TARGET_HOST = "www.google.com";                                   //++ Specify the Target Host to be pinged
bool ping_stop_flag = false;                                            //++ Flag to execute entire ping process one in while loop

//.............Wifi Declarations.....................
#define EXAMPLE_WIFI_SSID             "J@RVI$"                          //++ Set up your AP SSID which ESP will connect with
#define EXAMPLE_WIFI_PASS             "16436138"                      //++ Set up your AP PASSWORD which ESP will connect with
#define EXAMPLE_MAXIMUM_RETRY         2                                 //++ Set the number of attempts ESP makes to connect the AP on boot
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;
static const char *TAG_wifi = "Wifi";                                   //++ TAG for Wifi logs

//.............Ethernet Declarations.....................
static const char *TAG_eth = "Ethernet";                                //++ TAG for Ethernet logs
bool ethernet_connection_flag = false;                                  //++ To check whether ESP is connected to Ethernet Cable

//.............MQTT Declarations.....................
#define mqttbroker "mqtt://104.211.188.23:5131"                        //++ MQTT Broker

char data_topic[50];                                                    //++ Name of MQTT TOPIC to publish data from ESP
char cmd_topic[50];                                                     //++ Name of MQTT TOPIC to receive data on ESP
char dev_speci[250];                                                    //++ Array for ESP's MAC Address
int topic_id;

int mqtt_con_flag_1 = 0;                                                //++ Flags to check MQTT CONNECTION
int mqtt_con_flag_2 = 0;                                                

esp_mqtt_client_handle_t client;                                       //++ Client for MQTT BROKER

static const char *TAG_MQTT = "MQTT";                                   //++ TAG for MQTT

//.............OTA Declarations.....................
#define HASH_LEN 32
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");
char Ota_Cmd[20] = "{\"START_OTA\"}";
char Ota_Url[200] = "https://beta.alitersolutions.com/demo/images/OTA/enc28j60.bin";
char string2[200];
int count1 = 12;
int count2 = 0;
int content_len=0;
static const char *TAG_OTA = "OTA";
TaskHandle_t xHandle_reconnection;

//---------------------------------------------------------------------------------------------------------------------------
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)   //++ Handler for Wifi ( executes only once on boot )
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)                                   //++ ESP Wifi has began
    {                                 
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)                        //++ ESP connected to the specificed AP
    {                      
        ESP_LOGI("Connected to the AP","SSID : %s & PASS : %s", EXAMPLE_WIFI_SSID,EXAMPLE_WIFI_PASS);
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)                     //++ ESP couldn't connect to the specificed AP
    {
        if (s_retry_num < EXAMPLE_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG_wifi, "retry to connect to the AP");
        } 
        else 
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);                                    
        }
        ESP_LOGE(TAG_wifi,"connect to the AP fail");                                                  //++ ESP failed to connect to the specified AP
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)                               //++ ESP got the IP from specified AP
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_wifi, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

//---------------------------------------------------------------------------------------------------------------------------
void wifi_init_sta(void)                                                    //++ Wifi Initialising Funtion                                                    
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();

    esp_event_loop_create_default();

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, sta_netif, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, sta_netif, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
           .ssid = EXAMPLE_WIFI_SSID,
           .password = EXAMPLE_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG_wifi, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_wifi, "connected to ap SSID:%s password:%s",
                 EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
    
                // log_wifi = 1;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_wifi, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
                 //log_wifi = 0;
    } else {
        ESP_LOGE(TAG_wifi, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    vEventGroupDelete(s_wifi_event_group);
}

/** Event handler for Ethernet events ( executes continuously ) */
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG_eth, "Ethernet Link Up");
        ESP_LOGI(TAG_eth, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGE(TAG_eth, "Ethernet Link Down");
        ethernet_connection_flag = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG_eth, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG_eth, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG_eth, "Ethernet Got IP Address");
    ESP_LOGI(TAG_eth, "~~~~~~~~~~~");
    ESP_LOGI(TAG_eth, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG_eth, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG_eth, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG_eth, "~~~~~~~~~~~");

    ethernet_connection_flag = true;                                                //++ Set the flag as "true" once ESP get IP from ethernet
    
}

//---------------------------------------------------------------------------------------------------------------------------
void ethernet_init_sta()                                                            //++ Ethernet Initialising Function
{
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
   
   spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_ENC28J60_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_ENC28J60_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ENC28J60_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
   
     ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ENC28J60_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    /* ENC28J60 ethernet driver is based on spi driver */
    spi_device_interface_config_t devcfg = {
        .command_bits = 3,
        .address_bits = 5,
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ENC28J60_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_ENC28J60_CS_GPIO,
        .queue_size = 20,
        .cs_ena_posttrans = enc28j60_cal_spi_cs_hold_time(CONFIG_EXAMPLE_ENC28J60_SPI_CLOCK_MHZ),
    };

   spi_device_handle_t spi_handle = NULL;
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ENC28J60_SPI_HOST, &devcfg, &spi_handle));

     eth_enc28j60_config_t enc28j60_config = ETH_ENC28J60_DEFAULT_CONFIG(spi_handle);
    enc28j60_config.int_gpio_num = CONFIG_EXAMPLE_ENC28J60_INT_GPIO;

      eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.smi_mdc_gpio_num = -1;  // ENC28J60 doesn't have SMI interface
    mac_config.smi_mdio_gpio_num = -1;
    esp_eth_mac_t *mac = esp_eth_mac_new_enc28j60(&enc28j60_config, &mac_config);
   
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 0; // ENC28J60 doesn't support auto-negotiation
    phy_config.reset_gpio_num = -1; // ENC28J60 doesn't have a pin to reset internal PHY
    esp_eth_phy_t *phy = esp_eth_phy_new_enc28j60(&phy_config);

     esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    /* ENC28J60 doesn't burn any factory MAC address, we need to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
    */
       mac->set_addr(mac, (uint8_t[]) {
        base_mac_addr[0], base_mac_addr[1], base_mac_addr[2], base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]
    });
   
   // ENC28J60 Errata #1 check
    if (emac_enc28j60_get_chip_info(mac) < ENC28J60_REV_B5 && CONFIG_EXAMPLE_ENC28J60_SPI_CLOCK_MHZ < 8) {
        ESP_LOGE(TAG_eth, "SPI frequency must be at least 8 MHz for chip revision less than 5");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
   
   
    // Set default handlers to process TCP/IP stuffs
   ESP_ERROR_CHECK(esp_eth_set_default_handlers(eth_netif));
    //-------------------------------------------------------------------------------------------------------

    
    /* attach Ethernet driver to TCP/IP stack */
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));
   
   
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    /* start Ethernet driver state machine */
    esp_eth_start(eth_handle);
    enc28j60_set_phy_duplex(phy, ETH_DUPLEX_FULL);                 //++ Running the enc28j60 ic on FULL DUPLEX mode ( use "ETH_DUPLEX_HALF" to run on half duplex )

}

//---------------------------------------------------------------------------------------------------------------------------
void reconnection()                                                                         //++ Task to check for Interent and Reconnection                                                                            
{
    while(1)
    { 
        wifi_ap_record_t wifidata;
        esp_wifi_sta_get_ap_info(&wifidata);
        int rssi= wifidata.rssi;                                                            //++ Get the RSSI of the AP ESP is connected to

        if((rssi < 0 && rssi > -100) || ethernet_connection_flag == true)                   //++ Perform the ping operations only when sufficient Wifi RSSI is available or Ethernet cable is connected
        {
            ping_stop_flag = false;
            do{                                                                             //++ Loop to perform one complete cycle of ping function
                initialize_ping(ping_interval_ms, ping_priority, TARGET_HOST, ping_count);  //++ Initialize the ping process

                vTaskDelay((ping_interval_ms*ping_count + 500) / portTICK_PERIOD_MS);               //++ Wait for the complete execution to complete and take the response

                int loss = cmd_ping_on_ping_results(2);                                     //++ Extract the loss % from the ping cycle

                if(loss<ping_loss_tolerance)                                                //++ Condition to check whether good internet is available or not
                {
                    gpio_set_level(builtin,1);
                    printf("GOOD INTERNET\n");
                    ping_stop_flag = true;
                }
                else
                {
                    gpio_set_level(builtin,0);
                    printf("BAD INTERNET\n");
                    ping_stop_flag = true;
                }

            }while(ping_stop_flag==false);
        }

        if(rssi == 0 || rssi < -100)                              //++ If RSSI is 0 or beyond -100, ESP is connected to AP and try establishing the connection
        {
            gpio_set_level(builtin,0);
            ESP_LOGI(TAG_wifi, "Lost AP Radio, Trying to Establish Network.....");
            esp_wifi_connect();
        }

        vTaskDelay( 10000 / portTICK_PERIOD_MS);                       //++ Execute complete while loop every 10 seconds
    }
}

/* OTA loops */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
         content_len = esp_http_client_get_content_length(evt->client);
        
    }
    return ESP_OK;
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG_OTA, "Running firmware version: %s", running_app_info.version);
    }

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /**
     * Secure version check from firmware image header prevents subsequent download and flash write of
     * entire firmware image. However this is optional because it is also taken care in API
     * esp_https_ota_finish at the end of OTA update procedure.
     */
    const uint32_t hw_sec_version = esp_efuse_read_secure_version();
    if (new_app_info->secure_version < hw_sec_version) {
        ESP_LOGW(TAG_OTA, "New firmware security version is less than eFuse programmed, %d < %d", new_app_info->secure_version, hw_sec_version);
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}



static esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    return err;
}

// void simple_ota_example_task()
void ota_task()
// void advanced_ota_example_task(void *pvParameter)
{
    vTaskDelete(xHandle_reconnection);
    
    ESP_LOGI(TAG_OTA, "Starting OTA example");
    esp_err_t ota_finish_err = ESP_OK;
    esp_http_client_config_t config = {
        .url=Ota_Url,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler,
        .timeout_ms =5000,
        .keep_alive_enable = true,
    };
#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

        esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = _http_client_init_cb, // Register a callback to be invoked after esp_http_client is initialized
#ifdef CONFIG_EXAMPLE_ENABLE_PARTIAL_HTTP_DOWNLOAD
        .partial_http_download = true,
        .max_http_request_size = CONFIG_EXAMPLE_HTTP_REQUEST_SIZE,
#endif
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    // printf("Total size  %d \n",esp_https_ota_get_image_size(https_ota_handle));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_OTA, "ESP HTTPS OTA Begin failed");
 goto ota_end;
    }
// printf("Total size  %d \n",content_len);
    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_OTA, "esp_https_ota_read_img_desc failed");
        goto ota_end;
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_OTA, "image header verification failed");
        goto ota_end;
    }
    int old_percentage=0;
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        int current_len=esp_https_ota_get_image_len_read(https_ota_handle);
        int current_percentage =current_len*100/content_len;
        if(current_percentage!=old_percentage)
        { 
            old_percentage=current_percentage;
            ESP_LOGW("OTA"," Downloading...( %d %%) \n",old_percentage);
        }
        
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG_OTA, "Complete data was not received.");
         goto ota_end;
    } else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG_OTA, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        } else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG_OTA, "Image validation failed, image is corrupted");
                 goto ota_end;
             
            }
            ESP_LOGE(TAG_OTA, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
             goto ota_end;

        }
    }

ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG_OTA, "ESP_HTTPS_OTA upgrade failed");
             for (int i = 3; i > 0; i--) 
            {
                ESP_LOGE("OTA FAILED","Restarting in %d seconds...\n", i);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
            ESP_LOGI("OTA FAILED","Restarting now.\n");
            fflush(stdout);
           esp_restart();
    
        vTaskDelete(NULL);

}

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i)
    {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG_OTA, "%s %s", label, hash_print);
}
static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = {0};
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address = ESP_BOOTLOADER_OFFSET;
    partition.size = ESP_PARTITION_TABLE_OFFSET;
    partition.type = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

void cmd_topic_ISR(char *Server_Cmd)
{
    ESP_LOGI(TAG_MQTT, " cmd topic ISR JSON: %s\n", Server_Cmd);

    if (Server_Cmd[2] == 'O' && Server_Cmd[3] == 'T' && Server_Cmd[4] == 'A')
    {
        memset(Ota_Url, 0, strlen(Ota_Url));
        memset(string2, 0, strlen(string2));
        while (Server_Cmd[count1] != '\"')
        {
            string2[count2] = Server_Cmd[count1];
            count1++;
            count2++;
        }
        count1 = 12;
        count2 = 0;
        strncpy(Ota_Url, string2, sizeof(Ota_Url));

        topic_id = esp_mqtt_client_publish(client, data_topic, string2, 0, 1, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, on topic %s", data_topic);
        ESP_LOGI(TAG_MQTT, "data publish = %s", string2);

    }
    else if (strncmp(Server_Cmd, Ota_Cmd, 5) == 0)
    {

        topic_id = esp_mqtt_client_publish(client, data_topic, "OTA INTILIZATION", 0, 1, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, on topic %s", data_topic);
        ESP_LOGI(TAG_MQTT, "data publish = %s", "OTA INTILIZATION");

        ota_task();
    }

    else
    {
        ESP_LOGI(TAG_MQTT, "Wrong command \n");
    }
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)                          //++ FIRST MQTT HANDLER
{
    int msg_id;
    switch (event->event_id)
    {
        case MQTT_EVENT_CONNECTED:
        
            mqtt_con_flag_1 = 1;
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");

            msg_id = esp_mqtt_client_publish(client, data_topic, dev_speci, 0, 1, 0);
            ESP_LOGI(TAG_MQTT, "sent publish successful, on topic %s", data_topic);
            msg_id = esp_mqtt_client_subscribe(client, cmd_topic, 1);
            ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);
            break;

        case MQTT_EVENT_DISCONNECTED:

            mqtt_con_flag_1 = 0;
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");           
            break;

        case MQTT_EVENT_SUBSCRIBED:
            break;

        case MQTT_EVENT_UNSUBSCRIBED:

            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            break;

        case MQTT_EVENT_DATA:

            ESP_LOGI(TAG_MQTT, "DATA RECEIVED = %.*s\r\n", event->data_len, event->data);
            ESP_LOGI(TAG_MQTT, "TOPIC USED = %.*s\r\n", event->topic_len, event->topic);

            char Server_Cmd[300];
            memset(Server_Cmd, 0, strlen(Server_Cmd));
            strncpy(Server_Cmd, event->data, event->data_len);
            sprintf(Server_Cmd, "%.*s", event->data_len, event->data);
            cmd_topic_ISR(Server_Cmd);
            break;
        case MQTT_EVENT_ERROR:

            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
            break;

        default:
            ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
            break;
    }

    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)          //++ CREATE HANDLE
{
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}


static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg1 = {                                                          //++ PASSING MQTT BROKER
        .uri = mqttbroker,
        .client_id = "mqtt_client",
    };
    printf("MQTT: %s \n", mqtt_cfg1.uri);

    client = esp_mqtt_client_init(&mqtt_cfg1);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

}


//---------------------------------------------------------------------------------------------------------------------------
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();                                                   //++ Initialize the NVS of ESP
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_install_isr_service(0);
    
    esp_efuse_mac_get_default(base_mac_addr);;
    sprintf(mac_json,"%x:%x:%x:%x:%x:%x", base_mac_addr[0], base_mac_addr[1], base_mac_addr[2], base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);
    printf("MAC Address for the device = %s \n",mac_json);                              //++ Get the MAC Address of the ESP

    wifi_init_sta();                                                                    //++ Call the Wifi Initializing Function
    ethernet_init_sta();                                                             //++ Call the Ethernet Initializing Function

    // MQTT TOPICS
    sprintf(data_topic, "%s/data", mac_json);                                           //++ Making TOPICS for MQTT
    sprintf(cmd_topic, "%s/cmd", mac_json);
    sprintf(dev_speci, "{\"device\":\"%s\",\"Connection Established!\"}", mac_json);

    // MQTT START
    mqtt_app_start();
     
    gpio_set_direction(builtin, GPIO_MODE_OUTPUT);                                      //++ Set the built-in LED direction as OUTPUT
    gpio_set_level(builtin,0);

    xTaskCreate(reconnection, "reconnection", 1024*4, NULL, configMAX_PRIORITIES-1, &xHandle_reconnection);      //++ Create the Task to check reconnection and interent

}

