idf_component_register(SRCS "main.c" 
                       INCLUDE_DIRS "."
		       REQUIRES universal_REPL
		       REQUIRES UREPL_ADC
		       REQUIRES UREPL_PWM
		       PRIV_REQUIRES UREPL_camera
		       REQUIRES ethernet_init
		       PRIV_REQUIRES driver
		       PRIV_REQUIRES esp_psram
)
