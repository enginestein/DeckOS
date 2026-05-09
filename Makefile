BUILD_DIR=build
PROJECT=DeckOS
PICO_DRIVE=/media/indresh/RPI-RP2

all:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ..
	cd $(BUILD_DIR) && make -j4

flash: all
	cp $(BUILD_DIR)/$(PROJECT).uf2 $(PICO_DRIVE)/

monitor:
	@echo "[monitor] connecting... (Ctrl+A Ctrl+X to quit)"
	@while true; do \
		picocom /dev/ttyACM0 -b 115200 2>/dev/null || true; \
		echo "[monitor] device disconnected, waiting 2s..."; \
		sleep 2; \
	done

run: flash
	sleep 2
	$(MAKE) monitor

clean:
	rm -rf $(BUILD_DIR)