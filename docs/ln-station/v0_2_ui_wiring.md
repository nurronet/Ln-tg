# LN Station v0.2 UI Wiring

This release makes the LN station visible in the compiled Tg app.

## Apply

From repo root:

```bash
unzip ln_tg_v0_2_ui_wiring_overlay.zip -d /tmp/ln-tg-v0.2
python /tmp/ln-tg-v0.2/tools/apply_ln_station_v0_2.py
./autogen.sh
./configure
make
```

## Test

1. Launch `./tg-timer`.
2. Confirm the LN Station panel appears.
3. Enter an identity, such as `LN-SER-00025`.
4. Let Tg get a stable reading.
5. Open the menu and click `Save LN Timing JSON`.
6. Confirm a JSON file appears in the repo/run directory.
