/*
 * cam_capture.c -- Minimal media-controller + V4L2 capture tool.
 *
 * Links the ov5647 sensor entity to the unicam capture entity, queries the
 * sensor subdevice's active format, RE-ASSERTS it via VIDIOC_SUBDEV_S_FMT
 * (some drivers only mark a pad "configured" for pipeline validation after
 * an explicit S_FMT call), sets the matching pixel format on the video
 * capture node, then grabs one raw frame and writes it to disk.
 *
 * Usage on target:
 *   ./cam_capture /dev/mediaN output.raw [/dev/videoN] [/dev/v4l-subdevN]
 *
 * Run once with only mediaN + output.raw to get the entity dump. Run again
 * with video + subdev device paths once known.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>

#define NUM_BUFFERS 4

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int find_entity(int media_fd, const char *name_substr,
                        struct media_entity_desc *out) {
    struct media_entity_desc entd;
    memset(&entd, 0, sizeof(entd));
    entd.id = MEDIA_ENT_ID_FLAG_NEXT;

    while (ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &entd) == 0) {
        fprintf(stderr, "[entity] id=%d name=\"%s\" type=0x%08x devnode=%d:%d\n",
                entd.id, entd.name, entd.type, entd.dev.major, entd.dev.minor);
        if (strstr(entd.name, name_substr) != NULL) {
            *out = entd;
            return 0;
        }
        entd.id |= MEDIA_ENT_ID_FLAG_NEXT;
    }
    return -1;
}

static int find_pad_and_link(int media_fd, __u32 entity_id,
                              struct media_link_desc *link_out) {
    struct media_links_enum le;
    struct media_pad_desc pads[16];
    struct media_link_desc links[32];
    memset(&le, 0, sizeof(le));
    memset(pads, 0, sizeof(pads));
    memset(links, 0, sizeof(links));
    le.entity = entity_id;
    le.pads = pads;
    le.links = links;

    if (ioctl(media_fd, MEDIA_IOC_ENUM_LINKS, &le) < 0) {
        perror("MEDIA_IOC_ENUM_LINKS");
        return -1;
    }

    for (int i = 0; i < 32; i++) {
        struct media_link_desc *l = &links[i];
        if (l->source.entity == 0 && l->sink.entity == 0) break;
        if (l->source.entity == entity_id || l->sink.entity == entity_id) {
            *link_out = *l;
            return 0;
        }
    }
    return -1;
}

static unsigned mbus_code_to_fourcc(unsigned code) {
    switch (code) {
        case 0x3006: return v4l2_fourcc('B','G','1','0'); /* SBGGR10_1X10 */
        case 0x3001: return v4l2_fourcc('B','A','8','1'); /* SBGGR8_1X8 */
        case 0x3013: return v4l2_fourcc('B','G','1','2'); /* SBGGR12_1X12 */
        case 0x300e: return v4l2_fourcc('U','Y','V','Y'); /* UYVY8_1X16 */
        case 0x2008: return v4l2_fourcc('Y','U','Y','V'); /* YUYV8_1X16 */
        default: return 0;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s /dev/mediaN output.raw [video_device] [subdev_device]\n", argv[0]);
        return 1;
    }
    const char *media_path = argv[1];
    const char *out_path = argv[2];
    const char *video_path = (argc >= 4) ? argv[3] : NULL;
    const char *subdev_path = (argc >= 5) ? argv[4] : "/dev/v4l-subdev0";

    int mfd = open(media_path, O_RDWR);
    if (mfd < 0) { perror("open media"); return 1; }

    fprintf(stderr, "== Enumerating media entities on %s ==\n", media_path);
    struct media_entity_desc sensor_ent, cap_ent;
    memset(&sensor_ent, 0, sizeof(sensor_ent));
    memset(&cap_ent, 0, sizeof(cap_ent));

    int have_sensor = (find_entity(mfd, "ov5647", &sensor_ent) == 0);
    close(mfd);
    mfd = open(media_path, O_RDWR);
    int have_cap = (find_entity(mfd, "unicam-image", &cap_ent) == 0);
    if (!have_cap) {
        close(mfd);
        mfd = open(media_path, O_RDWR);
        have_cap = (find_entity(mfd, "unicam", &cap_ent) == 0);
    }

    if (!have_sensor) {
        fprintf(stderr, "ERROR: could not find 'ov5647' entity.\n");
        return 1;
    }
    if (!have_cap) {
        fprintf(stderr, "ERROR: could not find 'unicam' capture entity.\n");
        return 1;
    }

    fprintf(stderr, "Sensor entity: id=%d name=%s\n", sensor_ent.id, sensor_ent.name);
    fprintf(stderr, "Capture entity: id=%d name=%s devnode=%d:%d\n",
            cap_ent.id, cap_ent.name, cap_ent.dev.major, cap_ent.dev.minor);

    struct media_link_desc link;
    memset(&link, 0, sizeof(link));
    if (find_pad_and_link(mfd, sensor_ent.id, &link) == 0) {
        link.flags |= MEDIA_LNK_FL_ENABLED;
        if (ioctl(mfd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
            fprintf(stderr, "WARNING: MEDIA_IOC_SETUP_LINK failed: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "Link enabled: entity %d pad %d -> entity %d pad %d\n",
                    link.source.entity, link.source.index,
                    link.sink.entity, link.sink.index);
        }
    } else {
        fprintf(stderr, "NOTE: could not enumerate a link automatically.\n");
    }

    close(mfd);

    if (!video_path) {
        fprintf(stderr, "\nNo video device given. Re-run with the node matching "
                        "devnode major:minor printed above, plus subdev path.\n");
        return 0;
    }

    /* ---- Query sensor subdevice's active format, then re-assert it via
       S_FMT so the pipeline validator treats this pad as configured. ---- */
    unsigned sensor_width = 0, sensor_height = 0, sensor_code = 0;
    int sfd = open(subdev_path, O_RDWR);
    if (sfd >= 0) {
        struct v4l2_subdev_format sfmt;
        memset(&sfmt, 0, sizeof(sfmt));
        sfmt.pad = 0;
        sfmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        if (xioctl(sfd, VIDIOC_SUBDEV_G_FMT, &sfmt) == 0) {
            sensor_width = sfmt.format.width;
            sensor_height = sfmt.format.height;
            sensor_code = sfmt.format.code;
            fprintf(stderr, "Subdev active format (pad 0): %ux%u code=0x%04x\n",
                    sensor_width, sensor_height, sensor_code);

            struct v4l2_subdev_format sfmt_set;
            memset(&sfmt_set, 0, sizeof(sfmt_set));
            sfmt_set = sfmt;
            sfmt_set.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            if (xioctl(sfd, VIDIOC_SUBDEV_S_FMT, &sfmt_set) < 0) {
                fprintf(stderr, "WARNING: VIDIOC_SUBDEV_S_FMT (pad 0) failed: %s\n",
                        strerror(errno));
            } else {
                fprintf(stderr, "Subdev pad 0 format re-asserted: %ux%u code=0x%04x\n",
                        sfmt_set.format.width, sfmt_set.format.height, sfmt_set.format.code);
            }
        } else {
            perror("VIDIOC_SUBDEV_G_FMT (pad 0)");
        }

        struct v4l2_subdev_format sfmt1;
        memset(&sfmt1, 0, sizeof(sfmt1));
        sfmt1.pad = 1;
        sfmt1.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        if (xioctl(sfd, VIDIOC_SUBDEV_G_FMT, &sfmt1) == 0) {
            fprintf(stderr, "Subdev active format (pad 1): %ux%u code=0x%04x\n",
                    sfmt1.format.width, sfmt1.format.height, sfmt1.format.code);
        }

        close(sfd);
    } else {
        fprintf(stderr, "NOTE: could not open %s (%s) -- will fall back to "
                        "video node's default format.\n",
                subdev_path, strerror(errno));
    }

    /* ---- V4L2 capture on the resolved video device ---- */
    int vfd = open(video_path, O_RDWR);
    if (vfd < 0) { perror("open video device"); return 1; }

    /* Explicitly disable BOTH auto-exposure and auto-gain controls first
       -- if either stays enabled, it silently overrides any manual value
       we set afterward, which is why earlier attempts stayed dark even
       though the manual values appeared to "set" successfully. Then set
       manual exposure and analogue gain to their maximums. Control IDs
       taken from this driver'''s own VIDIOC_QUERYCTRL dump:
         0x009a0901 Auto Exposure       (0=manual, 1=auto) -- driver-specific
         0x00980912 Gain, Automatic     (0=manual, 1=auto) -- generic V4L2
         0x00980911 Exposure            range 4..500
         0x009e0903 Analogue Gain       range 16..1023 */
    struct v4l2_control ctrl;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = 0x009a0901; /* Auto Exposure (driver-specific) */
    ctrl.value = 0; /* manual */
    if (xioctl(vfd, VIDIOC_S_CTRL, &ctrl) < 0) {
        fprintf(stderr, "NOTE: Auto Exposure (0x009a0901) disable failed: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "Auto Exposure disabled (manual mode)\n");
    }

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_GAIN; /* "Gain, Automatic" toggle, 0x00980912 */
    ctrl.value = 0; /* manual */
    if (xioctl(vfd, VIDIOC_S_CTRL, &ctrl) < 0) {
        fprintf(stderr, "NOTE: Gain,Automatic disable failed: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "Auto Gain disabled (manual mode)\n");
    }

    struct v4l2_queryctrl qctrl;
    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = V4L2_CID_EXPOSURE;
    if (xioctl(vfd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.id = V4L2_CID_EXPOSURE;
        ctrl.value = qctrl.maximum;
        if (xioctl(vfd, VIDIOC_S_CTRL, &ctrl) < 0) {
            fprintf(stderr, "NOTE: Exposure set failed: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "Exposure set to max: %d (range %d-%d)\n",
                    qctrl.maximum, qctrl.minimum, qctrl.maximum);
        }
    }

    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = 0x009e0903; /* Analogue Gain */
    if (xioctl(vfd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.id = 0x009e0903;
        ctrl.value = qctrl.maximum;
        if (xioctl(vfd, VIDIOC_S_CTRL, &ctrl) < 0) {
            fprintf(stderr, "NOTE: Analogue Gain set failed: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "Analogue Gain set to max: %d (range %d-%d)\n",
                    qctrl.maximum, qctrl.minimum, qctrl.maximum);
        }
    }

    /* Read back actual current values to confirm what really stuck. */
    fprintf(stderr, "== Verifying actual current control values ==\n");
    int check_ids[] = {0x009a0901, V4L2_CID_GAIN, V4L2_CID_EXPOSURE, 0x009e0903};
    const char *check_names[] = {"Auto Exposure", "Gain,Automatic", "Exposure", "Analogue Gain"};
    for (int i = 0; i < 4; i++) {
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.id = check_ids[i];
        if (xioctl(vfd, VIDIOC_G_CTRL, &ctrl) == 0) {
            fprintf(stderr, "  %s = %d\n", check_names[i], ctrl.value);
        } else {
            fprintf(stderr, "  %s: G_CTRL failed: %s\n", check_names[i], strerror(errno));
        }
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(vfd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        return 1;
    }

    unsigned target_fourcc = sensor_code ? mbus_code_to_fourcc(sensor_code) : 0;
    if (target_fourcc == 0) {
        target_fourcc = v4l2_fourcc('U','Y','V','Y');
        fprintf(stderr, "NOTE: using fallback fourcc UYVY\n");
    }
    if (sensor_width && sensor_height) {
        fmt.fmt.pix.width = sensor_width;
        fmt.fmt.pix.height = sensor_height;
    }
    fmt.fmt.pix.pixelformat = target_fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(vfd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return 1;
    }
    fprintf(stderr, "Format set: %ux%u fourcc=%c%c%c%c size=%u\n",
            fmt.fmt.pix.width, fmt.fmt.pix.height,
            (char)(fmt.fmt.pix.pixelformat & 0xff),
            (char)((fmt.fmt.pix.pixelformat >> 8) & 0xff),
            (char)((fmt.fmt.pix.pixelformat >> 16) & 0xff),
            (char)((fmt.fmt.pix.pixelformat >> 24) & 0xff),
            fmt.fmt.pix.sizeimage);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(vfd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return 1;
    }

    void *bufs[NUM_BUFFERS];
    size_t buf_lens[NUM_BUFFERS];

    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(vfd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return 1;
        }
        bufs[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                       vfd, buf.m.offset);
        if (bufs[i] == MAP_FAILED) { perror("mmap"); return 1; }
        buf_lens[i] = buf.length;

        if (xioctl(vfd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return 1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(vfd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return 1;
    }
    fprintf(stderr, "Streaming started, waiting for one frame...\n");

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(vfd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return 1;
    }
    fprintf(stderr, "Got frame: bytesused=%u index=%u\n", buf.bytesused, buf.index);

    FILE *out = fopen(out_path, "wb");
    if (!out) { perror("fopen output"); return 1; }
    fwrite(bufs[buf.index], 1, buf.bytesused, out);
    fclose(out);
    fprintf(stderr, "Wrote %u bytes to %s\n", buf.bytesused, out_path);

    xioctl(vfd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < req.count; i++) munmap(bufs[i], buf_lens[i]);
    close(vfd);

    fprintf(stderr, "Done.\n");
    return 0;
}
