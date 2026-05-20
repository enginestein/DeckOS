BUILD_DIR=build
PROJECT=DeckOS
PICO_DRIVE=/media/indresh/RPI-RP2
SESSION=deckos
LOG_FILE=/tmp/deckos.log

all:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ..
	cd $(BUILD_DIR) && make -j4

flash: all
	cp $(BUILD_DIR)/$(PROJECT).uf2 $(PICO_DRIVE)/

monitor:
	@echo "[monitor] connecting to DeckOS (Ctrl+A Ctrl+X to quit picocom)"
	@if tmux has-session -t $(SESSION) 2>/dev/null; then \
		echo "[monitor] session exists, attaching..."; \
		tmux attach-session -t $(SESSION); \
	else \
		tmux new-session -s $(SESSION) \
			"while true; do \
				picocom /dev/ttyACM0 -b 115200 --logfile $(LOG_FILE) 2>/dev/null || true; \
				echo '[monitor] disconnected, retrying in 2s...'; \
				sleep 2; \
			done"; \
	fi

monitor-attach:
	@echo "[monitor-attach] joining existing session..."
	@tmux attach-session -t $(SESSION) || echo "No session found — run 'make monitor' first"

monitor-ro:
	@echo "[monitor-ro] read-only output tail (Ctrl+C to quit)"
	@touch $(LOG_FILE)
	@tail -f $(LOG_FILE)

monitor-kill:
	@tmux kill-session -t $(SESSION) 2>/dev/null && echo "session killed" || echo "no session running"

run: flash
	sleep 2
	$(MAKE) monitor

clean:
	rm -rf $(BUILD_DIR)