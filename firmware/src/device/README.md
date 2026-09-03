# Device scaffolds (UNTESTED)

These files are **scaffolds for the on-device integration** (story #28). They
capture the intended wiring but are **not built or tested** in this repo's CI:
there is no ESP toolchain or hardware here, so none of this has been compiled or
run on a MeowKit.

- Each file's body is guarded by `CATNIP_DEVICE_WIP`, so it compiles to nothing
  until that flag is set and the required deps (LVGL, ESP-IDF drivers, a PNG
  decoder) are added to `platformio.ini`. This keeps the normal build green.
- The host build (`make test`) ignores these `.cpp` files entirely.

Bring them to life on real hardware, one issue at a time; only then close the
matching issue.
