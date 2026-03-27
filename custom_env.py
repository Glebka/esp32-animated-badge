Import("env", "projenv")

try:
    import os

    platform = env.PioPlatform()
    board = env.BoardConfig()
    pioenv = env['PIOENV']
    mcu = board.get("build.mcu", "esp32")
    UPLOADER=os.path.join(
        platform.get_package_dir("tool-esptoolpy") or "", "esptool")
    offset_image_pairs = []
    for image in env.get("FLASH_EXTRA_IMAGES", []):
        offset_image_pairs += [str(image[0]), env.subst(image[1])]
    merged_bin = f"$BUILD_DIR/{pioenv}-$PROGNAME-merged.bin"
    env.AddPostAction(
        "$BUILD_DIR/${PROGNAME}.bin",
        env.VerboseAction(" ".join([
            "$PYTHONEXE", UPLOADER, 
            "--chip", mcu, 
            "merge-bin", 
            "-o", merged_bin,
            "--flash-mode", "${__get_board_flash_mode(__env__)}",
            "--flash-size", board.get("upload.flash_size", "detect"),
            "0x10000",
            "$BUILD_DIR/${PROGNAME}.bin",
        ] + offset_image_pairs), "Merging firmware files to $BUILD_DIR/$PROGNAME-merged.bin")
    )

except ImportError:
    raise RuntimeError('Unable to set custom CXX flags')