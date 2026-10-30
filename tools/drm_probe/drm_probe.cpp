// Minimal DRM/KMS probe for the TB-J716F physical-display black-screen
// investigation.  It enumerates the DRM connector, creates a linear dumb
// buffer, fills it with solid colors, modesets it on the DSI CRTC, and
// samples battery current so we can tell "panel emits light" from
// "panel receives black pixels".
//
// Build:  m drm_probe
// Run (root, after `stop surfaceflinger`):
//   /data/local/tmp/drm_probe --black 10 --white 10 --cycles 2 --hold 120
//
// This tool is diagnostic only.  It does not write any partition.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#include <string>
#include <vector>

#define LOGE(...)            \
  do {                       \
    printf(__VA_ARGS__);     \
    fflush(stdout);          \
  } while (0)

static int xioctl(int fd, unsigned long req, void *arg) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r == -1 && errno == EINTR);
  return r;
}

static long read_sysfs_long(const char *path) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  char buf[64] = {};
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return -1;
  return strtol(buf, nullptr, 10);
}

static std::string read_sysfs_str(const char *path) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return std::string();
  char buf[128] = {};
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return std::string();
  std::string s(buf, n);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

static int write_sysfs_str(const char *path, const char *val) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  ssize_t n = write(fd, val, strlen(val));
  close(fd);
  return n == (ssize_t)strlen(val) ? 0 : -1;
}

static const char *conn_type_name(uint32_t t) {
  switch (t) {
    case DRM_MODE_CONNECTOR_Unknown: return "Unknown";
    case DRM_MODE_CONNECTOR_VGA: return "VGA";
    case DRM_MODE_CONNECTOR_DVII: return "DVI-I";
    case DRM_MODE_CONNECTOR_DVID: return "DVI-D";
    case DRM_MODE_CONNECTOR_DVIA: return "DVI-A";
    case DRM_MODE_CONNECTOR_Composite: return "Composite";
    case DRM_MODE_CONNECTOR_SVIDEO: return "SVIDEO";
    case DRM_MODE_CONNECTOR_LVDS: return "LVDS";
    case DRM_MODE_CONNECTOR_Component: return "Component";
    case DRM_MODE_CONNECTOR_9PinDIN: return "9PinDIN";
    case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
    case DRM_MODE_CONNECTOR_HDMIA: return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB: return "HDMI-B";
    case DRM_MODE_CONNECTOR_TV: return "TV";
    case DRM_MODE_CONNECTOR_eDP: return "eDP";
    case DRM_MODE_CONNECTOR_VIRTUAL: return "Virtual";
    case DRM_MODE_CONNECTOR_DSI: return "DSI";
    case DRM_MODE_CONNECTOR_DPI: return "DPI";
    default: return "?";
  }
}

static uint32_t color_from_name(const std::string &name) {
  if (name == "black") return 0x00000000;
  if (name == "white") return 0x00ffffff;
  if (name == "red") return 0x00ff0000;
  if (name == "green") return 0x0000ff00;
  if (name == "blue") return 0x000000ff;
  return 0x00ffffff;
}

struct PhaseStat {
  uint32_t samples = 0;
  long min_ua = 0;
  long max_ua = 0;
  double sum_ua = 0.0;
};

static long sample_current() {
  long ua = read_sysfs_long("/sys/class/power_supply/battery/current_now");
  if (ua < 0)
    ua = read_sysfs_long("/sys/class/power_supply/bms/current_now");
  return ua;
}

static void fill_buffer(void *ptr, size_t bytes, uint32_t argb) {
  uint32_t *p = static_cast<uint32_t *>(ptr);
  size_t n = bytes / sizeof(uint32_t);
  for (size_t i = 0; i < n; ++i) p[i] = argb;
  __sync_synchronize();
}

static void run_phase(const char *tag, void *ptr, size_t bytes, uint32_t argb,
                      int seconds, bool sample) {
  fill_buffer(ptr, bytes, argb);
  LOGE("PHASE_BEGIN color=%s argb=0x%08x secs=%d\n", tag, argb, seconds);
  std::vector<long> vals;
  vals.reserve(seconds * 2 + 1);
  long first = -1;
  for (int i = 0; i < seconds * 2; ++i) {
    usleep(500 * 1000);
    long ua = sample_current();
    long mv = read_sysfs_long("/sys/class/power_supply/battery/voltage_now");
    if (ua >= 0) {
      if (first < 0) first = ua;
      vals.push_back(ua);
    }
    if (sample) {
      LOGE("SAMPLE color=%s i=%d uA=%ld uV=%ld\n", tag, i, ua, mv);
    }
  }
  // Summary over the tail 70% to skip the brightness/refresh ramp.
  size_t start = vals.size() * 3 / 10;
  double sum = 0.0;
  long mn = 0, mx = 0;
  size_t n = 0;
  for (size_t i = start; i < vals.size(); ++i) {
    long v = vals[i];
    if (n == 0) { mn = mx = v; }
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    sum += (double)v;
    ++n;
  }
  LOGE("PHASE_END color=%s samples=%zu first_uA=%ld tail_n=%zu tail_min_uA=%ld "
       "tail_max_uA=%ld tail_mean_uA=%.0f\n",
       tag, vals.size(), first, n, mn, mx, n ? sum / n : 0.0);
}

int main(int argc, char **argv) {
  int black_secs = 10;
  int white_secs = 10;
  int cycles = 2;
  int hold_secs = 120;
  std::string hold_color = "white";
  bool dry_run = false;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--black") && i + 1 < argc) black_secs = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--white") && i + 1 < argc) white_secs = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) cycles = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold_secs = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--hold-color") && i + 1 < argc) hold_color = argv[++i];
    else if (!strcmp(argv[i], "--dry-run")) dry_run = true;
    else {
      LOGE("usage: %s [--black N] [--white N] [--cycles N] [--hold N] "
           "[--hold-color white|black|red|green|blue] [--dry-run]\n", argv[0]);
      return 2;
    }
  }

  LOGE("drm_probe: black=%d white=%d cycles=%d hold=%d hold_color=%s dry_run=%d\n",
       black_secs, white_secs, cycles, hold_secs, hold_color.c_str(), dry_run);

  int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    LOGE("FATAL open /dev/dri/card0: %s\n", strerror(errno));
    return 1;
  }
  LOGE("opened /dev/dri/card0 fd=%d\n", fd);

  if (xioctl(fd, DRM_IOCTL_SET_MASTER, nullptr) < 0) {
    LOGE("WARN DRM_IOCTL_SET_MASTER failed: %s (continuing)\n", strerror(errno));
  } else {
    LOGE("DRM master acquired\n");
  }

  drm_mode_card_res res = {};
  if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
    LOGE("FATAL GETRESOURCES: %s\n", strerror(errno));
    close(fd);
    return 1;
  }
  LOGE("resources: fbs=%u crtcs=%u connectors=%u encoders=%u max=%ux%u\n",
       res.count_fbs, res.count_crtcs, res.count_connectors, res.count_encoders,
       res.max_width, res.max_height);

  std::vector<uint32_t> fbs(res.count_fbs);
  std::vector<uint32_t> crtcs(res.count_crtcs);
  std::vector<uint32_t> connectors(res.count_connectors);
  std::vector<uint32_t> encoders(res.count_encoders);
  res.fb_id_ptr = (uint64_t)(uintptr_t)fbs.data();
  res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs.data();
  res.connector_id_ptr = (uint64_t)(uintptr_t)connectors.data();
  res.encoder_id_ptr = (uint64_t)(uintptr_t)encoders.data();
  if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
    LOGE("FATAL GETRESOURCES(2): %s\n", strerror(errno));
    close(fd);
    return 1;
  }
  for (uint32_t i = 0; i < res.count_connectors; ++i) {
    drm_mode_get_connector c = {};
    c.connector_id = connectors[i];
    if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) {
      LOGE("connector %u: GETCONNECTOR failed: %s\n", connectors[i], strerror(errno));
      continue;
    }
    uint32_t nm = c.count_modes;
    uint32_t ne = c.count_encoders;
    std::vector<drm_mode_modeinfo> modes(nm);
    std::vector<uint32_t> encs(ne);
    c.modes_ptr = (uint64_t)(uintptr_t)modes.data();
    c.encoders_ptr = (uint64_t)(uintptr_t)encs.data();
    c.count_modes = nm;
    c.count_encoders = ne;
    if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) {
      LOGE("connector %u: GETCONNECTOR(2) failed: %s\n", connectors[i], strerror(errno));
      continue;
    }
    LOGE("connector id=%u type=%u(%s) type_id=%u connection=%u enc=%u modes=%u\n",
         c.connector_id, c.connector_type, conn_type_name(c.connector_type),
         c.connector_type_id, c.connection, c.encoder_id, c.count_modes);
    for (uint32_t m = 0; m < c.count_modes && m < 8; ++m) {
      LOGE("  mode[%u] %s %ux%u@%u clock=%u\n", m, modes[m].name,
           modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh, modes[m].clock);
    }
  }
  LOGE("dry-run: enumeration only; not touching the CRTC\n");
  if (dry_run) {
    xioctl(fd, DRM_IOCTL_DROP_MASTER, nullptr);
    close(fd);
    return 0;
  }

  // Pick the connected DSI connector with a mode.  Fall back to any connected
  // connector with a mode.
  uint32_t conn_id = 0, enc_id = 0;
  drm_mode_modeinfo mode = {};
  for (int pass = 0; pass < 2 && !conn_id; ++pass) {
    for (uint32_t i = 0; i < res.count_connectors; ++i) {
      drm_mode_get_connector c = {};
      c.connector_id = connectors[i];
      if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;
      uint32_t nm = c.count_modes;
      uint32_t ne = c.count_encoders;
      if (!nm) continue;
      if (pass == 0 && (c.connector_type != DRM_MODE_CONNECTOR_DSI ||
                        c.connection != DRM_MODE_CONNECTED))
        continue;
      if (pass == 1 && c.connection != DRM_MODE_CONNECTED) continue;
      std::vector<drm_mode_modeinfo> modes(nm);
      std::vector<uint32_t> encs(ne);
      c.modes_ptr = (uint64_t)(uintptr_t)modes.data();
      c.encoders_ptr = (uint64_t)(uintptr_t)encs.data();
      c.count_modes = nm;
      c.count_encoders = ne;
      if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;
      conn_id = c.connector_id;
      enc_id = c.encoder_id;
      mode = modes[0];
      LOGE("selected connector id=%u type=%s encoder=%u mode=%s %ux%u@%u\n",
           conn_id, conn_type_name(c.connector_type), enc_id, mode.name,
           mode.hdisplay, mode.vdisplay, mode.vrefresh);
      break;
    }
  }
  if (!conn_id) {
    LOGE("FATAL no connected connector with a mode\n");
    xioctl(fd, DRM_IOCTL_DROP_MASTER, nullptr);
    close(fd);
    return 1;
  }

  uint32_t crtc_id = 0;
  if (enc_id) {
    drm_mode_get_encoder enc = {};
    enc.encoder_id = enc_id;
    if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
      crtc_id = enc.crtc_id;
      LOGE("encoder %u: crtc_id=%u possible_crtcs=0x%x\n", enc_id, crtc_id,
           enc.possible_crtcs);
      if (!crtc_id) {
        for (int b = 0; b < 32; ++b) {
          if (enc.possible_crtcs & (1u << b)) {
            if ((uint32_t)b < res.count_crtcs) crtc_id = crtcs[b];
            break;
          }
        }
      }
    }
  }
  if (!crtc_id && res.count_crtcs) crtc_id = crtcs[0];
  if (!crtc_id) {
    LOGE("FATAL no CRTC\n");
    xioctl(fd, DRM_IOCTL_DROP_MASTER, nullptr);
    close(fd);
    return 1;
  }
  LOGE("using crtc_id=%u\n", crtc_id);

  drm_mode_create_dumb create = {};
  create.width = mode.hdisplay;
  create.height = mode.vdisplay;
  create.bpp = 32;
  if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
    LOGE("FATAL CREATE_DUMB: %s\n", strerror(errno));
    xioctl(fd, DRM_IOCTL_DROP_MASTER, nullptr);
    close(fd);
    return 1;
  }
  LOGE("dumb: handle=%u pitch=%u size=%llu\n", create.handle, create.pitch,
       (unsigned long long)create.size);

  drm_mode_map_dumb map = {};
  map.handle = create.handle;
  if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
    LOGE("FATAL MAP_DUMB: %s\n", strerror(errno));
    close(fd);
    return 1;
  }
  void *ptr = mmap(nullptr, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                   map.offset);
  if (ptr == MAP_FAILED) {
    LOGE("FATAL mmap: %s\n", strerror(errno));
    close(fd);
    return 1;
  }
  LOGE("mapped %p size=%llu\n", ptr, (unsigned long long)create.size);

  drm_mode_fb_cmd fb = {};
  fb.width = create.width;
  fb.height = create.height;
  fb.pitch = create.pitch;
  fb.bpp = 32;
  fb.depth = 24;
  fb.handle = create.handle;
  if (xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) {
    LOGE("FATAL ADDFB: %s\n", strerror(errno));
    munmap(ptr, create.size);
    close(fd);
    return 1;
  }
  LOGE("framebuffer id=%u\n", fb.fb_id);

  // Read and force the panel backlight to max so a black/white comparison is
  // not confounded by a dimmed DBV.  SF is stopped; nothing else changes it.
  const char *bl_dir = "/sys/class/backlight/panel0-backlight";
  long bl_before = read_sysfs_long((std::string(bl_dir) + "/brightness").c_str());
  long bl_max = read_sysfs_long((std::string(bl_dir) + "/max_brightness").c_str());
  LOGE("backlight before=%ld max=%ld\n", bl_before, bl_max);
  if (bl_max > 0) {
    char v[32];
    snprintf(v, sizeof(v), "%ld", bl_max);
    if (write_sysfs_str((std::string(bl_dir) + "/brightness").c_str(), v) == 0)
      LOGE("backlight set to max=%ld\n", bl_max);
    else
      LOGE("WARN backlight write failed: %s\n", strerror(errno));
  }
  LOGE("backlight after write: brightness=%ld actual=%ld\n",
       read_sysfs_long((std::string(bl_dir) + "/brightness").c_str()),
       read_sysfs_long((std::string(bl_dir) + "/actual_brightness").c_str()));

  drm_mode_crtc crtc = {};
  crtc.crtc_id = crtc_id;
  crtc.fb_id = fb.fb_id;
  crtc.mode_valid = 1;
  crtc.mode = mode;
  crtc.count_connectors = 1;
  crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&conn_id;
  if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
    LOGE("FATAL SETCRTC: %s\n", strerror(errno));
    munmap(ptr, create.size);
    close(fd);
    return 1;
  }
  LOGE("SETCRTC ok\n");

  for (int c = 0; c < cycles; ++c) {
    run_phase("black", ptr, create.size, 0x00000000, black_secs, true);
    run_phase("white", ptr, create.size, 0x00ffffff, white_secs, true);
  }
  uint32_t hold_argb = color_from_name(hold_color);
  run_phase(hold_color.c_str(), ptr, create.size, hold_argb, hold_secs, true);

  // Cleanup: disable CRTC, drop the framebuffer, restore backlight.
  drm_mode_crtc off = {};
  off.crtc_id = crtc_id;
  off.fb_id = 0;
  off.mode_valid = 0;
  off.count_connectors = 0;
  if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &off) < 0)
    LOGE("WARN SETCRTC(off): %s\n", strerror(errno));
  uint32_t fb_id = fb.fb_id;
  if (xioctl(fd, DRM_IOCTL_MODE_RMFB, &fb_id) < 0)
    LOGE("WARN RMFB: %s\n", strerror(errno));
  if (bl_before >= 0) {
    char v[32];
    snprintf(v, sizeof(v), "%ld", bl_before);
    write_sysfs_str((std::string(bl_dir) + "/brightness").c_str(), v);
  }
  munmap(ptr, create.size);
  xioctl(fd, DRM_IOCTL_DROP_MASTER, nullptr);
  close(fd);
  LOGE("DONE\n");
  return 0;
}
