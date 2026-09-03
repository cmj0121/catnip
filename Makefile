SUBDIR :=

.PHONY: all clean test run build upgrade help flash backup install uninstall $(SUBDIR)

all: $(SUBDIR) 		# default action
	@[ -f .git/hooks/pre-commit ] || pre-commit install --install-hooks
	@git config commit.template .git-commit-template

clean: $(SUBDIR)	# clean-up environment
	@find . -name '*.sw[po]' -delete
	@$(MAKE) -C firmware clean

test:				# run the firmware host tests
	@$(MAKE) -C firmware test

run:				# run in the local environment

build:				# build the firmware (host compile)
	@$(MAKE) -C firmware build

flash:				# put the MeowKit into flash/download status (first-time setup)
	@scripts/meowkit.sh flash

backup:				# back up the MeowKit's full flash (do this before installing)
	@scripts/meowkit.sh backup

install:			# back up stock, then build & flash Catnip onto the MeowKit
	@scripts/meowkit.sh install

uninstall:			# restore the MeowKit to stock (from your backup, or official)
	@scripts/meowkit.sh uninstall

upgrade:			# upgrade all the necessary packages
	pre-commit autoupdate

help:				# show this message
	@printf "Usage: make [OPTION]\n"
	@printf "\n"
	@perl -nle 'print $$& if m{^[\w-]+:.*?#.*$$}' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?#"} {printf "    %-18s %s\n", $$1, $$2}'

$(SUBDIR):
	$(MAKE) -C $@ $(MAKECMDGOALS)
