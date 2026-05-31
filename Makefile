BUILD_DIR  = build
PROJECT    = DeckOS
PICO_DRIVE = /media/indresh/RPI-RP2
LOG_FILE   = /tmp/deckos.log

all:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ..
	cd $(BUILD_DIR) && make -j4

flash: all
	cp $(BUILD_DIR)/$(PROJECT).uf2 $(PICO_DRIVE)/

monitor:
	@echo "[monitor] connecting to DeckOS -- Ctrl+T Q to quit"
	@echo "[monitor] tio will auto-reconnect if the board reboots"
	tio -l $(LOG_FILE) /dev/ttyACM0

monitor-ro:
	@echo "[monitor-ro] read-only log tail -- Ctrl+C to quit"
	@touch $(LOG_FILE)
	tail -f $(LOG_FILE)

run: flash
	@sleep 2
	$(MAKE) monitor

clean:
	rm -rf $(BUILD_DIR)