/*
STANDARD PING LIRARBY TO PERFORM PING OPERATIONS FROM EPS32
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "tcpip_adapter_types.h"
#include "tcpip_adapter.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_netif_defaults.h"
#include "esp_eth_netif_glue.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "ping/ping_sock.h"

static const char *TAG = "PING";
uint32_t transmitted;
uint32_t received;
uint32_t total_time_ms;
uint32_t loss;

void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args)
{
	uint8_t ttl;
	uint16_t seqno;
	uint32_t elapsed_time, recv_len;
	ip_addr_t target_addr;
	esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
	esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
	esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));

#if 1
	// ESP_LOGI(TAG, "%d bytes from %s icmp_seq=%d ttl=%d time=%d ms", recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);	//++ Uncomment this line if want to know ping responses
#else
	wifi_ap_record_t wifidata;
	esp_wifi_sta_get_ap_info(&wifidata);
	ESP_LOGI(TAG, "%d bytes from %s icmp_seq=%d ttl=%d time=%d ms RSSI=%d",
			 recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time, wifidata.rssi);
#endif
}

void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
	uint16_t seqno;
	ip_addr_t target_addr;
	esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	// ESP_LOGW(TAG, "From %s icmp_seq=%d timeout", inet_ntoa(target_addr.u_addr.ip4), seqno);		//++ Uncomment this line if want to know when ping timeout happens
}

void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void *args)
{
	ip_addr_t target_addr;
	esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
	esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
	loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
	if (IP_IS_V4(&target_addr)) 
	{
		ESP_LOGI(TAG, "\n--- %s ping statistics ---", inet_ntoa(*ip_2_ip4(&target_addr)));
	} 
	else 
	{
		ESP_LOGI(TAG, "\n--- %s ping statistics ---", inet6_ntoa(*ip_2_ip6(&target_addr)));
	}
	ESP_LOGI(TAG, "%d packets transmitted, %d received, %d%% packet loss, time %dms", transmitted, received, loss, total_time_ms);
	// delete the ping sessions, so that we clean up all resources and can create a new ping session
	// we don't have to call delete function in the callback, instead we can call delete function from other tasks
	esp_ping_delete_session(hdl);
}

int cmd_ping_on_ping_results(int select)		//++ pass value 0 to 3 to get the desired result of ping
{
	switch(select)
	{
		case 0:
			return transmitted;					//++ Number of packets transmitted
		case 1:
			return received;					//++ Number of packets received
		case 2:
			return loss;						//++ % of loss 
		case 3:
			return total_time_ms;				//++ Total time in milliseconds 
		default:
			return -1;
	}
}

/*
Initializes the ping routine
interval_ms : Intervals in milliseconds to ping the host
task_prio 	: Sets the priority of ping task
target_host : Specifies the ping target host 
ping_count 	: Number of the times ping should be done in each cycle
*/
esp_err_t initialize_ping(uint32_t interval_ms, uint32_t task_prio, char * target_host, int ping_count)
{
	esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();

	if (strlen(target_host) > 0) 
	{
		/* convert URL to IP address */
		ip_addr_t target_addr;
		memset(&target_addr, 0, sizeof(target_addr));
		struct addrinfo hint;
		memset(&hint, 0, sizeof(hint));
		struct addrinfo *res = NULL;
		//int err = getaddrinfo("www.espressif.com", NULL, &hint, &res);
		int err = getaddrinfo(target_host, NULL, &hint, &res);
		if(err != 0 || res == NULL) 
		{
			loss = 100;
			ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
			return ESP_FAIL;
		} 
		else 
		{
			ESP_LOGI(TAG, "DNS lookup success");
		}

		if (res->ai_family == AF_INET) 
		{
			struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
			inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
		} 
		else 
		{
			struct in6_addr addr6 = ((struct sockaddr_in6 *) (res->ai_addr))->sin6_addr;
			inet6_addr_to_ip6addr(ip_2_ip6(&target_addr), &addr6);
		}
		freeaddrinfo(res);
		// ESP_LOGI(TAG, "target_addr.type=%d", target_addr.type);
		// ESP_LOGI(TAG, "target_addr=%s", ip4addr_ntoa(&(target_addr.u_addr.ip4)));
		ping_config.target_addr = target_addr;			// target IP address
	} 
	
	else 
	{
		// ping target is my gateway
		tcpip_adapter_ip_info_t ip_info;
		ESP_ERROR_CHECK(esp_netif_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
		ESP_LOGI(TAG, "IP Address: %s", ip4addr_ntoa(&ip_info.ip));
		ESP_LOGI(TAG, "Subnet mask: %s", ip4addr_ntoa(&ip_info.netmask));
		ESP_LOGI(TAG, "Gateway:	%s", ip4addr_ntoa(&ip_info.gw));
		ip_addr_t gateway_addr;
		gateway_addr.type = 0;
		gateway_addr.u_addr.ip4 = ip_info.gw;
		ESP_LOGI(TAG, "gateway_addr.type=%d", gateway_addr.type);
		ESP_LOGI(TAG, "gateway_addr=%s", ip4addr_ntoa(&(gateway_addr.u_addr.ip4)));
		ping_config.target_addr = gateway_addr;			// gateway IP address
	}

	if(ping_count == -1)
	{
		ping_config.count = ESP_PING_COUNT_INFINITE;   //++ ping in infinite mode, esp_ping_stop can stop it
	}
	else
	{
		ping_config.count = ping_count;				   //++ Pings "ping_count" number of times
	}
	ping_config.interval_ms = interval_ms;
	ping_config.task_prio = task_prio;

	/* set callback functions */
	esp_ping_callbacks_t cbs = {
		.on_ping_success = cmd_ping_on_ping_success,
		.on_ping_timeout = cmd_ping_on_ping_timeout,
		.on_ping_end = cmd_ping_on_ping_end,
		.cb_args = NULL
	};
	esp_ping_handle_t ping;
	esp_ping_new_session(&ping_config, &cbs, &ping);
	esp_ping_start(ping);
	ESP_LOGI(TAG, "ping start");
	return ESP_OK;
}

