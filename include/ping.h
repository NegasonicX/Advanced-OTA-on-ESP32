#ifndef __ping_h__
#define __ping_h__

#include "ping/ping_sock.h"

void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args);       //++ Pings the desired Host Server
void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args);       //++ Returns when ping timeout
void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void *args);           //++ End the ping and cleans the session
int cmd_ping_on_ping_results(int select);                               //++ Outputs the desired ping results
esp_err_t initialize_ping(uint32_t interval_ms, uint32_t task_prio, char * target_host, int ping_count);        //++ Initializes the ping

#endif /* __ping_h__ */
