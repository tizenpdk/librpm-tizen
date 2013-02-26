PKG_NAME := librpm-tizen
SPECFILE = $(addsuffix .spec, $(PKG_NAME))
PKG_VERSION := $(shell grep '^Version: ' $(SPECFILE)|awk '{print $$2}')

ORIG_NAME = rpm
TARBALL := $(ORIG_NAME)-$(PKG_VERSION).tar.gz

dsc: $(TARBALL)
	@echo "Updating $(PKG_NAME).dsc"
	@sed  -i 's/\s*Debtransform-Tar.*/Debtransform-Tar: $(TARBALL)/' $(PKG_NAME).dsc

$(TARBALL):
	@echo "Creating $(TARBALL)"
	@cd `git rev-parse --show-toplevel` \
		&& git archive --prefix $(ORIG_NAME)-$(PKG_VERSION)/ HEAD \
		| gzip > $(CURDIR)/$(TARBALL)

series:
	@echo "Creating debian.series file"
	@find . -maxdepth 1 -name '0*patch' -print0  | xargs -0 -I{} basename {} | sort > debian.series

clean:
	@rm -f $(ORIG_NAME)*.tar.gz

all: clean dsc series
