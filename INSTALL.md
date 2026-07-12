# Install

Copy the modified files over the current LN Watchmaker Station repository, preserving paths.

```bash
pacman -S mingw-w64-ucrt-x86_64-curl
make distclean || true
./autogen.sh
./configure
make
```

Launch `tg-timer.exe`, open **ERP Connection**, save the credentials, and use **Test Connection**.
