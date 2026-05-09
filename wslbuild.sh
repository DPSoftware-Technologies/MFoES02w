#!/bin/bash
echo "Running build on WSL2"
sh ./build-cross.sh Release

# 1. Ensure SD card is mounted
if ! mountpoint -q /mnt/h; then
    sudo mkdir -p /mnt/h
    sudo mount -t drvfs H: /mnt/h
fi

# 2. Check if mount succeeded
if [ -d "/mnt/h" ]; then
    echo "Copying to SD card..."
    
    # Create the target directory and the libs subdirectory
    # -p ensures it doesn't error if they already exist
    mkdir -p /mnt/h/mfoes02w/libs
    
    # Copy the Binary (into the folder)
    cp ./build-aarch64/bin/mfoes02w /mnt/h/mfoes02w/
    
    # Copy Services to the root of SD card
    cp ./build-aarch64/bin/usbd /mnt/h/
    cp ./build-aarch64/bin/otad /mnt/h/
    
    # Copy Libs (into the libs folder)
    cp -r ./build-aarch64/lib/* /mnt/h/mfoes02w/libs/
    
    echo "Done!"
else
    echo "H: Drive not found. Is the SD card plugged in?"
fi