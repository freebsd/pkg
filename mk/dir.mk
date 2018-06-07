all:
	@for d in $(DIRS); do \
		$(MAKE) -C $$d ; \
	done

install:
	@for d in $(DIRS); do \
		$(MAKE) -C $$d install ; \
	done

clean:
	@for d in $(DIRS); do \
		$(MAKE) -C $$d clean ; \
	done
