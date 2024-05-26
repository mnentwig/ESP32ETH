idf_component_register(SRCS "ethernet_example_main.c" "feature_nvsMan.c" "feature_errMan.c" "util.c" "dispatcher.c" "feature_ETH.c" "feature_UART.c" "dispatcherconn_ethernet.c" "dispatcherconn_UART.c"
                       INCLUDE_DIRS "."
		       REQUIRES ethernet_init
		       REQUIRES nvs_flash
		       REQUIRES esp_eth
		       REQUIRES esp_netif
		       REQUIRES vfs
		       PRIV_REQUIRES driver
)
