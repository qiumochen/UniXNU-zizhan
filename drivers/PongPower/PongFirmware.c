/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <IOKit/IOLib.h>
#include "ZizhanDspFirmwarePolicy.h"
int sheng_fw_open(const char *path,void **handle,uint64_t *size) {
 if(!handle||!size)return EINVAL;
 *handle=NULL;*size=0;
 if(!zizhan_dsp_firmware_path(path))return EINVAL;
 vnode_t vnode=NULLVP;vfs_context_t ctx=vfs_context_kernel();
 int rc=vnode_open(path,FREAD,0,0,&vnode,ctx);if(rc)return rc;
 struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_data_size);
 rc=vnode_getattr(vnode,&va,ctx);
 if(rc||!VATTR_IS_SUPPORTED(&va,va_data_size)||!va.va_data_size||va.va_data_size>64*1024*1024){vnode_close(vnode,FREAD,ctx);return rc?rc:EFTYPE;}
 *handle=vnode;*size=va.va_data_size;return 0;
}
int sheng_fw_read(void *handle,uint64_t offset,void *to,size_t length) {
 if(!handle||(!to&&length)||!zizhan_dsp_firmware_read_range(offset,length))return EINVAL;
 vfs_context_t ctx=vfs_context_kernel();int resid=(int)length;
 int rc=vn_rdwr(UIO_READ,(vnode_t)handle,(caddr_t)to,(int)length,(off_t)offset,UIO_SYSSPACE,IO_NOAUTH,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
 return rc?rc:resid?EIO:0;
}
void sheng_fw_close(void *handle) {if(handle)vnode_close((vnode_t)handle,FREAD,vfs_context_kernel());}
