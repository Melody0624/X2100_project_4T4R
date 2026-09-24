RADAR_SDK := firmware/x2100/freertos
RADAR_DEFCONFIG := x2100_Cheetah_RadarEye_nor_defconfig
JOBS ?= 2

.PHONY: radar-config radar-build radar radar-clean radar-rebuild

radar-config:
	$(MAKE) -C $(RADAR_SDK) $(RADAR_DEFCONFIG)

radar-build:
	$(MAKE) -C $(RADAR_SDK) -j$(JOBS)

radar:
	$(MAKE) -C $(RADAR_SDK) $(RADAR_DEFCONFIG)
	$(MAKE) -C $(RADAR_SDK) -j$(JOBS)

radar-clean:
	$(MAKE) -C $(RADAR_SDK) clean

radar-rebuild:
	$(MAKE) -C $(RADAR_SDK) clean
	$(MAKE) -C $(RADAR_SDK) $(RADAR_DEFCONFIG)
	$(MAKE) -C $(RADAR_SDK) -j$(JOBS)
