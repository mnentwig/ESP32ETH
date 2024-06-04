idf_component_register(SRCS "UREPL_PWM.c"
		       PRIV_REQUIRES universal_REPL
		       PRIV_REQUIRES driver
#    	   	   PRIV_REQUIRES "esp_timer"
               INCLUDE_DIRS ".")
