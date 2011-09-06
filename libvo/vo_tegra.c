#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stropts.h>
#include <sys/mman.h>

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

static uint32_t image_width;
static uint32_t image_height;


static const vo_info_t info = {
	"Tegra",
	"tegra",
	"Ilya Petrov <ilya.muromec@gmail.com>",
	""
};

const LIBVO_EXTERN(tegra);

static int hdmi;

static int dc, nvmap, out_buff, out_addr;
uint8_t *video_buf;

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

        image_height = height;
	image_width = width;
        printf("width = %x\n", image_width);

        dc = open("/dev/tegra_dc_1", O_RDWR);
        if(dc < 0) {
            perror("cant open dc");
            exit(1);
        }

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
        map.size = 0x967b0;

        ret = ioctl(nvmap, NVMAP_IOC_CREATE, &map);
        if (ret < 0) {
            perror("nvmap create fail");
            exit(1);
        }

        struct nvmap_alloc_handle ah;
        ah.handle = map.handle;
        ah.heap_mask = 1;
        ah.flags = 1;
        ah.align = 0x100;

        ret = ioctl(nvmap, NVMAP_IOC_ALLOC, &ah);
        if (ret < 0) {
            perror("nvmap alloc fail");
            exit(1);
        }

        struct nvmap_pin_handle pin;
        pin.handles = map.handle;
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
        
        printf("mmap: %x\n", video_buf);

        video_buf = mmap(0, 0x64000, 3, 1, nvmap, 0);
        if(video_buf == -1)
            perror("mmap failed");

        printf("mmap: %x\n", video_buf);

        struct nvmap_map_caller nv_mmap;
        nv_mmap.handle = map.handle;
        nv_mmap.offset = 0;
        nv_mmap.length = 0x64000;
        nv_mmap.flags = 0;
        nv_mmap.addr = video_buf;

        ret = ioctl(nvmap, NVMAP_IOC_MMAP, &nv_mmap);

        if (ret < 0) {
            perror("nvmap mmap fail");
            exit(1);
        }


	return 0;
}

static void flip_page(void)
{
        __flip();

}

static int draw_slice(uint8_t * image[], int stride[], int w, int h,
		int x, int y) {


        uint8_t *dst;

        dst = video_buf;// + image_width * y + x;

	memcpy_pic(dst, image[0], w, h, image_width,
			stride[0]);

        dst += image_width * image_height;
	memcpy_pic(dst, image[1], w, h, image_width/2,
			stride[1]);

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

static void __flip(void) {
    int ret;
    struct tegra_dc_ext_flip flip;
    bzero(&flip, sizeof flip);

    flip.win[0].index = 1;
    flip.win[0].buff_id = out_buff;
    flip.win[0].offset_u = 408064;
    flip.win[0].offset_v = 512256;
    flip.win[0].stride = 848;
    flip.win[0].stride_uv = 432;
    flip.win[0].pixformat = 18;
    flip.win[0].w = 3473408;
    flip.win[0].h = 1966080;
    flip.win[0].out_w = 1400;
    flip.win[0].out_h = 900;
    flip.win[0].z = 1;

    flip.win[1].index = -1;
    flip.win[2].index = -1;

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

static int test_dst_str( void * arg ) {
	strarg_t * strarg = (strarg_t *)arg;

	if (
			strargcmp( strarg, "hdmi" ) == 0 ||
			strargcmp( strarg, "lcd" ) == 0 ||
			strargcmp( strarg, "both" ) == 0
	   ) {
		return 1;
	}

	return 0;
}

static int preinit(const char *arg)
{
	strarg_t dst_str={0, NULL};
	const opt_t subopts[]= {
		{ "dst",        OPT_ARG_STR,    &dst_str,       test_dst_str },
		{ NULL }
	};

	if(subopt_parse(arg, subopts))
		return -1;

	if(dst_str.str && strcmp( dst_str.str, "hdmi" )==0)
		hdmi=1;
	else
		hdmi=0;

        printf("hdmi=%d\n", hdmi);
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
