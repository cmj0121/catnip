SUBDIR :=

.PHONY: all clean build ci flash backup install monitor uninstall upgrade help $(SUBDIR)

all: $(SUBDIR) 		# default action
	@[ -f .git/hooks/pre-commit ] || pre-commit install --install-hooks
	@git config commit.template .git-commit-template

clean: $(SUBDIR)	# clean-up environment
	@find . -name '*.sw[po]' -delete
	@$(MAKE) -C firmware clean

build:				# compile the framework for the host only
	@$(MAKE) -C firmware build

ci:					# every check there is - run this before touching the device
	@$(MAKE) -C firmware build
	@$(MAKE) -C firmware test
	@shellcheck scripts/*.sh
	@command -v pio >/dev/null 2>&1 \
		&& pio run -d firmware -e meowkit \
		|| echo "note: PlatformIO not installed, skipping the device build"

flash:				# put the MeowKit into flash/download status (first-time setup)
	@scripts/meowkit.sh flash

backup:				# back up the MeowKit's full flash (do this before installing)
	@scripts/meowkit.sh backup

install: ci			# check, back up stock, flash Catnip, and verify it started
	@scripts/meowkit.sh install

monitor:			# watch the serial log
	@scripts/meowkit.sh monitor

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
