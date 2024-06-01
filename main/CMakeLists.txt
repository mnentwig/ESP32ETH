idf_component_register(SRCS "main.c" 
                       INCLUDE_DIRS "."
		       REQUIRES universal_REPL
		       REQUIRES UREPL_PWM
		       REQUIRES ethernet_init
		       PRIV_REQUIRES driver
)
