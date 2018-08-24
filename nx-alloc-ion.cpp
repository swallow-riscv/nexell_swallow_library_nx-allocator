/*
 * Copyright (C) 2017  Nexell Co., Ltd.
 * Author: Sungwon Jo <doriya@nexell.co.kr>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "include/linux/ion/ion.h"
#include <linux/videodev2.h>
#include "include/linux/videodev2_nxp_media.h"
#include "nx-alloc.h"

#define ION_DEVICE_NAME			"/dev/ion"
#define ION_HEAP_TYPE_MASK		ION_HEAP_TYPE_DMA_MASK

#ifndef ALIGN
#define	ALIGN(X,N)		( (X+N-1) & (~(N-1)) )
#endif

#ifndef ALIGNED16
#define	ALIGNED16(X)	ALIGN(X,16)
#endif

static int ion_ioctl(int fd, int req, void *arg)
{
	int ret = ioctl(fd, req, arg);
	if (ret < 0) {
		printf("ioctl %x failed with code %d: %s\n", req,
			  ret, strerror(errno));
		return -errno;
	}
	return ret;
}

static int ion_alloc(int fd, size_t len, size_t align, unsigned int heap_mask,
			  unsigned int flags, ion_user_handle_t *handle)
{
	int ret;
	struct ion_allocation_data data = {
		.len = len,
		.align = align,
		.heap_id_mask = heap_mask,
		.flags = flags,
	};
	if (handle == NULL)
		return -EINVAL;
	ret = ion_ioctl(fd, ION_IOC_ALLOC, &data);
	if (ret < 0)
		return ret;
	*handle = data.handle;
	return ret;
}

static int ion_free(int fd, ion_user_handle_t handle)
{
	struct ion_handle_data data = {
		.handle = handle,
	};
	return ion_ioctl(fd, ION_IOC_FREE, &data);
}

// static int ion_map(int fd, ion_user_handle_t handle, size_t length, int prot,
// 			int flags, off_t offset, unsigned char **ptr, int *map_fd)
// {
// 	int ret;
// 	unsigned char *tmp_ptr;
// 	struct ion_fd_data data = {
// 		.handle = handle,
// 	};
// 	if (map_fd == NULL)
// 		return -EINVAL;
// 	if (ptr == NULL)
// 		return -EINVAL;
// 	ret = ion_ioctl(fd, ION_IOC_MAP, &data);
// 	if (ret < 0)
// 		return ret;
// 	if (data.fd < 0) {
// 		printf("map ioctl returned negative fd\n");
// 		return -EINVAL;
// 	}
// 	tmp_ptr = mmap(NULL, length, prot, flags, data.fd, offset);
// 	if (tmp_ptr == MAP_FAILED) {
// 		printf("mmap failed: %s\n", strerror(errno));
// 		return -errno;
// 	}
// 	*map_fd = data.fd;
// 	*ptr = tmp_ptr;
// 	return ret;
// }

static int ion_share(int fd, ion_user_handle_t handle, int *share_fd)
{
	int ret;
	struct ion_fd_data data = {
		.handle = handle,
	};
	if (share_fd == NULL)
		return -EINVAL;
	ret = ion_ioctl(fd, ION_IOC_SHARE, &data);
	if (ret < 0)
		return ret;
	if (data.fd < 0) {
		printf("share ioctl returned negative fd\n");
		return -EINVAL;
	}
	*share_fd = data.fd;
	return ret;
}

static int ion_alloc_fd(int fd, size_t len, size_t align, unsigned int heap_mask,
				 unsigned int flags, int *handle_fd) {
	ion_user_handle_t handle;
	int ret;
	ret = ion_alloc(fd, len, align, heap_mask, flags, &handle);
	if (ret < 0)
		return ret;
	ret = ion_share(fd, handle, handle_fd);
	ion_free(fd, handle);
	return ret;
}

static int32_t IsContinuousPlanes( uint32_t fourcc )
{
	switch (fourcc)
	{
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
	case V4L2_PIX_FMT_YUV444M:
	case V4L2_PIX_FMT_NV24M:
	case V4L2_PIX_FMT_NV42M:
		return 0;
	}

	return 1;
}

//	Nexell Memory Allocator Wrapper
//
//	Suport Format & Planes
//		YUV420 Format :
//			1 Plane : I420, NV12
//			2 Plane : NV12
//			3 Plane : I420
//
NX_MEMORY_INFO * NX_AllocateMemory( int width, int height, int32_t planes, uint32_t format, int align )
{
	int sharedFd[NX_MAX_PLANES] = {0, };
	int32_t i=0;
	int32_t luStride, cStride;
	int32_t luVStride, cVStride;
	int32_t stride[NX_MAX_PLANES];
	int32_t size[NX_MAX_PLANES];
	int ret;

	NX_MEMORY_INFO *pVidMem;

	int ionFd = open( ION_DEVICE_NAME, O_RDWR );
	if( 0 > ionFd ) {
		printf("failed to open ion device:%d\n", ionFd);
		return NULL;
	}

	//	Luma
	luStride = ALIGN(width, 32);
	luVStride = ALIGN(height, 16);

	//	Chroma
	switch (format)
	{
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YVU420M:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV21M:
		cStride = luStride/2;
		cVStride = ALIGN(height/2, 16);
		break;

	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV61M:
		cStride = luStride/2;
		cVStride = luVStride;
		break;

	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_YUV444M:
	case V4L2_PIX_FMT_NV24:
	case V4L2_PIX_FMT_NV24M:
	case V4L2_PIX_FMT_NV42:
	case V4L2_PIX_FMT_NV42M:
		cStride = luStride;
		cVStride = luVStride;
		break;

	case V4L2_PIX_FMT_GREY:
		cStride = 0;
		cVStride = 0;
		break;

	default:
		printf("Unknown format type\n");
		goto ErrorExit;
	}

	//	Decide Memory Size
	switch (planes)
	{
	case 1:
		size[0] = luStride*luVStride + cStride*cVStride*2;
		stride[0] = luStride;

		ret = ion_alloc_fd( ionFd, size[0], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[0] );
		if( 0 > ret ) goto ErrorExit;

		break;

	case 2:
		size[0] = luStride*luVStride;
		stride[0] = luStride;

		size[1] = cStride*cVStride*2;
		stride[1] = cStride * 2;

		if( IsContinuousPlanes(format) )
		{
			ret = ion_alloc_fd( ionFd, size[0] + size[1], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[0] );
			if( 0 > ret ) goto ErrorExit;

			sharedFd[1] = sharedFd[0];
		}
		else
		{
			ret = ion_alloc_fd( ionFd, size[0], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[0] );
			if( 0 > ret ) goto ErrorExit;

			ret = ion_alloc_fd( ionFd, size[1], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[1] );
			if( 0 > ret ) goto ErrorExit;
		}
		break;

	case 3:
		size[0] = luStride*luVStride;
		stride[0] = luStride;

		size[1] = cStride*cVStride;
		stride[1] = cStride;

		size[2] = cStride*cVStride;
		stride[2] = cStride;

		if( IsContinuousPlanes(format) )
		{
			ret = ion_alloc_fd( ionFd, size[0] + size[1] + size[2], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[0] );
			if( 0 > ret ) goto ErrorExit;

			sharedFd[1] =
			sharedFd[2] = sharedFd[0];
		}
		else
		{
			ret = ion_alloc_fd( ionFd, size[0], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[0] );
			if( 0 > ret ) goto ErrorExit;

			ret = ion_alloc_fd( ionFd, size[1], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[1] );
			if( 0 > ret ) goto ErrorExit;

			ret = ion_alloc_fd( ionFd, size[2], align, ION_HEAP_TYPE_MASK, 0, &sharedFd[2] );
			if( 0 > ret ) goto ErrorExit;
		}
		break;

	default:
		break;
	}

	pVidMem = (NX_MEMORY_INFO *)calloc(1, sizeof(NX_MEMORY_INFO));
	pVidMem->width = width;
	pVidMem->height = height;
	pVidMem->align = align;
	pVidMem->planes = planes;
	pVidMem->format = format;
	for( i=0 ; i<planes ; i++ )
	{
		pVidMem->sharedFd[i] = sharedFd[i];
		pVidMem->size[i] = size[i];
		pVidMem->stride[i] = stride[i];
	}
	printf("size = %d.\n", pVidMem->size[0] + pVidMem->size[1] + pVidMem->size[2]);
	close( ionFd );
	return pVidMem;

ErrorExit:
	printf("[%s] Error Exit\n", __func__);
	for( i=0 ; i<planes ; i++ )
	{
		if( sharedFd[i] > 0 )
		{
			close( sharedFd[i] );
		}
	}

	if( 0 < ionFd )
	{
		close( ionFd );
	}

	return NULL;
}

void NX_FreeMemory( NX_MEMORY_INFO * pMem )
{
	int32_t i;
	if( pMem )
	{
		NX_UnmapMemory( pMem );

		for( i=0; i < pMem->planes ; i++ )
		{
			close(pMem->sharedFd[i]);
		}
		free( pMem );
	}
}


//
//	Mapping/Unmapping Virtual Memory
//
int NX_MapMemory( NX_MEMORY_INFO *pMem )
{
	int32_t i, size=0;
	void *pBuf;

	if( !pMem )
	{
		return -1;
	}

	if( IsContinuousPlanes(pMem->format) )
	{
		for( i=0 ; i < pMem->planes; i ++ )
		{
			size += pMem->size[i];
		}

		for( i=0 ; i < pMem->planes; i ++ )
		{
			pBuf = (i < 1 ) ?
				mmap( 0, size, PROT_READ|PROT_WRITE, MAP_SHARED, pMem->sharedFd[i], 0 ) :
				(uint8_t*)pMem->pBuffer[i-1] + pMem->size[i-1];

			if( pBuf == MAP_FAILED )
			{
				return -1;
			}

			pMem->pBuffer[i] = pBuf;
		}
	}
	else
	{
		for( i=0 ; i < pMem->planes; i ++ )
		{
			pBuf = mmap( 0, pMem->size[i], PROT_READ|PROT_WRITE, MAP_SHARED, pMem->sharedFd[i], 0 );

			if( pBuf == MAP_FAILED )
			{
				return -1;
			}

			pMem->pBuffer[i] = pBuf;
		}
	}

	return 0;
}

int NX_UnmapMemory( NX_MEMORY_INFO *pMem )
{
	int32_t i, size=0;
	if( !pMem )
	{
		return -1;
	}

	if( IsContinuousPlanes(pMem->format) )
	{
		for( i=0 ; i < pMem->planes; i ++ )
		{
			size += pMem->size[i];
		}

		if( pMem->pBuffer[0] )
		{
			munmap( pMem->pBuffer[0], size );
		}
		else
		{
			return -1;
		}
	}
	else {
		for( i=0; i < pMem->planes ; i++ )
		{
			if( pMem->pBuffer[i] )
			{
				munmap( pMem->pBuffer[i], pMem->size[i] );
			}
			else
			{
				return -1;
			}
		}
	}

	return 0;
}

#ifdef ANDROID
int NX_PrivateHandleToMemory( struct private_handle_t const *pHandle, NX_MEMORY_INFO *pMemInfo )
{
	int ret;
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr ycbcr;

	ret = module->lock_ycbcr(module, pHandle, PROT_READ | PROT_WRITE, 0, 0,
		pHandle->width, pHandle->height, &ycbcr);

	if( ret )
	{
		printf("Fail, lock_ycbcr().\n");
		return ret;
	}

	memset( pMemInfo, 0x00, sizeof(NX_MEMORY_INFO) );
	pMemInfo->width       = pHandle->width;
	pMemInfo->height      = pHandle->height;
	pMemInfo->align       = 4096;
	pMemInfo->planes      = 3;

	pMemInfo->sharedFd[0] = pHandle->share_fd;
	pMemInfo->sharedFd[1] = pHandle->share_fd;
	pMemInfo->sharedFd[2] = pHandle->share_fd;

	switch( pHandle->format )
	{
	case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
	case HAL_PIXEL_FORMAT_YCbCr_420_888:
		pMemInfo->format    = V4L2_PIX_FMT_YUV420;

		pMemInfo->stride[0] = (int32_t)ycbcr.ystride;
		pMemInfo->size[0]   = (int32_t)((uint64_t)ycbcr.cb - (uint64_t)ycbcr.y);
		pMemInfo->stride[1] =
		pMemInfo->stride[2] = (int32_t)ycbcr.cstride;
		pMemInfo->size[1]   =
		pMemInfo->size[2]   = (int32_t)((uint64_t)ycbcr.cr - (uint64_t)ycbcr.cb);
		break;

	case HAL_PIXEL_FORMAT_YV12:
		pMemInfo->format    = V4L2_PIX_FMT_YVU420;

		pMemInfo->stride[0] = (int32_t)ycbcr.ystride;
		pMemInfo->size[0]   = (int32_t)((uint64_t)ycbcr.cr - (uint64_t)ycbcr.y);
		pMemInfo->stride[1] =
		pMemInfo->stride[2] = (int32_t)ycbcr.cstride;
		pMemInfo->size[1]   =
		pMemInfo->size[2]   = (int32_t)((uint64_t)ycbcr.cb - (uint64_t)ycbcr.cr);
		break;

	default:
		memset( pMemInfo, 0x00, sizeof(NX_MEMORY_INFO) );
		printf("Unknown format type.\n");
		ret = -1;
	}

	module->unlock(module, pHandle);

	return ret;
}
#endif
