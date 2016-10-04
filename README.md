This is a beta release of the SPI service.  It is expected that this code will
be integrated into Legato in a future release.

To use this service you need to do the following:

1. Get a copy of the mangOH source code (see README.md in the mangOH/manifest repository for more information)
1. In `mangOH/targetDefs.mangoh`, add following line: `MKSYS_FLAGS += -s $(MANGOH_ROOT)/apps/SpiService`
1. In `mangOH/mangoh.sdef` add an app entry for the service: `$MANGOH_ROOT/apps/SpiService/spiService.adef`
