objs-m := host.o 2a0x.o
ifneq ($(filter-out 0,$(CONFIG_TEST)),)
	objs-m += test_main.o
endif
