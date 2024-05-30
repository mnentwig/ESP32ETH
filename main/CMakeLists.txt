idf_component_register(SRCS "ethernet_example_main.c" 
                       INCLUDE_DIRS "."
		       REQUIRES universal_REPL
		       REQUIRES ethernet_init
		       PRIV_REQUIRES driver
)
