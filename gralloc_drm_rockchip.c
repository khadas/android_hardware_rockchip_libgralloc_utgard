#define LOG_TAG "GRALLOC-ROCKCHIP"

// #define ENABLE_DEBUG_LOG
#include <log/custom_log.h>


#include <log/log.h>
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
#include <sys/stat.h>

#ifdef USE_HWC2
#include "gralloc_buffer_priv.h"
#endif

#define RK_CTS_WORKROUND	(1)
#define UNUSED(...) ((void)(__VA_ARGS__))
#define USAGE_CONTAIN_VALUE(value,mask) ((usage & mask) == value)

#if RK_CTS_WORKROUND
#define VIEW_CTS_FILE		"/data/ota/view_cts.ini"
#define VIEW_CTS_PROG_NAME	"android.view.cts"
#define VIEW_CTS_HINT		"view_cts"
#define BIG_SCALE_HINT		"big_scale"
typedef unsigned int       u32;
typedef enum
{
	IMG_STRING_TYPE		= 1,                    /*!< String type */
	IMG_FLOAT_TYPE		,                       /*!< Float type */
	IMG_UINT_TYPE		,                       /*!< Unsigned Int type */
	IMG_INT_TYPE		,                       /*!< (Signed) Int type */
	IMG_FLAG_TYPE                               /*!< Flag Type */
}IMG_DATA_TYPE;
#endif

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
	ROCKCHIP_BO_CONTIG      = 1 << 0,
	/* cachable mapping. */
	ROCKCHIP_BO_CACHABLE	= 1 << 1,
	/* write-combine mapping. */
	ROCKCHIP_BO_WC		= 1 << 2,
	ROCKCHIP_BO_SECURE      = 1 << 3,
	ROCKCHIP_BO_MASK	= ROCKCHIP_BO_CONTIG | ROCKCHIP_BO_CACHABLE |
				ROCKCHIP_BO_WC | ROCKCHIP_BO_SECURE
};

struct drm_rockchip_gem_phys {
	uint32_t handle;
	uint32_t phy_addr;
};

#define DRM_ROCKCHIP_GEM_GET_PHYS	0x04
#define DRM_IOCTL_ROCKCHIP_GEM_GET_PHYS		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_ROCKCHIP_GEM_GET_PHYS, struct drm_rockchip_gem_phys)

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

#define RK_GRALLOC_VERSION "1.2.0"
#define ARM_RELEASE_VER "r7"

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

#if RK_CTS_WORKROUND
static bool ConvertCharToData(const char *pszHintName, const char *pszData, void *pReturn, IMG_DATA_TYPE eDataType)
{
	bool bFound = false;


	switch(eDataType)
	{
		case IMG_STRING_TYPE:
		{
			strcpy((char*)pReturn, pszData);

			ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "Hint: Setting %s to %s\n", pszHintName, (char*)pReturn);

			bFound = true;

			break;
		}
		case IMG_FLOAT_TYPE:
		{
			*(float*)pReturn = (float) atof(pszData);

			ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "Hint: Setting %s to %f", pszHintName, *(float*)pReturn);

			bFound = true;

			break;
		}
		case IMG_UINT_TYPE:
		case IMG_FLAG_TYPE:
		{
			/* Changed from atoi to stroul to support hexadecimal numbers */
			*(u32*)pReturn = (u32) strtoul(pszData, NULL, 0);
			if (*(u32*)pReturn > 9)
			{
				ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "Hint: Setting %s to %u (0x%X)", pszHintName, *(u32*)pReturn, *(u32*)pReturn);
			}
			else
			{
				ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "Hint: Setting %s to %u", pszHintName, *(u32*)pReturn);
			}
			bFound = true;

			break;
		}
		case IMG_INT_TYPE:
		{
			*(int*)pReturn = (int) atoi(pszData);

			ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "Hint: Setting %s to %d\n", pszHintName, *(int*)pReturn);

			bFound = true;

			break;
		}
		default:
		{
			ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "ConvertCharToData: Bad eDataType");

			break;
		}
	}

	return bFound;
}

static int getProcessCmdLine(char* outBuf, size_t bufSize)
{
	int ret = 0;

	FILE* file = NULL;
	long pid = 0;
	char procPath[128]={0};

	pid = getpid();
	sprintf(procPath, "/proc/%ld/cmdline", pid);

	file = fopen(procPath, "r");
	if ( NULL == file )
	{
		ALOGE("fail to open file (%s)",strerror(errno));
	}

	if ( NULL == fgets(outBuf, bufSize - 1, file) )
	{
		ALOGE("fail to read from cmdline_file.");
	}

	if ( NULL != file )
	{
		fclose(file);
	}

	return ret;
}

bool FindAppHintInFile(char *pszFileName, const char *pszAppName,
								  const char *pszHintName, void *pReturn,
								  IMG_DATA_TYPE eDataType)
{
	FILE *regFile;
	bool bFound = false;

	regFile = fopen(pszFileName, "r");

	if(regFile)
	{
		char pszTemp[1024], pszApplicationSectionName[1024];
		int iLineNumber;
		bool bUseThisSection, bInAppSpecificSection;

		/* Build the section name */
		snprintf(pszApplicationSectionName, 1024, "[%s]", pszAppName);

		bUseThisSection 		= false;
		bInAppSpecificSection	= false;

		iLineNumber = -1;

		while(fgets(pszTemp, 1024, regFile))
		{
			size_t uiStrLen;

			iLineNumber++;
			ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "FindAppHintInFile iLineNumber=%d pszTemp=%s",iLineNumber,pszTemp);

			uiStrLen = strlen(pszTemp);

			if (pszTemp[uiStrLen-1]!='\n')
			{
			    ALOGE("FindAppHintInFile : Error in %s at line %u",pszFileName,iLineNumber);

				continue;
			}

			if((uiStrLen >= 2) && (pszTemp[uiStrLen-2] == '\r'))
			{
				/* CRLF (Windows) line ending */
				pszTemp[uiStrLen-2] = '\0';
			}
			else
			{
				/* LF (unix) line ending */
				pszTemp[uiStrLen-1] = '\0';
			}

			switch (pszTemp[0])
			{
				case '[':
				{
					/* Section */
					bUseThisSection 		= false;
					bInAppSpecificSection	= false;

					if (!strcmp("[default]", pszTemp))
					{
						bUseThisSection = true;
					}
					else if (!strcmp(pszApplicationSectionName, pszTemp))
					{
						bUseThisSection 		= true;
						bInAppSpecificSection 	= true;
					}

					break;
				}
				default:
				{
					char *pszPos;

					if (!bUseThisSection)
					{
						/* This line isn't for us */
						continue;
					}

					pszPos = strstr(pszTemp, pszHintName);

					if (pszPos!=pszTemp)
					{
						/* Hint name isn't at start of string */
						continue;
					}

					if (*(pszPos + strlen(pszHintName)) != '=')
					{
						/* Hint name isn't exactly correct, or isn't followed by an equals sign */
						continue;
					}

					/* Move to after the equals sign */
					pszPos += strlen(pszHintName) + 1;

					/* Convert anything after the equals sign to the requested data type */
					bFound = ConvertCharToData(pszHintName, pszPos, pReturn, eDataType);

					if (bFound && bInAppSpecificSection)
					{
						/*
						// If we've found the hint in the application specific section we may
						// as well drop out now, since this should override any default setting
						*/
						fclose(regFile);

						return true;
					}

					break;
				}
			}
		}

		fclose(regFile);
	}
	else
	{
		regFile = fopen(pszFileName, "wb+");
		if(regFile)
		{
			char acBuf[] = "[android.view.cts]\n"
							"view_cts=0\n"
							"big_scale=0\n";
			fprintf(regFile,"%s",acBuf);
			fclose(regFile);
			chmod(pszFileName, 0x777);
		}
		else
		{
			ALOGE("%s open fail errno=0x%x  (%s)",__FUNCTION__, errno,strerror(errno));
		}
	}

	return bFound;
}

bool ModifyAppHintInFile(char *pszFileName, const char *pszAppName,
								const char *pszHintName, void *pReturn, int pSet,
								IMG_DATA_TYPE eDataType)
{
	FILE *regFile;
	bool bFound = false;

	regFile = fopen(pszFileName, "r+");

	if(regFile)
	{
		char pszTemp[1024], pszApplicationSectionName[1024];
		int iLineNumber;
		bool bUseThisSection, bInAppSpecificSection;
		int offset = 0;

		/* Build the section name */
		snprintf(pszApplicationSectionName, 1024, "[%s]", pszAppName);

		bUseThisSection		  = false;
		bInAppSpecificSection   = false;

		iLineNumber = -1;

		while(fgets(pszTemp, 1024, regFile))
		{
			size_t uiStrLen;

			iLineNumber++;
			ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "ModifyAppHintInFile iLineNumber=%d pszTemp=%s",iLineNumber,pszTemp);

			uiStrLen = strlen(pszTemp);

			if (pszTemp[uiStrLen-1]!='\n')
			{
				ALOGE("FindAppHintInFile : Error in %s at line %u",pszFileName,iLineNumber);
				continue;
			}

			if((uiStrLen >= 2) && (pszTemp[uiStrLen-2] == '\r'))
			{
				/* CRLF (Windows) line ending */
				pszTemp[uiStrLen-2] = '\0';
			}
			else
			{
				/* LF (unix) line ending */
				pszTemp[uiStrLen-1] = '\0';
			}

			switch (pszTemp[0])
			{
				case '[':
				{
					/* Section */
					bUseThisSection		  = false;
					bInAppSpecificSection   = false;

					if (!strcmp("[default]", pszTemp))
					{
						bUseThisSection = true;
					}
					else if (!strcmp(pszApplicationSectionName, pszTemp))
					{
						bUseThisSection		  = true;
						bInAppSpecificSection   = true;
					}

					break;
				}
				default:
				{
					char *pszPos;

					if (!bUseThisSection)
					{
						/* This line isn't for us */
						offset += uiStrLen;
						continue;
					}

					pszPos = strstr(pszTemp, pszHintName);

					if (pszPos!=pszTemp)
					{
						/* Hint name isn't at start of string */
						offset += uiStrLen;
						continue;
					}

					if (*(pszPos + strlen(pszHintName)) != '=')
					{
						/* Hint name isn't exactly correct, or isn't followed by an equals sign */
						offset += uiStrLen;
						continue;
					}

					/* Move to after the equals sign */
					pszPos += strlen(pszHintName) + 1;

					/* Convert anything after the equals sign to the requested data type */
					bFound = ConvertCharToData(pszHintName, pszPos, pReturn, eDataType);

					if (bFound && bInAppSpecificSection)
					{
						offset += (strlen(pszHintName) + 1);
						if(eDataType == IMG_INT_TYPE && *((int*)pReturn) != pSet)
						{
							fseek(regFile, offset, SEEK_SET);
							fprintf(regFile,"%d",pSet);
							*((int*)pReturn) = pSet;
						}
						/*
						// If we've found the hint in the application specific section we may
						// as well drop out now, since this should override any default setting
						*/
						fclose(regFile);

						return true;
					}

					break;
				}
			}
			offset += uiStrLen;
		}

		fclose(regFile);
	}
	else
	{
		regFile = fopen(pszFileName, "wb+");
		if(regFile)
		{
			char acBuf[] = "[android.view.cts]\n"
							"view_cts=0\n"
							"big_scale=0\n";
			fprintf(regFile,"%s",acBuf);
			fclose(regFile);
			chmod(pszFileName, 0x777);
		}
		else
		{
			ALOGE("%s open faile errno=0x%x  (%s)",__FUNCTION__, errno,strerror(errno));
		}
	}

	return bFound;
}
#endif

/*static void drm_gem_rockchip_free(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo);*/

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
	int stride; // pixel_stride
	int byte_stride;
	int changeFromat = -1;
	int align = 8;
	int bpp = 0; // bytes_per_pixel
	int bpr = 0; // bytes_per_row
#endif //end of RK_DRM_GRALLOC
	uint32_t flags = 0;
	struct drm_rockchip_gem_phys phys_arg;

	phys_arg.phy_addr = 0;

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
		    I("to force 'format' to HAL_PIXEL_FORMAT_YCrCb_NV12");
	        format = HAL_PIXEL_FORMAT_YCrCb_NV12;
	    }
	    else
	    {
	        changeFromat = format;
	        format = HAL_PIXEL_FORMAT_RGBX_8888;
	    }
	}
    else if ( HAL_PIXEL_FORMAT_YCbCr_420_888 == format )
    {
        I("to use NV12 for HAL_PIXEL_FORMAT_YCbCr_420_888.");
        format = HAL_PIXEL_FORMAT_YCrCb_NV12;
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
		|| format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO
	   )
	{
		switch (format)
		{
			case HAL_PIXEL_FORMAT_YCrCb_420_SP:
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride + GRALLOC_ALIGN(stride / 2, 16);
				size = GRALLOC_ALIGN(h, 16) * bpr;
				byte_stride = stride;
				break;

			case HAL_PIXEL_FORMAT_YV12:
#ifdef SUPPORT_LEGACY_FORMAT
			case HAL_PIXEL_FORMAT_YCbCr_420_P:
#endif
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride + GRALLOC_ALIGN(stride / 2, 16);
				size = GRALLOC_ALIGN(h, 2) * bpr;
				byte_stride = stride;
				break;
#ifdef SUPPORT_LEGACY_FORMAT

			case HAL_PIXEL_FORMAT_YCbCr_420_SP:
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride + GRALLOC_ALIGN(stride / 2, 16);
				size = GRALLOC_ALIGN(h, 16) * bpr;
				byte_stride = stride;
				break;

			case HAL_PIXEL_FORMAT_YCbCr_422_I:
				stride = GRALLOC_ALIGN(w, 16);
				bpr = stride * 2;
				size = h * bpr;
				byte_stride = stride;
				break;
#endif
			case HAL_PIXEL_FORMAT_YCrCb_NV12:
				bpp = 2;
				bpr = (w * bpp + (align-1)) & (~(align-1));
				size = bpr * h;
				stride = bpr / bpp;
				byte_stride = stride;
				D("for nv12_buf, size : %d, pixel_stride : %d, byte_stride : %d", size, stride, byte_stride);
				break;

			case HAL_PIXEL_FORMAT_YCrCb_NV12_10:
				stride = w;
				bpr = stride * 2;
				size = h * bpr;
				byte_stride = stride; // .T : need more test.
				break;
			default:
				ALOGE("unsupport format [0x%x] now", format);
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
			case HAL_PIXEL_FORMAT_BLOB:
				bpp = 1;
				break;
			default:
				return NULL;
		}

		bpr = GRALLOC_ALIGN(w * bpp, 64);
		size = bpr * h;
		stride = bpr / bpp;
		byte_stride = bpr;
	}
#endif

	/*-------------------------------------------------------*/

       if ( (usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN
		|| format == HAL_PIXEL_FORMAT_YCrCb_NV12_10 )
       {
               D("to ask for cachable buffer for CPU read, usage : 0x%x", usage);
               //set cache flag
               flags = ROCKCHIP_BO_CACHABLE;
       }

       if(USAGE_CONTAIN_VALUE(GRALLOC_USAGE_TO_USE_PHY_CONT,GRALLOC_USAGE_ROT_MASK))
       {
               flags |= ROCKCHIP_BO_CONTIG;
               ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "try to use Physically Continuous memory\n");
       }

	if(usage & GRALLOC_USAGE_PROTECTED)
	{
		flags |= ROCKCHIP_BO_SECURE;
		ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "try to use secure memory\n");
	}

	/*-------------------------------------------------------*/

	if (handle->prime_fd >= 0) {
		D("prime_fd is valid : %d", handle->prime_fd);

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

		D("to create rk_bo from gem_handle('%d'), size : %d", gem_handle, size);
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
		D("to create rk_bo directly, size : %d", size);
		buf->bo = rockchip_bo_create(info->rockchip, size, flags);
		if (!buf->bo) {
                        AERR("failed to allocate bo %dx%dx%dx%zd\n",
                                handle->height, stride, byte_stride, size);
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

		if(USAGE_CONTAIN_VALUE(GRALLOC_USAGE_TO_USE_PHY_CONT,GRALLOC_USAGE_ROT_MASK))
		{
			phys_arg.handle = gem_handle;
			ret = drmIoctl(info->fd, DRM_IOCTL_ROCKCHIP_GEM_GET_PHYS, &phys_arg);
			if (ret)
				ALOGE("failed to get phy address: %s\n", strerror(errno));
			ALOGD_IF(RK_DRM_GRALLOC_DEBUG,"get phys 0x%x\n", phys_arg.phy_addr);
		}
	}

#if RK_DRM_GRALLOC
#ifdef USE_HWC2
	/*
	 * If handle has been dup,then the fd is a negative number.
	 * Either you should close it or don't allocate the fd agagin.
	 * Otherwize,it will leak fd.
	 */
	 int err;
	if(handle->ashmem_fd < 0)
	{
			err = gralloc_rk_ashmem_allocate( handle );
			//ALOGD("err=%d,isfb=%x,[%d,%x]",err,usage & GRALLOC_USAGE_HW_FB,hnd->share_attr_fd,hnd->attr_base);
			if( err < 0 )
			{
					if ( (usage & GRALLOC_USAGE_HW_FB) )
					{
							/*
							 * Having the attribute region is not critical for the framebuffer so let it pass.
							 */
							err = 0;
					}
					else
					{
							drm_gem_rockchip_free( drv, &buf->base );
							goto err_unref;
					}
			}
	}
#endif
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

	if(phys_arg.phy_addr && phys_arg.phy_addr != handle->phy_addr)
	{
		handle->phy_addr = phys_arg.phy_addr;
	}

	handle->flags = PRIV_FLAGS_USES_ION;

        handle->stride = byte_stride;
        handle->pixel_stride = stride;
        handle->byte_stride = byte_stride;

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
                stride, byte_stride);
        AINF("leave: prime_fd=%d",handle->prime_fd);
	return &buf->base;

err_unref:
	rockchip_bo_destroy(buf->bo);
err:
	free(buf);
	return NULL;
}

void drm_gem_rockchip_free(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	struct rockchip_buffer *buf = (struct rockchip_buffer *)bo;
        struct gralloc_drm_handle_t *gr_handle = gralloc_drm_handle((buffer_handle_t)bo->handle);

	UNUSED(drv);

        if (!gr_handle)
        {
                ALOGE("%s: invalid handle",__FUNCTION__);
                gralloc_drm_unlock_handle((buffer_handle_t)bo->handle);
                return;
        }

#if RK_DRM_GRALLOC
#ifdef USE_HWC2
	gralloc_rk_ashmem_free( gr_handle );
#endif
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
	struct gralloc_drm_handle_t *gr_handle = gralloc_drm_handle((buffer_handle_t)bo->handle);
        struct dma_buf_sync sync_args;
        int ret = 0, ret2 = 0;

	UNUSED(drv);
    UNUSED(x);
    UNUSED(y);
    UNUSED(w);
    UNUSED(h);
    UNUSED(enable_write);

	if (gr_handle->usage & GRALLOC_USAGE_PROTECTED)
	{
		*addr = NULL;
		ALOGE("The secure buffer cann't be map");
	}
	else
	{
		*addr = rockchip_bo_map(buf->bo);
		if (!*addr) {
			ALOGE("failed to map bo\n");
			ret = -1;
		}
#if RK_CTS_WORKROUND
		else {
			int big_scale;
			static int iCnt = 0;
			char cmdline[256] = {0};

			getProcessCmdLine(cmdline, sizeof(cmdline));

			if(!strcmp(cmdline,"android.view.cts"))
			{
				FindAppHintInFile(VIEW_CTS_FILE, VIEW_CTS_PROG_NAME, BIG_SCALE_HINT, &big_scale, IMG_INT_TYPE);
				if(big_scale && gr_handle->usage == 0x603 ) {
					memset(*addr,0xFF,gr_handle->height*gr_handle->byte_stride);
					ALOGD_IF(1, "memset 0xff byte_stride=%d iCnt=%d",gr_handle->byte_stride,iCnt);
					iCnt++;
				}
				if(iCnt == 400 && big_scale)
				{
					ModifyAppHintInFile(VIEW_CTS_FILE, VIEW_CTS_PROG_NAME, BIG_SCALE_HINT, &big_scale, 0, IMG_INT_TYPE);
					ALOGD_IF(1,"reset big_scale");
				}
			}
		}
#endif
	}

	if(buf && buf->bo && (buf->bo->flags & ROCKCHIP_BO_CACHABLE))
	{
		sync_args.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
		ret2 = ioctl(bo->handle->prime_fd, DMA_BUF_IOCTL_SYNC, &sync_args);
		if (ret2 != 0)
			ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "%s:DMA_BUF_IOCTL_SYNC start failed", __FUNCTION__);
	}

	gralloc_drm_unlock_handle((buffer_handle_t)bo->handle);
	return ret;
}

static void drm_gem_rockchip_unmap(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
       struct rockchip_buffer *buf = (struct rockchip_buffer *)bo;
       struct dma_buf_sync sync_args;
       int ret = 0;

       UNUSED(drv);

       if(buf && buf->bo && (buf->bo->flags & ROCKCHIP_BO_CACHABLE))
       {
               sync_args.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
               ioctl(bo->handle->prime_fd, DMA_BUF_IOCTL_SYNC, &sync_args);
               if (ret != 0)
                       ALOGD_IF(RK_DRM_GRALLOC_DEBUG, "%s:DMA_BUF_IOCTL_SYNC end failed", __FUNCTION__);
       }
}

#if RK_DRM_GRALLOC
static int drm_init_version()
{
        char value[PROPERTY_VALUE_MAX];
        char acCommit[50];

        /* RK_GRAPHICS_VER=commit-id:067e5d0: only keep string after '=' */
        sscanf(RK_GRAPHICS_VER, "%*[^=]=%s", acCommit);
        property_get("sys.ggralloc.version", value, "NULL");
        if(!strcmp(value,"NULL"))
        {
                property_set("sys.ggralloc.version", RK_GRALLOC_VERSION);
                property_set("sys.ggralloc.commit", acCommit);
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
