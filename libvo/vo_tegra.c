#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stropts.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include "config.h"
#include "mp_msg.h"
#include "help_mp.h"
#include "video_out.h"
#include "video_out_internal.h"
#include "vo_tegra.h"
#include "tegra_dc_ext.h"
#include <errno.h>

#include "fastmemcpy.h"
#include "sub/sub.h"
#include "aspect.h"

#include "subopt-helper.h"

#include "libavutil/common.h"
static void __flip(void);
static int output = 0;

static uint32_t image_width;
static uint32_t image_height;

static struct fb_var_screeninfo fb_vinfo;
struct tegra_dc_ext_flip flip;

static void flip_setup(int out_buff, int width, int heigth,
        int d_width, int d_d_heigth);
uint8_t* nv_mmap(int size, int* offset) ;


static const vo_info_t info = {
	"Tegra",
	"tegra",
	"Ilya Petrov <ilya.muromec@gmail.com>",
	""
};

const LIBVO_EXTERN(tegra);

static int fb, dc, nvmap, out_buff, out_addr, mem_handle;
uint8_t *video_buf[3];

static uint32_t image_width;
static uint32_t image_height;

static void fixup_osd_position(int *x0, int *y0, int *w, int *h)
{
    //*x0 += image_width * (vo_panscan_x >> 1) / (vo_dwidth + vo_panscan_x);
    *w = av_clip(*w, 0, image_width);
    *h = av_clip(*h, 0, image_height);
    *x0 = FFMIN(*x0, image_width  - *w);
    *y0 = FFMIN(*y0, image_height - *h);
}

static void draw_alpha_yv12(int x0, int y0, int w, int h,
                            unsigned char *src, unsigned char *srca,
                            int stride)
{
    fixup_osd_position(&x0, &y0, &w, &h);
}

static int config(uint32_t width, uint32_t height, uint32_t d_width,
		uint32_t d_height, uint32_t flags, char *title,
		uint32_t format) {

        int ret;
        char device[16];

        image_height = height;
	image_width = width;

        snprintf(device, 16, "/dev/tegra_dc_%d", output);

        dc = open(device, O_RDWR);
        if(dc < 0) {
            perror("cant open dc");
            exit(1);
        }

        snprintf(device, 10, "/dev/fb%d", output);
        fb = open(device, O_RDWR);
        if(dc < 0) {
            perror("cant open fb");
            exit(1);
        }

        // get screen info
        ret = ioctl(fb, FBIOGET_VSCREENINFO, &fb_vinfo);
        if(ret < 0) {
            perror("cant get fb info");
            exit(1);
        }
        close(fb);


        nvmap = open("/dev/knvmap", O_RDWR);
        if(nvmap < 0) {
            perror("cant open knvmap");
            exit(1);
        }

        ret = ioctl(dc, TEGRA_DC_EXT_SET_NVMAP_FD, nvmap);
        if (ret < 0) {
            perror("nvmap fd");
            exit(1);
        }
        ret = ioctl(dc, TEGRA_DC_EXT_GET_WINDOW, 1);
        if (ret < 0) {
            perror("get win");
            exit(1);
        }

        struct nvmap_create_handle map;

        int size = width * height ;
        map.size = (size + 4096) * 2;
        printf("size all: %dx%d %x\n", width, height, map.size);

        ret = ioctl(nvmap, NVMAP_IOC_CREATE, &map);
        if (ret < 0) {
            perror("nvmap create fail");
            exit(1);
        }

        mem_handle = map.handle;

        struct nvmap_alloc_handle ah;
        ah.handle = mem_handle;
        ah.heap_mask = 1;
        ah.flags = 1;
        ah.align = 0x100;

        ret = ioctl(nvmap, NVMAP_IOC_ALLOC, &ah);
        if (ret < 0) {
            perror("nvmap alloc fail");
            exit(1);
        }

        struct nvmap_pin_handle pin;
        pin.handles = mem_handle;
        pin.count = 1;

        ret = ioctl(nvmap, NVMAP_IOC_PIN_MULT, &pin);
        if (ret < 0) {
            perror("nvmap pin fail");
            exit(1);
        }

        out_addr = pin.addr;

        ret = ioctl(nvmap, NVMAP_IOC_GET_ID, &map);
        if (ret < 0) {
            perror("nvmap id fail");
            exit(1);
        }

        out_buff = map.id;
        
        int off=0, off_u, off_v;
        video_buf[0] = nv_mmap(size, &off);
        off_u = off;

        video_buf[1] = nv_mmap(size/2, &off);
        off_v = off;

        video_buf[2] = nv_mmap(size/2, &off);

        flip_setup(out_buff, width, height, off_u, off_v);

	return 0;
}


uint8_t* nv_mmap(int size, int* offset) {

    int ret;
    struct nvmap_map_caller nv_mmap;

    if(size % 4096) {
        size += 4096;
        size /= 4096;
        size *= 4096;
    }
    uint8_t *buf = mmap(0, size, 3, 1, nvmap, 0);
    if(buf == -1)
        perror("mmap failed");

    printf("mmap: %x\n", buf);
    nv_mmap.handle = mem_handle;
    nv_mmap.offset = *offset;
    nv_mmap.length = size;
    nv_mmap.flags = 0;
    nv_mmap.addr = buf;

    ret = ioctl(nvmap, NVMAP_IOC_MMAP, &nv_mmap);

    if (ret < 0) {
        perror("nvmap mmap fail");
        exit(1);
    }

    *offset += size;

    return buf;

}

static void flip_page(void)
{
        __flip();

}

static int draw_slice(uint8_t * image[], int stride[], int w, int h,
		int x, int y) {


        uint8_t *dst;

        dst = video_buf[0];// + image_width * y + x;

	memcpy_pic(dst, image[0], w, h, image_width,
			stride[0]);

        dst = video_buf[1];
	memcpy_pic(dst, image[1], w, h, image_width/2,
			stride[1]);

        dst = video_buf[2];
	memcpy_pic(dst, image[2], w, h, image_width/2,
			stride[2]);


        /*
        x /= 2;
	y /= 2;
	w /= 2;
	h /= 2;

        dst = video_buf + image_width*image_height +
		(image_width/2)*y + x;

	memcpy_pic(dst, image[1], w, h, image_width/2,
			stride[1]);

        dst = video_buf + image_width*image_height*5/4 +
		(image_width/2)*y + x;
	memcpy_pic(dst, image[2], w, h, image_width/2,
			stride[2]);
*/

	return 0;
}

static int draw_frame(uint8_t * src[])
{
	return VO_ERROR;
}

static uint32_t draw_image(mp_image_t * mpi)
{
	if (mpi->flags & MP_IMGFLAG_DRAW_CALLBACK)
		return VO_TRUE;         // done
	else if (mpi->flags & MP_IMGFLAG_PLANAR)
	{
		//One plane per stride
		draw_slice(mpi->planes, mpi->stride, mpi->w, mpi->h, 0, 0);
		return VO_TRUE;
	} else if (mpi->flags & MP_IMGFLAG_YUV)
	{
		//Three planes in one stride
		return VO_TRUE;
	}
	
        return VO_FALSE;            // not (yet) supported
}

static int query_format(uint32_t format)
{
	int flag = VFCAP_CSP_SUPPORTED | VFCAP_CSP_SUPPORTED_BY_HW | VFCAP_HWSCALE_UP | VFCAP_HWSCALE_DOWN | VFCAP_OSD | VFCAP_ACCEPT_STRIDE;
	return flag;
}

static void uninit(void)
{
    printf("UNINIT\n");

    int ret;
    struct tegra_dc_ext_flip flip;
    bzero(&flip, sizeof flip);

    flip.win[0].index = 1;
    flip.win[1].index = -1;
    flip.win[2].index = -1;

    ret = ioctl(dc, TEGRA_DC_EXT_FLIP, &flip);

    if (ret < 0)
        perror("uninit failed");
}

static void flip_setup(int out_buff, int width, int height,
        int off_u, int off_v) {

    bzero(&flip, sizeof flip);
    flip.win[0].index = 1;

    flip.win[0].buff_id = out_buff;

    flip.win[0].offset_u = off_u;
    flip.win[0].offset_v = off_v;
    flip.win[0].stride = width;
    flip.win[0].stride_uv = width/2;
    flip.win[0].pixformat = 18;
    flip.win[0].w = width * 4096;
    flip.win[0].h = height * 4096;
    flip.win[0].out_w = fb_vinfo.xres;
    flip.win[0].out_h = fb_vinfo.yres;
    flip.win[0].z = 1;

    flip.win[1].index = -1;
    flip.win[2].index = -1;

}

static void __flip(void) {
    int ret;
    
    ret = ioctl(dc, TEGRA_DC_EXT_FLIP, &flip);

    if (ret < 0)
        perror("flip failed");

}

static void draw_osd(void)
{
    vo_draw_text(image_width, image_height, draw_alpha_yv12);
}

static void check_events(void)
{
}

static int preinit(const char *arg)
{
	const opt_t subopts[]= {
                {"output",   OPT_ARG_INT,       &output,       int_non_neg},
                { NULL }
	};

	if(subopt_parse(arg, subopts))
		return -1;

        printf("tegra dev=%d\n", output);
	return 0;
}

static int control(uint32_t request, void *data)
{
	switch (request)
	{
		case VOCTRL_QUERY_FORMAT:
			return query_format(*((uint32_t *) data));
		case VOCTRL_DRAW_IMAGE:
			return draw_image(data);
	}
	return VO_NOTIMPL;
}
