ifneq (,$(findstring olsr_nhdp,$(USEMODULE)))
	DIRS += src/nhdp
endif
ifneq (,$(findstring olsr_ff_ext,$(USEMODULE)))
	DIRS += src-plugins/ff_ext
endif
ifneq (,$(findstring olsr_hysteresis_olsrv1,$(USEMODULE)))
	DIRS += src-plugins/hysteresis_olsrv1
endif
ifneq (,$(findstring olsr_nhdpcheck,$(USEMODULE)))
	DIRS += src-plugins/nhdpcheck
endif

all:
	mkdir -p $(BINDIR)
	@for i in $(DIRS) ; do $(MAKE) -C $$i ; done ;
	
clean:
	@for i in $(DIRS) ; do $(MAKE) -C $$i clean ; done ;
	@if [ -d $(BINDIR) ] ; \
	then rmdir --ignore-fail-on-non-empty $(BINDIR) ; \
	fi
