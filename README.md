# Thesis ESP-NOW Link

ESP32 firmware that turns two ESP32 boards into a wireless serial bridge using the **ESP-NOW** protocol, so MATLAB Simulink can stream a signal out through one board and read it back from the other.

This is part of an undergraduate BSECE thesis at Notre Dame of Marbel University: a wireless laboratory trainer for modulation techniques built on ESP-NOW and ESP32 microcontrollers.

## How it works

```
Simulink ──USB serial──► ESP32 (TX) ──ESP-NOW──► ESP32 (RX) ──USB serial──► Simulink
```

Both boards run the **same sketch**. At startup you choose whether each board is the transmitter (`T`) or the receiver (`R`):

1. **RX board** prints its own MAC address and waits for a test ping.
2. **TX board** asks for the RX board's MAC address, registers it as a peer, and sends `PING`.
3. RX replies with `PONG`, and both boards switch to streaming.
4. TX forwards whatever bytes arrive from Simulink over serial (up to 250 bytes per ESP-NOW packet, the protocol limit). RX writes every packet it receives straight back out over serial to Simulink.

The firmware is a state machine (`STATE_START → STATE_TX_* / STATE_RX_*`) that follows the thesis flowchart.

## Repository contents

| Folder | Description |
|---|---|
| `ESP32_TransmitReceive/` | Original version. Choose T/R and do the handshake on every power-up. |
| `ESP32_TransmitReceive_v2/` | Recommended version. Adds saved configuration and a faster receive path (see below). |

### What v2 adds

- **Saved configuration:** after a successful handshake, the mode (T/R) and peer MAC are stored in flash (NVS namespace `trainer_cfg`). On the next boot the board goes straight to streaming with no setup.
- **Reset option:** type `CLEAR` in the serial monitor within 3 seconds of boot to erase the saved configuration and start over.
- **Faster receive callback:** streaming packets are handled first, without String allocation, so the handshake checks don't run on every packet.
- **Larger serial RX buffer** (1024 bytes) to reduce dropped bytes from Simulink.

## Requirements

- 2 × ESP32 development boards
- Arduino IDE (or Arduino CLI) with the **ESP32 board package, Core 3.x**. The ESP-NOW callbacks use the Core 3.x signatures (`wifi_tx_info_t`, `esp_now_recv_info`) and will not compile on Core 2.x.
- MATLAB Simulink with serial send/receive blocks, for the signal-streaming side

## Getting started

1. Open `ESP32_TransmitReceive_v2/ESP32_TransmitReceive_v2.ino` in the Arduino IDE.
2. Flash it to **both** ESP32 boards.
3. Open a serial monitor at **115200 baud** for each board.
4. On the receiver, enter `R` and copy the MAC address it prints.
5. On the transmitter, enter `T`, then paste the receiver's MAC in the form `AA:BB:CC:DD:EE:FF`.
6. Wait for `Connection successful.` on both boards.
7. Close the serial monitors and connect each board's COM port to your Simulink model: the TX port to the serial send block, the RX port to the serial receive block.

With v2, steps 4–6 only need to be done once. After that, both boards reconnect automatically on power-up.

## Notes

- Close the Arduino serial monitor before running Simulink; only one program can hold a COM port at a time.
- Both boards stay in WiFi station mode on channel 0, with encryption off.
- Anything Simulink sends to the TX board is forwarded as raw bytes, so the Simulink transmit and receive blocks must agree on data type and frame size.

## Author

Pink Note (Legitpotao), BSECE, Notre Dame of Marbel University
