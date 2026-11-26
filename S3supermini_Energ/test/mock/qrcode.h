#pragma once
#define ESP_OK 0
typedef void* esp_qrcode_handle_t;
enum { ESP_QRCODE_ECC_LOW };
struct esp_qrcode_config_t { void (*display_func)(esp_qrcode_handle_t); int max_qrcode_version; int qrcode_ecc_level; };
#define ESP_QRCODE_CONFIG_DEFAULT() esp_qrcode_config_t{nullptr,40,0}
inline int esp_qrcode_get_size(esp_qrcode_handle_t){return 0;} inline bool esp_qrcode_get_module(esp_qrcode_handle_t,int,int){return false;}
inline int esp_qrcode_generate(esp_qrcode_config_t*,const char*){return 0;}
