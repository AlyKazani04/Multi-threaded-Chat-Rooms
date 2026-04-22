# Troubleshooting

## Error: `raylib.h: No such file or directory`
```bash
./setup.sh   # re-run setup; it will build raylib from source for Ubuntu
```

## Error: `raygui.h: No such file or directory`
```bash
curl -L -o src/raygui.h \
  https://raw.githubusercontent.com/raysan5/raygui/master/src/raygui.h
```

## Error: `cannot open display` / Black window
- Ensure VirtualBox has **3D Acceleration** enabled (VM Settings → Display)
- Log out and log back in after installing Guest Additions
- Try: `export DISPLAY=:0` then re-run

## Error: `undefined reference to pthread_create`
- Ensure `-lpthread` is in `LDFLAGS` in the Makefile (it already is)

## Build fails with `-Werror` on older gcc
- Remove `-Werror` from `CFLAGS` in Makefile if present (it is not in our Makefile by default)

## `./setup.sh` hangs on raylib source build
- This means the internet is slow; wait up to 5 minutes
- Or manually download: https://github.com/raysan5/raylib/releases
