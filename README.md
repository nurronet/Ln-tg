# LNWS Milestone 0.10 ERP Identity Smart Search

Adds:

- debounced partial search after two characters
- autocomplete popup with matching ERP identities
- exact barcode/serial lookup when Enter is received
- automatic form population for movement serial, movement/part, watch unit, work order, ERP status, measurement type, and QA profile

A USB barcode scanner configured to send Enter will immediately select and load the exact identity.

## Required backend

Deploy FreeERP v1.4.4 from the paired backend overlay first.

## Build

Copy the modified files over the current LNWS source, then:

```bash
make clean
./autogen.sh
./configure
make -j1
```
