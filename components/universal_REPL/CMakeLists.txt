idf_component_register(SRCS "feature_nvsMan.c" "feature_errMan.c" "dispatcher.c" "feature_ETH.c" "feature_UART.c" "dispatcherconn_ethernet.c" "dispatcherconn_UART.c"
                       PRIV_REQUIRES driver esp_eth
       		       REQUIRES nvs_flash
		       REQUIRES esp_eth
		       REQUIRES esp_netif
		       REQUIRES vfs
		       PRIV_REQUIRES driver

                       INCLUDE_DIRS ".")
