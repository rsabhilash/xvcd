O=.

all:
	@echo "Select appropriate target for your platform and make it:"
	@echo "make raspi"
	@echo "make macos"
	@echo "make slc6"

raspi:
	$(MAKE) -C src O=$(O) USE_GETIFADDRS=wlan0

macos:
	$(MAKE) -C src O=$(O) USE_GETIFADDRS=en0

slc6:
	$(MAKE) -C src O=$(O) USE_GETIFADDRS=eth0


