# yolo support library

This directory contains the YOLO support-library metadata and implementation sources.

## layout

- `yolo.ycmd.json`: command contract and implementation entry mapping
- `yolo.ysup.json`: IDE support metadata, artifact location, command entries
- `yolo.protocol.json`: compile/protocol extern declarations
- `impl/`: implementation sources copied from root files
  - `yolo11.h`
  - `yolo11.cpp`
  - `ncnn_gpu_policy.h`
  - `ncnn_gpu_policy.cpp`
  - `yolo_api.h`

## notes

- Current implementation entry is `impl/yolo11.cpp` on Windows.
- Default static library output path is:
  - `build/windows/x64/Release/yolo.lib`

## reference/out-parameter rule

If a command parameter is an output reference (needs variable-address passing), configure both files:

1. In `yolo.ycmd.json`, mark that parameter with:
   - `"isVariable": true`
2. In `yolo.protocol.json` `emit`, use reference placeholder:
   - `{&n}` or `{&n|default}`

Example (`yolo.getObjectInfo`):

- `yolo.ycmd.json` params `x/y/w/h/label/prob` are marked as `"isVariable": true`
- `yolo.protocol.json` emit uses:
  - `(float*)({&2|0})`, `(float*)({&3|0})`, `(float*)({&4|0})`, `(float*)({&5|0})`
  - `(int*)({&6|0})`, `(float*)({&7|0})`
