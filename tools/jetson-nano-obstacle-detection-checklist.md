# Jetson Nano Obstacle Detection — Setup Checklist

**Route: C++ + TensorRT.** Detection runs through the TensorRT C++ API, Every dependency below ships pre-installed with
JetPack — you install **nothing extra** on the Nano for detection.

Target stack (the frozen, known-good baseline — verify you land on exactly this):

| Component   | Expected value            | Used for                          |
|-------------|---------------------------|-----------------------------------|
| OS          | Ubuntu 18.04 (L4T r32.7.x)| base                              |
| CUDA        | 10.2                      | inference                         |
| cuDNN       | 8.2.x                     | inference                         |
| TensorRT    | 8.2.x                     | inference (engine build + runtime)|
| OpenCV      | 4.1.1 (bundled)           | frame decode/draw + V4L2 cv::Mat  |
| Python      | 3.6.9 (system)            | tooling only (jtop) — not detection |

PyTorch/torchvision appear nowhere here. The only place a `.pt` model meets PyTorch is the
one-time `.pt → .onnx` export, which you do **on your PC** (or skip with a pre-exported ONNX).
The Nano stays PyTorch-free.

## Cardinal rules (read once, obey always)
- [ ] Read these before touching anything:
  - **Never** run a blanket `sudo apt upgrade` — update specific packages only, never the `nvidia-*`/L4T ones.
  - **Never** `sudo pip install` into system Python — use a virtualenv.
  - **Never** `pip install ultralytics` or any YOLO Python package on the Nano — the C++ route doesn't need it, and it would only pull incompatible deps.
  - **Image the SD card** after every milestone (Phases 4, 7, 9). This is your undo button.

---

## Phase 0 — Hardware prep
- [ ] microSD card 64 GB+, rated A1 or A2 (cheap/slow cards corrupt and "break" the build)
- [ ] Power: 5V/4A barrel-jack supply, and a jumper on J48 to enable barrel-jack mode (micro-USB browns out under load)
- [ ] Logitech C270 + keyboard/mouse/monitor (or set up headless SSH)
- [ ] A host PC with an SD card reader (for flashing and for backups)

## Phase 1 — Flash the OS
- [ ] Download the Jetson Nano Developer Kit SD card image:
      https://developer.nvidia.com/embedded/learn/get-started-jetson-nano-devkit
      (For the latest security-patched 4.6.6 instead, use SDK Manager / the JetPack Archive — same stack underneath.)
- [ ] Format the card with SD Memory Card Formatter
- [ ] Flash the image with Balena Etcher
- [ ] Insert card, connect monitor/keyboard, power on
- [ ] Complete first-boot Ubuntu setup; choose **MAXN** when prompted; create your user

## Phase 2 — Verify the stack (gate: do not proceed until all match the table)
Run each and confirm the version:

- [ ] L4T / JetPack base:
      `cat /etc/nv_tegra_release`   → expect `# R32 (release), REVISION: 7.x`
- [ ] CUDA (add it to PATH first — common gotcha):
      ```
      echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc
      echo 'export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
      source ~/.bashrc
      nvcc --version                 # expect release 10.2
      ```
- [ ] cuDNN:
      `dpkg -l | grep -i cudnn`      → expect 8.2.x
- [ ] TensorRT (you'll use both the libs and the trtexec tool):
      `dpkg -l | grep -i nvinfer`    → expect 8.2.x
      `/usr/src/tensorrt/bin/trtexec --help | head`   → confirms trtexec is present
- [ ] OpenCV (C++ dev headers, no Python needed):
      `pkg-config --modversion opencv4`   → expect 4.1.1
      (if pkg-config finds nothing, try `dpkg -l | grep -i opencv`)

## Phase 3 — Performance + monitoring
- [ ] Lock to max performance:
      ```
      sudo nvpmodel -m 0
      sudo jetson_clocks
      ```
- [ ] Install jtop (the Nano's htop) — also your live temp/throttle/RAM monitor:
      ```
      sudo apt install -y python3-pip
      sudo -H pip3 install -U jetson-stats
      sudo reboot
      ```
- [ ] After reboot: run `jtop`, confirm GPU shows up and you're in MAXN/10W mode
- [ ] Add build swap (needed for any on-device compile; Nano's 2 GB zram isn't enough):
      ```
      sudo fallocate -l 4G /var/swapfile
      sudo chmod 600 /var/swapfile
      sudo mkswap /var/swapfile && sudo swapon /var/swapfile
      echo '/var/swapfile swap swap defaults 0 0' | sudo tee -a /etc/fstab
      free -h          # confirm ~4G swap
      ```

## Phase 4 — BACKUP (do not skip)
- [ ] Power off, move the SD card to your host PC
- [ ] Identify the device carefully (wrong device = data loss): `lsblk`
- [ ] Clone to an image file (Linux/macOS host):
      `sudo dd if=/dev/sdX of=nano-clean-stack.img bs=4M status=progress`
- [ ] Label this backup "clean stack — versions verified." This is your fallback for the rest of the project.

## Phase 5 — Camera + your V4L2 capture (the low-level core)
- [ ] Install V4L2 tools:
      `sudo apt install -y v4l-utils`
- [ ] Plug in the C270, confirm it enumerates:
      `v4l2-ctl --list-devices`      → C270 appears as /dev/video0
- [ ] Inspect its real format/resolution table (note where YUYV stops and MJPEG begins):
      `v4l2-ctl --device /dev/video0 --list-formats-ext`
- [ ] Copy your `capture.c` onto the Nano, then build and run:
      ```
      gcc -O2 -Wall -o capture capture.c
      ./capture
      ```
- [ ] Open `frame.ppm` in an image viewer — confirm a correct, non-skewed color image
- [ ] (Optional) turn the warmup loop into a continuous loop and check real YUYV FPS

## Phase 6 — Detection: Qengineering YOLOv8 + TensorRT (C++, no PyTorch)
- [ ] **On your PC (not the Nano): get an ONNX model.** Either download a pre-exported
      `yolov8n.onnx`, or with Ultralytics on your laptop run:
      `yolo export model=yolov8n.pt format=onnx opset=12`
      Copy the resulting `.onnx` to the Nano (scp/USB). This is the ONLY step that ever touches
      PyTorch, and it happens off-device.
- [ ] Clone the Nano-specific, TensorRT-8 branch:
      `git clone https://github.com/Qengineering/YoloV8-TensorRT-Jetson_Nano.git`
      (Confirm/checkout the branch that matches TensorRT 8.x per the repo README.)
- [ ] Read its README — it specifies the exact model file name and where to place it
- [ ] **On the Nano: build the TensorRT engine from the ONNX** (engines are tied to this exact
      GPU + TensorRT version and are NOT portable):
      ```
      /usr/src/tensorrt/bin/trtexec --onnx=yolov8n.onnx \
          --saveEngine=yolov8n.engine --fp16
      ```
      (FP16 roughly doubles throughput on the Nano. This step takes a few minutes.)
- [ ] Build the project:
      ```
      cd YoloV8-TensorRT-Jetson_Nano
      mkdir build && cd build
      cmake .. && make -j2          # -j2, not -j4 — full parallel build can OOM the Nano
      ```
- [ ] Run the sample on a test image/video; confirm boxes draw and note the FPS in jtop
      (expect roughly 5–15 FPS; first inference is slow as the engine warms up)

## Phase 7 — BACKUP again
- [ ] Re-image the card ("stack + camera + detection working"). Same `dd` command, new filename.

## Phase 8 — Integrate (your pipeline)
- [ ] Feed your V4L2 capture into detection: after `VIDIOC_DQBUF`, wrap the buffer as a
      `cv::Mat` (YUYV→BGR via `cv::cvtColor(..., COLOR_YUV2BGR_YUYV)`) and pass it to the
      inference call — replacing the repo's `VideoCapture` source
- [ ] Decide what "obstacle detected" triggers (log / stop / steer)
- [ ] If you need true distance, add a sensor (VL53L0X ToF over I2C, or HC-SR04 ultrasonic
      over GPIO) and fuse its reading with the detection box — a mono C270 alone gives no depth

## Phase 9 — Final backup + lock-in
- [ ] Re-image the working system one last time
- [ ] Write down the exact version combination that worked (copy the Phase 2 outputs into a file
      on the card) — on this frozen platform, the working combo is the single most valuable artifact

---

### If something breaks
- First response: reflash your most recent good `.img` (10 min) instead of debugging the system.
- `import cv2` fails or a binary won't link after an apt action → you ran an upgrade; restore the backup.
- `trtexec` engine build fails → check the ONNX opset (try `opset=12`) and that you're on the
  TensorRT-8 branch; engine + library versions must match.
- Detection runs but boxes are garbage → input preprocessing mismatch (size/normalization/letterbox);
  follow the repo's expected input spec exactly.
- Random freezes/corruption → power (use the 5V/4A barrel jack) or a worn SD card.

### Reference links
- Get Started / SD image: https://developer.nvidia.com/embedded/learn/get-started-jetson-nano-devkit
- V4L2 API + capture.c: https://docs.kernel.org/userspace-api/media/v4l/capture.c.html
- YOLOv8 TensorRT (Nano): https://github.com/Qengineering/YoloV8-TensorRT-Jetson_Nano
- (Not needed for this C++ route) PyTorch-for-Nano wheels, only if you ever switch to the Python path: https://github.com/Qengineering/PyTorch-Jetson-Nano
