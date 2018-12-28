all:
	@set -e ; for d in $(DIRS); do \
		$(MAKE) -C $$d ; \
	done

install:
	@set -e ; for d in $(DIRS); do \
		$(MAKE) -C $$d install ; \
	done

clean:
	@set -e; for d in $(DIRS); do \
		$(MAKE) -C $$d clean ; \
	done
