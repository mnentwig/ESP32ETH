idf_component_register(SRCS "ethernet_example_main.c" "nvsMan.c" "errMan.c" "util.c"
                       INCLUDE_DIRS "."
		       REQUIRES ethernet_init
		       REQUIRES nvs_flash
		       REQUIRES esp_eth
		       REQUIRES esp_netif
		       REQUIRES vfs
		       PRIV_REQUIRES driver
)
