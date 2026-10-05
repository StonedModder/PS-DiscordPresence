# PS-DiscordPresence

PS5 game activity service with a local status endpoint and a Python Discord RPC bridge.

## Contents

- `src/` payload sources
- `bridge.py` desktop Discord RPC bridge

## Bridge

Install Python 3 and pypresence:

```text
python -m pip install pypresence
```

Run `bridge.py` once, then set `discord_application_id` in:

```text
~/.ps-discordpresence-bridge/config.json
```

The bridge reads `http://<ps5-host>:9878/api/status` and publishes the active title, official PlayStation Store cover art, and firmware line through the local Discord desktop client. PS4 titles (`CUSAxxxxx`) without a store page fall back to Sony's title metadata service for their name and icon.

## Build

Requires the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and LLVM 18:

```text
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make                          # builds PS-DiscordPresence.elf
make test PS5_HOST=<ps5-ip>   # sends it to an ELF loader on port 9021
```

## Tests

Host-side tests for the portable C sources and the bridge:

```text
cd tests
clang -I../src ../src/config.c ../src/reducer.c ../src/metadata.c test_c.c -o run_tests && ./run_tests
python3 test_bridge.py && python3 test_offline.py
```

## Release

`v0.01` includes `PS-DiscordPresence.elf`.
