#define LOG_TAG "GRALLOC-ROCKCHIP"

#define LOG_TAG "mali_so"
#define ENABLE_DEBUG_LOG
#include <log/custom_log.h>


#include <cutils/log.h>
#include <stdlib.h>
#include <errno.h>
#include <drm.h>
#include <rockchip/rockchip_drmif.h>
#include "gralloc_helper.h"
#include "gralloc_drm.h"
#include "gralloc_drm_priv.h"
#if RK_DRM_GRALLOC
#include <cutils/properties.h>
#endif //end of RK_DRM_GRALLOC
#include <stdbool.h>

#define UNUSED(...) (void)(__VA_ARGS__)

struct dma_buf_sync {
        __u64 flags;
};

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)
#define DMA_BUF_SYNC_VALID_FLAGS_MASK \
        (DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END)
#define DMA_BUF_BASE            'b'
#define DMA_BUF_IOCTL_SYNC      _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

/* memory type definitions. */
enum drm_rockchip_gem_mem_type {
	/* Physically Continuous memory and used as default. */
	ROCKCHIP_BO_CONTIG	= 0 << 0,
	/* Physically Non-Continuous memory. */
	ROCKCHIP_BO_NONCONTIG	= 1 << 0,
	/* non-cachable mapping and used as default. */
	ROCKCHIP_BO_NONCACHABLE	= 0 << 1,
	/* cachable mapping. */
	ROCKCHIP_BO_CACHABLE	= 1 << 1,
	/* write-combine mapping. */
	ROCKCHIP_BO_WC		= 1 << 2,
	ROCKCHIP_BO_MASK	= ROCKCHIP_BO_NONCONTIG | ROCKCHIP_BO_CACHABLE |
					ROCKCHIP_BO_WC
};

struct rockchip_info {
	struct gralloc_drm_drv_t base;

	struct rockchip_device *rockchip;
	int fd;
};

struct rockchip_buffer {
	struct gralloc_drm_bo_t base;

	struct rockchip_bo *bo;
};

#if RK_DRM_GRALLOC

#define RK_GRALLOC_VERSION "1.0.6"
#define ARM_RELEASE_VER "r13p0-00rel0"

#if RK_DRM_GRALLOC_DEBUG
#ifndef AWAR
#define AWAR(fmt, args...) __android_log_print(ANDROID_LOG_WARN, "[Gralloc-Warning]", "%s:%d " fmt,__func__,__LINE__,##args)
#endif
#ifndef AINF
#define AINF(fmt, args...) __android_log_print(ANDROID_LOG_INFO, "[Gralloc]", fmt,##args)
#endif
#ifndef ADBG
#define ADBG(fmt, args...) __android_log_print(ANDROID_LOG_DEBUG, "[Gralloc-DEBUG]", fmt,##args)
#endif

#else

#ifndef AWAR
#define AWAR(fmt, args...)
#endif
#ifndef AINF
#define AINF(fmt, args...)
#endif
#ifndef ADBG
#define ADBG(fmt, args...)
#endif

#endif //end of RK_DRM_GRALLOC_DEBUG

#ifndef AERR
#define AERR(fmt, args...) __android_log_print(ANDROID_LOG_ERROR, "[Gralloc-ERROR]", "%s:%d " fmt,__func__,__LINE__,##args)
#endif
#ifndef AERR_IF
#define AERR_IF( eq, fmt, args...) if ( (eq) ) AERR( fmt, args )
#endif

#define GRALLOC_ALIGN( value, base ) (((value) + ((base) - 1)) & ~((base) - 1))
#define ODD_ALIGN(x, align)		(((x) % ((align) * 2) == 0) ? ((x) + (align)) : (x))
#define GRALLOC_ODD_ALIGN( value, base )   ODD_ALIGN(GRALLOC_ALIGN(value, base), base)

#endif

static void drm_gem_rockchip_free(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo);

static void drm_gem_rockchip_destroy(struct gralloc_drm_drv_t *drv)
{
	struct rockchip_info *info = (struct rockchip_info *)drv;

	if (info->rockchip)
		rockchip_device_destroy(info->rockchip);
	free(info);
}

static struct gralloc_drm_bo_t *drm_gem_rockchip_alloc(
		struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_handle_t *handle)
{
	struct rockchip_info *info = (struct rockchip_info *)drv;
	struct rockchip_buffer *buf;
	struct drm_gem_close args;
	uint32_t size, gem_handle;
#if  !RK_DRM_GRALLOC
        int ret, cpp, pitch, aligned_width, aligned_height;
#else
	int ret;
	int w, h, format, usage;
	size_t stride;
	int changeFromat = -1;
	int align = 8;
	int bpp = 0;
	int bpr = 0;
#endif //end of RK_DRM_GRALLOC
	uint32_t flags = 0;

	buf = calloc(1, sizeof(*buf));
	if (!buf) {
                ALOGE("Failed to allocate buffer wrapper\n");
		return NULL;
	}

#if !RK_DRM_GRALLOC
        cpp = gralloc_drm_get_bpp(handle->format);
        if (!cpp) {
                ALOGE("unrecognized format 0x%x", handle->format);
                return NULL;
        }

	aligned_width = handle->width;
	aligned_height = handle->height;
	gralloc_drm_align_geometry(handle->format,
			&aligned_width, &aligned_height);

	/* TODO: We need to sort out alignment */
	pitch = ALIGN(aligned_width * cpp, 64);
	size = aligned_height * pitch;

	if (handle->format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
		/*
		 * WAR for H264 decoder requiring additional space
		 * at the end of destination buffers.
		 */
		uint32_t w_mbs, h_mbs;

		w_mbs = ALIGN(handle->width, 16) / 16;
		h_mbs = ALIGN(handle->height, 16) / 16;
		size += 64 * w_mbs * h_mbs;
	}
#else
	format = handle->format;
	w = handle->width;
	h = handle->height;
	usage = handle->usage;
	if (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)
	{
	    if (usage & GRALLOC_USAGE_HW_VIDEO_ENCODER )
	    {
	        format = HAL_PIXEL_FORMAT_YCrCb_NV12;
	    }
	    else
	    {
	        changeFromat = format;
	        format = HAL_PIXEL_FORMAT_RGBX_8888;
	    }
	}

	if (format == HAL_PIXEL_FORMAT_YCrCb_420_SP || format == HAL_PIXEL_FORMAT_YV12
	        /* HAL_PIXEL_FORMAT_YCbCr_420_SP, HAL_PIXEL_FORMAT_YCbCr_420_P, HAL_PIXEL_FORMAT_YCbCr_422_I are not defined in Android.
	         * To enable Mali DDK EGLImage support for those formats, firstly, you have to add them in Android system/core/include/system/graphics.h.
	         * Then, define SUPPORT_LEGACY_FORMAT in the same header file(Mali DDK will also check this definition).
	         */
#ifdef SUPPORT_LEGACY_FORMAT
	        || format == HAL_PIXEL_FORMAT_YCbCr_420_SP || format == HAL_PIXEL_FORMAT_YCbCr_420_P || format == HAL_PIXEL_FORMAT_YCbCr_422_I
#endif
		|| format == HAL_PIXEL_FORMAT_YCrCb_NV12 || format == HAL_PIXEL_FORMAT_YCrCb_NV12_10
		|| format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO || format == HAL_PIXEL_FORMAT_YCbCr_420_888
	   )
	{
		switch (format)
		{
			case HAL_PIXEL_FORMAT_YCrCb_420_SP:
			case HAL_PIXEL_FORMAT_YCbCr_420_888:
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride + GRALLOC_ALIGN(stride / 2, 16);
				size = GRALLOC_ALIGN(h, 16) * bpr;
				break;

			case HAL_PIXEL_FORMAT_YV12:
#ifdef SUPPORT_LEGACY_FORMAT
			case HAL_PIXEL_FORMAT_YCbCr_420_P:
#endif
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride + GRALLOC_ALIGN(stride / 2, 16);
				size = GRALLOC_ALIGN(h, 2) * bpr;

				break;
#ifdef SUPPORT_LEGACY_FORMAT

			case HAL_PIXEL_FORMAT_YCbCr_420_SP:
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride + GRALLOC_ALIGN(stride / 2, 16);
				size = GRALLOC_ALIGN(h, 16) * bpr;
				break;

			case HAL_PIXEL_FORMAT_YCbCr_422_I:
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride * 2;
				size = h * bpr;

				break;
#endif
			case HAL_PIXEL_FORMAT_YCrCb_NV12:
				bpp = 2;
				bpr = (w * bpp + (align-1)) & (~(align-1));
				size = bpr * h;
				stride = bpr / bpp;
				break;
			case HAL_PIXEL_FORMAT_YCrCb_NV12_10:
				stride = w;
				bpr = stride * 2;
				size = h * bpr;
				break;
			case HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO:
				ALOGE("unsupport format [0x%x] now", HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO);
				break;

			default:
				return NULL;
		}
	}
	else
	{
		switch (format)
		{
			case HAL_PIXEL_FORMAT_RGBA_8888:
			case HAL_PIXEL_FORMAT_RGBX_8888:
			case HAL_PIXEL_FORMAT_BGRA_8888:
				bpp = 4;
				break;

			case HAL_PIXEL_FORMAT_RGB_888:
				bpp = 3;
				break;

			case HAL_PIXEL_FORMAT_RGB_565:
#if PLATFORM_SDK_VERSION < 19
			case HAL_PIXEL_FORMAT_RGBA_5551:
			case HAL_PIXEL_FORMAT_RGBA_4444:
#endif
				bpp = 2;
				break;

			default:
				return NULL;
		}

		bpr = GRALLOC_ALIGN(w * bpp, 64);
		size = bpr * h;
		stride = bpr / bpp;
	}
#endif
       if ( (usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN )
       {
               D("to ask for cachable buffer for CPU read, usage : 0x%x", usage);
               flags = ROCKCHIP_BO_CACHABLE;
       }

       if(format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
       {
               //set cache flag
               flags = ROCKCHIP_BO_CACHABLE;
       }

	if (handle->prime_fd >= 0) {
		ret = drmPrimeFDToHandle(info->fd, handle->prime_fd,
			&gem_handle);
		if (ret) {
			char *c = NULL;
			ALOGE("failed to convert prime fd to handle %d ret=%d",
				handle->prime_fd, ret);
			*c = 0;
			goto err;
		}
                ALOGV("Got handle %d for fd %d\n", gem_handle, handle->prime_fd);
		buf->bo = rockchip_bo_from_handle(info->rockchip, gem_handle,
			flags, size);
		if (!buf->bo) {
                        ALOGE("failed to wrap bo handle=%d size=%d\n",
				gem_handle, size);
			memset(&args, 0, sizeof(args));
			args.handle = gem_handle;
			drmIoctl(info->fd, DRM_IOCTL_GEM_CLOSE, &args);
			return NULL;
		}
	} else {
		buf->bo = rockchip_bo_create(info->rockchip, size, flags);
		if (!buf->bo) {
                        AERR("failed to allocate bo %dx%dx%dx%zd\n",
                                handle->height, stride, bpr, size);
			goto err;
		}

		gem_handle = rockchip_bo_handle(buf->bo);
		ret = drmPrimeHandleToFD(info->fd, gem_handle, 0,
			&handle->prime_fd);

		ALOGV("Got fd %d for handle %d\n", handle->prime_fd, gem_handle);
		if (ret) {
                        ALOGE("failed to get prime fd %d", ret);
			goto err_unref;
		}

		buf->base.fb_handle = gem_handle;
	}

#if RK_DRM_GRALLOC
	int private_usage = usage & (GRALLOC_USAGE_PRIVATE_0 |
	                                  GRALLOC_USAGE_PRIVATE_1);

	switch (private_usage)
	{
		case 0:
			handle->yuv_info = MALI_YUV_BT601_NARROW;
			break;

		case GRALLOC_USAGE_PRIVATE_1:
			handle->yuv_info = MALI_YUV_BT601_WIDE;
			break;

		case GRALLOC_USAGE_PRIVATE_0:
			handle->yuv_info = MALI_YUV_BT709_NARROW;
			break;

		case (GRALLOC_USAGE_PRIVATE_0 | GRALLOC_USAGE_PRIVATE_1):
			handle->yuv_info = MALI_YUV_BT709_WIDE;
			break;
	}

	handle->flags = PRIV_FLAGS_USES_ION;
        handle->stride = bpr;//pixel_stride;
        handle->pixel_stride = stride;
        handle->byte_stride = bpr;
        handle->format = changeFromat >= 0 ? changeFromat : format;
        handle->size = size;
        handle->offset = 0;
        handle->cpu_addr = rockchip_bo_map(buf->bo);
	if (!handle->cpu_addr) {
		ALOGE("cpu_addr: failed to map bo\n");
	}
#else
        handle->stride = pitch;
#endif
        handle->name = 0;
	buf->base.handle = handle;

        AINF("leave, w : %d, h : %d, format : 0x%x, usage : 0x%x. size=%d,pixel_stride=%d,byte_stride=%d",
                handle->width, handle->height, handle->format, handle->usage, handle->size,
                stride,bpr);
        AINF("leave: prime_fd=%d",handle->prime_fd);
	return &buf->base;

err_unref:
	rockchip_bo_destroy(buf->bo);
err:
	free(buf);
	return NULL;
}

static void drm_gem_rockchip_free(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	struct rockchip_buffer *buf = (struct rockchip_buffer *)bo;
        struct gralloc_drm_handle_t *gr_handle = gralloc_drm_handle((buffer_handle_t)bo->handle);

	UNUSED(drv);

        if (!gr_handle)
                return;

#if RK_DRM_GRALLOC
	if (gr_handle->prime_fd)
		close(gr_handle->prime_fd);

	gr_handle->prime_fd = -1;
#endif
        gralloc_drm_unlock_handle((buffer_handle_t)bo->handle);

	/* TODO: Is destroy correct here? */
	rockchip_bo_destroy(buf->bo);
	free(buf);
}

static int drm_gem_rockchip_map(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo, int x, int y, int w, int h,
		int enable_write, void **addr)
{
	struct rockchip_buffer *buf = (struct rockchip_buffer *)bo;
        struct dma_buf_sync sync_args;
        int ret = 0;

	UNUSED(drv, x, y, w, h, enable_write);

	*addr = rockchip_bo_map(buf->bo);
	if (!*addr) {
		ALOGE("failed to map bo\n");
		return -1;
	}

	if(buf && buf->bo && buf->bo->flags == ROCKCHIP_BO_CACHABLE)
	{
		sync_args.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
		ret = ioctl(bo->handle->prime_fd, DMA_BUF_IOCTL_SYNC, &sync_args);
		if (ret != 0)
			ALOGE("%s:DMA_BUF_IOCTL_SYNC failed", __FUNCTION__);
	}
	return 0;
}

static void drm_gem_rockchip_unmap(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
       struct rockchip_buffer *buf = (struct rockchip_buffer *)bo;
       struct dma_buf_sync sync_args;
       int ret = 0;

       UNUSED(drv);

       if(buf && buf->bo && buf->bo->flags == ROCKCHIP_BO_CACHABLE)
       {
               sync_args.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
               ioctl(bo->handle->prime_fd, DMA_BUF_IOCTL_SYNC, &sync_args);
               if (ret != 0)
                       ALOGE("%s:DMA_BUF_IOCTL_SYNC failed", __FUNCTION__);
       }
}

#if RK_DRM_GRALLOC
static int drm_init_version()
{
        char value[PROPERTY_VALUE_MAX];

        property_get("sys.ggralloc.version", value, "NULL");
        if(!strcmp(value,"NULL"))
        {
                property_set("sys.ggralloc.version", RK_GRALLOC_VERSION);
                ALOGD(RK_GRAPHICS_VER);
                ALOGD("gralloc ver '%s' on arm_release_ver '%s'.",
                        RK_GRALLOC_VERSION,
                        ARM_RELEASE_VER);
        }

        return 0;
}
#endif

struct gralloc_drm_drv_t *gralloc_drm_drv_create_for_rockchip(int fd)
{
	struct rockchip_info *info;
	int ret;

#if RK_DRM_GRALLOC
        drm_init_version();
#endif

	info = calloc(1, sizeof(*info));
	if (!info) {
		ALOGE("Failed to allocate rockchip gralloc device\n");
		return NULL;
	}

	info->rockchip = rockchip_device_create(fd);
	if (!info->rockchip) {
		ALOGE("Failed to create new rockchip instance\n");
		free(info);
		return NULL;
	}

	info->fd = fd;
	info->base.destroy = drm_gem_rockchip_destroy;
	info->base.alloc = drm_gem_rockchip_alloc;
	info->base.free = drm_gem_rockchip_free;
	info->base.map = drm_gem_rockchip_map;
	info->base.unmap = drm_gem_rockchip_unmap;

	return &info->base;
}
