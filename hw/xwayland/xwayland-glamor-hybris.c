/*
 * Copyright © 2011-2014 Intel Corporation
 * Copyright © 2017 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Lyude Paul <lyude@redhat.com>
 *
 */

#include "xwayland.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
//#include <gbm.h>
#include <glamor_egl.h>

#include <glamor.h>
#include <glamor_priv.h>
//#include <glamor_context.h>
//#include <dri3.h>
#include "drm-client-protocol.h"

#include <hybris/gralloc/gralloc.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "wayland-android-client-protocol.h"

#define DRIHYBRIS
#ifdef DRIHYBRIS
#include <xorg/drihybris.h>
#include <hybris/eglplatformcommon/hybris_nativebufferext.h>
#endif

#define GLAMOR_GLES2
#define EGL_WAYLAND_BUFFER_WL           0x31D5 /* eglCreateImageKHR target */
struct xwl_pixmap {
    struct wl_buffer *buffer;
    EGLClientBuffer buf;
    EGLImage image;
    unsigned int texture;
    struct gbm_bo *bo;
    buffer_handle_t handle;
    EGLClientBuffer remote_buffer;
};

struct glamor_egl_screen_private {
    EGLDisplay display;
    EGLContext context;
    char *device_path;

    CreateScreenResourcesProcPtr CreateScreenResources;
    CloseScreenProcPtr CloseScreen;
    int fd;
    struct gbm_device *gbm;
    int dmabuf_capable;

    PFNEGLHYBRISCREATENATIVEBUFFERPROC eglHybrisCreateNativeBuffer;
    PFNEGLHYBRISLOCKNATIVEBUFFERPROC eglHybrisLockNativeBuffer;
    PFNEGLHYBRISUNLOCKNATIVEBUFFERPROC eglHybrisUnlockNativeBuffer;
    PFNEGLHYBRISRELEASENATIVEBUFFERPROC eglHybrisReleaseNativeBuffer;
    PFNEGLHYBRISCREATEREMOTEBUFFERPROC eglHybrisCreateRemoteBuffer;
    PFNEGLHYBRISGETNATIVEBUFFERINFOPROC eglHybrisGetNativeBufferInfo;
    PFNEGLHYBRISSERIALIZENATIVEBUFFERPROC eglHybrisSerializeNativeBuffer;
    PFNEGLHYBRISCREATEWAYLANDBUFFERFROMIMAGEWLPROC eglCreateWaylandBufferFromImageWL;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;

    CloseScreenProcPtr saved_close_screen;
    DestroyPixmapProcPtr saved_destroy_pixmap;
    //xf86FreeScreenProc *saved_free_screen;
    struct android_wlegl * android_wlegl;
};

static struct glamor_egl_screen_private *glamor_egl = NULL;

static DevPrivateKeyRec xwl_hybris_private_key;

static uint32_t
wl_drm_format_for_depth(int depth)
{
    switch (depth) {
    case 15:
        return WL_DRM_FORMAT_XRGB1555;
    case 16:
        return WL_DRM_FORMAT_RGB565;
    case 24:
        return WL_DRM_FORMAT_XRGB8888;
    case 30:
        return WL_DRM_FORMAT_ARGB2101010;
    default:
        ErrorF("unexpected depth: %d\n", depth);
    case 32:
        return WL_DRM_FORMAT_ARGB8888;
    }
}

static char
is_fd_render_node(int fd)
{
    struct stat render;

    if (fstat(fd, &render))
        return 0;
    if (!S_ISCHR(render.st_mode))
        return 0;
    if (render.st_rdev & 0x80)
        return 1;

    return 0;
}

static char
is_device_path_render_node (const char *device_path)
{
    char is_render_node;
    int fd;

    fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 0;

    is_render_node = is_fd_render_node(fd);
    close(fd);

    return is_render_node;
}

static PixmapPtr
xwl_glamor_hybris_create_pixmap_for_native_buffer(ScreenPtr screen,  EGLClientBuffer buf, int width, int height,
                                    int depth)
{
    PixmapPtr pixmap;
    struct xwl_pixmap *xwl_pixmap;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    pixmap = glamor_create_pixmap(screen,
                                  width,
                                  height,
                                  depth,
                                  GLAMOR_CREATE_PIXMAP_NO_TEXTURE);
    if (!pixmap) {
        //free(xwl_pixmap);
        return NULL;
    }


    xwl_pixmap = malloc(sizeof *xwl_pixmap); 
    if (xwl_pixmap == NULL) 
        return NULL; 

    xwl_glamor_egl_make_current(xwl_screen);

    xwl_pixmap->buf = buf;
    xwl_pixmap->buffer = NULL;
    //xwl_pixmap->buffer = wlb;
    //xwl_pixmap->handle = handle;
    xwl_pixmap->image = glamor_egl->eglCreateImageKHR(xwl_screen->egl_display,
                                          EGL_NO_CONTEXT,
                                          EGL_NATIVE_BUFFER_HYBRIS,
                                          xwl_pixmap->buf, NULL);
    if (xwl_pixmap->image == EGL_NO_IMAGE_KHR)
      goto error;

    glGenTextures(1, &xwl_pixmap->texture);
    glBindTexture(GL_TEXTURE_2D, xwl_pixmap->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, xwl_pixmap->image);
    if (eglGetError() != EGL_SUCCESS)
      goto error;

    glBindTexture(GL_TEXTURE_2D, 0);

    glamor_set_pixmap_texture(pixmap, xwl_pixmap->texture);
    if (!glamor_get_pixmap_texture(pixmap))
      goto error;

    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);

    //glamor_set_pixmap_type(pixmap, GLAMOR_DRM_ONLY);
    xwl_pixmap_set_private(pixmap, xwl_pixmap);
    //struct glamor_pixmap_private *pixmap_priv =
    //    glamor_get_pixmap_private(pixmap);
    goto done;
#if 0    
    EGLImageKHR image;
    GLuint texture;
    Bool ret = FALSE;

    //if (pixmap_priv->buf)
    //    glamor_egl->eglHybrisReleaseNativeBuffer(pixmap_priv->buf);

    //glamor_make_current(glamor_priv);
    //xwl_glamor_egl_make_current(xwl_screen);
    eglMakeCurrent(xwl_screen->egl_display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE, xwl_screen->egl_context)
    image = eglCreateImageKHR(xwl_screen->egl_display,
                              /* glamor_egl->context*/ EGL_NO_CONTEXT,
                              EGL_NATIVE_BUFFER_HYBRIS, wnb, NULL);
    if (image == EGL_NO_IMAGE_KHR) {
        glamor_set_pixmap_type(pixmap, GLAMOR_DRM_ONLY);
        goto done;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    //if (eglGetError() != EGL_SUCCESS)
    //  goto error;

    glBindTexture(GL_TEXTURE_2D, 0);

    pixmap_priv->wnb = wnb;
    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    glamor_set_pixmap_texture(pixmap, texture);


    EGLImageKHR old;

    old = pixmap_priv->image;
    if (old) {
         eglDestroyImageKHR(xwl_screen->egl_display, old);
    }
    pixmap_priv->image = image;
#endif
done:
    return pixmap;

error:
    if (xwl_pixmap->image != EGL_NO_IMAGE_KHR)
      eglDestroyImageKHR(xwl_screen->egl_display, xwl_pixmap->image);
    if (pixmap)
      glamor_destroy_pixmap(pixmap);
    free(xwl_pixmap);

    return NULL;

}

static void
wl_buffer_release(void *data, struct wl_buffer *buffer)
{
   // WaylandNativeWindow *win = static_cast<WaylandNativeWindow *>(data);
   // win->releaseBuffer(buffer);
}

static struct wl_buffer_listener wl_buffer_listener = {
    wl_buffer_release
};


static PixmapPtr
xwl_glamor_hybris_create_pixmap(ScreenPtr screen,
                             int width, int height, int depth,
                             unsigned int hint)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_pixmap *xwl_pixmap;
    //WaylandNativeWindowBuffer *wnb = NULL;
    PixmapPtr pixmap = NULL;
    //struct glamor_egl_screen_private *glamor_egl = xwl_hybris_get(xwl_screen);

    if (width > 0 && height > 0 && depth >= 15 &&
        (hint == 0 ||
         hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP ||
         hint == CREATE_PIXMAP_USAGE_SHARED)) {
	int m_format = 1;
	//int m_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
	int m_usage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER;
	//int m_usage = 0xb00;
	uint32_t stride = 0;
	buffer_handle_t handle;
       // wnb = new ClientWaylandBuffer(width, height, m_format, m_usage);

       EGLClientBuffer buf;
       glamor_egl->eglHybrisCreateNativeBuffer(width, height,
                                      m_usage,
                                      HYBRIS_PIXEL_FORMAT_RGBA_8888,
                                      &stride, &buf);
#if 0
	struct wl_array ints;
	int *ints_data;
	struct android_wlegl_handle *wlegl_handle;

        int alloc_ok = hybris_gralloc_allocate(width ? width : 1, height ? height : 1, m_format, m_usage, &handle, &stride);

    ErrorF("xwl_glamor_hybris_create_pixmap before wl_array_add width: %d, height: %d, numInts: %d\n",width, height,  handle->numInts);

	wl_array_init(&ints);
	ints_data = (int*) wl_array_add(&ints, handle->numInts*sizeof(int));
    ErrorF("xwl_glamor_hybris_create_pixmap after wl_array_add width:%d, height:%d,  numInts : %d\n", width, height,  handle->numInts);
	memcpy(ints_data, handle->data + handle->numFds, handle->numInts*sizeof(int));
    ErrorF("xwl_glamor_hybris_create_pixmap after 1: numFds: %d \n", handle->numFds);

	wlegl_handle = android_wlegl_create_handle((struct android_wlegl_handle *)glamor_egl->android_wlegl, handle->numFds, &ints);

    ErrorF("xwl_glamor_hybris_create_pixmap after 2\n");
	wl_array_release(&ints);

    ErrorF("xwl_glamor_hybris_create_pixmap after 3  numFds: %d\n", handle->numFds);
	for (int i = 0; i < handle->numFds; i++) {
    		ErrorF("xwl_glamor_hybris_create_pixmap before addFd i: %d\n", i);
		android_wlegl_handle_add_fd(wlegl_handle, handle->data[i]);
	}

    ErrorF("xwl_glamor_hybris_create_pixmap after 4\n");
	struct wl_buffer * wlbuffer = android_wlegl_create_buffer((struct android_wlegl_handle *)glamor_egl->android_wlegl, width, height, stride, m_format, m_usage, wlegl_handle);
    ErrorF("xwl_glamor_hybris_create_pixmap after 5\n");

        android_wlegl_handle_destroy(wlegl_handle);
	

    ErrorF("xwl_glamor_hybris_create_pixmap after 6\n");
	//WaylandDisplay *wdpy = (WaylandDisplay *)xwl_screen->egl_display;
	//glamor_egl->wl_queue = wl_display_create_queue(wdpy->wl_dpy);
	//wnb->init(glamor_egl->android_wlegl, wdpy->wl_dpy, glamor_egl->wl_queue);
	wl_buffer_add_listener(wlbuffer, &wl_buffer_listener, NULL);
	//wl_proxy_set_queue((struct wl_proxy *) wnb->wlbuffer, glamor_egl->wl_queue);
#endif
	//if (handle) {
	    pixmap = xwl_glamor_hybris_create_pixmap_for_native_buffer(screen, buf,width, height, depth);
	    if (!pixmap) {
	    
	    }
	    else if (xwl_screen->rootless && hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP) {
                glamor_clear_pixmap(pixmap);
            }

	//}
    }

    if (!pixmap)
        pixmap = glamor_create_pixmap(screen, width, height, depth, hint);

    return pixmap;

}

static Bool
xwl_glamor_hybris_destroy_pixmap(PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
    //struct glamor_pixmap_private *pixmap_priv =
    //    glamor_get_pixmap_private(pixmap);

    if (xwl_pixmap && pixmap->refcnt == 1) {
        if (xwl_pixmap->buffer)
            wl_buffer_destroy(xwl_pixmap->buffer);
	//if (xwl_pixmap->handle > 0)
	//     hybris_gralloc_release(xwl_pixmap->handle, 1);
	
	eglDestroyImageKHR(xwl_screen->egl_display, xwl_pixmap->image);
	 if (xwl_pixmap->buf)
		glamor_egl->eglHybrisReleaseNativeBuffer(xwl_pixmap->buf);
        //eglDestroyImageKHR(xwl_screen->egl_display, pixmap_priv->image);
	//pixmap_priv->image = NULL;
	free(xwl_pixmap);
	xwl_pixmap_set_private(pixmap, NULL);
    }

    return fbDestroyPixmap(pixmap);
    //return glamor_destroy_pixmap(pixmap);

}

static struct wl_buffer *
xwl_glamor_hybris_get_wl_buffer_for_pixmap(PixmapPtr pixmap,
                                        Bool *created)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
    //struct glamor_pixmap_private *pixmap_priv =
   //     glamor_get_pixmap_private(pixmap);
   // WaylandNativeWindowBuffer * wnb = pixmap_priv->wnb;
    if (xwl_pixmap == NULL)
       return NULL;

    if (xwl_pixmap->buffer) {
        /* Buffer already exists. Return it and inform caller if interested. */
        if (created)
            *created = FALSE;
        return xwl_pixmap->buffer;
    }

    if (created)
        *created = TRUE;

    if (!xwl_pixmap->buf)
       return NULL;
   //struct egl_image * egl_image = xwl_pixmap->image;
   //egl_image->target = EGL_WAYLAND_BUFFER_WL;
   //xwl_pixmap->buffer = glamor_egl->eglCreateWaylandBufferFromImageWL(xwl_screen->egl_display, xwl_pixmap->image);

    int numInts = 0;
    int numFds = 0;

    int * ints = NULL;
    int * fds = NULL;

    glamor_egl->eglHybrisGetNativeBufferInfo(xwl_pixmap->buf, &numInts, &numFds);

    ints = malloc(numInts * sizeof(int));
    fds = malloc(numFds * sizeof(int));

    glamor_egl->eglHybrisSerializeNativeBuffer(xwl_pixmap->buf, ints, fds);

    struct android_wlegl_handle *wlegl_handle;
    struct wl_array wl_ints;
    int *the_ints;

    wl_array_init(&wl_ints);
    the_ints = (int *)wl_array_add(&wl_ints, numInts * sizeof(int));
    memcpy(the_ints, ints, numInts * sizeof(int));
    wlegl_handle = android_wlegl_create_handle(glamor_egl->android_wlegl, numFds, &wl_ints);
    wl_array_release(&wl_ints);

    for (int i = 0; i < numFds; i++) {
            android_wlegl_handle_add_fd(wlegl_handle, fds[i]);
    }

    int width =  pixmap->drawable.width;
    int height =  pixmap->drawable.height;
    //int stride = ((struct ANativeWindowBuffer *)(xwl_pixmap->buf))->stride ;
    int stride = width * 4;
    int m_format = 1;

    struct wl_buffer * wlbuffer = android_wlegl_create_buffer((struct android_wlegl_handle *)glamor_egl->android_wlegl, width, height, stride, m_format, HYBRIS_USAGE_HW_RENDER, wlegl_handle);
    android_wlegl_handle_destroy(wlegl_handle);

    //wl_buffer_add_listener(wlbuffer, &buffer_listener, buffer);
    //wl_buffer_add_listener(wlbuffer, &wl_buffer_listener, NULL);
    xwl_pixmap->buffer = wlbuffer;

    return xwl_pixmap->buffer;
}

/* Not actually used, just defined here so there's something for
 * _glamor_egl_fds_from_pixmap() to link against
 */
_X_EXPORT int
glamor_egl_fd_from_pixmap(ScreenPtr screen, PixmapPtr pixmap,
                          CARD16 *stride, CARD32 *size)
{
    return -1;
}


static Bool
xwl_glamor_hybris_init_wl_registry(struct xwl_screen *xwl_screen,
                                struct wl_registry *wl_registry,
                                uint32_t id, const char *name,
                                uint32_t version)
{

   //struct glamor_egl_screen_private *glamor_egl = xwl_hybris_get(xwl_screen);

   if(strcmp(name, "android_wlegl") == 0) {
	glamor_egl->android_wlegl = wl_registry_bind(wl_registry, id,
					&android_wlegl_interface, version);
        ErrorF("xwl_glamor_hybris_init_wl_registry android_wlegl: %p id: %d  version: %d \n", glamor_egl->android_wlegl,id, version);
	return TRUE;
    }

    /* no match */
    return FALSE;
}

static Bool
xwl_glamor_hybris_has_wl_interfaces(struct xwl_screen *xwl_screen)
{
//    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
//
//    if (xwl_gbm->drm == NULL) {
//        ErrorF("glamor: 'wl_drm' not supported\n");
//        return FALSE;
//    }

    return TRUE;
}

Bool hybris_init_hybris_native_buffer(struct xwl_screen *xwl_screen)
{
    //struct glamor_egl_screen_private *glamor_egl;

    //glamor_egl = glamor_egl_get_screen_private(scrn);

    //if (strstr(eglQueryString(glamor_egl->display, EGL_EXTENSIONS), "EGL_HYBRIS_native_buffer") == NULL)
    //{
    //    xf86DrvMsg(scrn->scrnIndex, X_ERROR, "EGL_HYBRIS_native_buffer is missing. Make sure libhybris EGL implementation is used\n");
    //    return FALSE;
    //}

    glamor_egl->eglHybrisCreateNativeBuffer = (PFNEGLHYBRISCREATENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisCreateNativeBuffer");
    assert(glamor_egl->eglHybrisCreateNativeBuffer != NULL);

    glamor_egl->eglHybrisCreateRemoteBuffer = (PFNEGLHYBRISCREATEREMOTEBUFFERPROC) eglGetProcAddress("eglHybrisCreateRemoteBuffer");
    assert(glamor_egl->eglHybrisCreateRemoteBuffer != NULL);

    glamor_egl->eglHybrisLockNativeBuffer = (PFNEGLHYBRISLOCKNATIVEBUFFERPROC) eglGetProcAddress("eglHybrisLockNativeBuffer");
    assert(glamor_egl->eglHybrisLockNativeBuffer != NULL);

    glamor_egl->eglHybrisUnlockNativeBuffer = (PFNEGLHYBRISUNLOCKNATIVEBUFFERPROC) eglGetProcAddress("eglHybrisUnlockNativeBuffer");
    assert(glamor_egl->eglHybrisUnlockNativeBuffer != NULL);

    glamor_egl->eglHybrisReleaseNativeBuffer = (PFNEGLHYBRISRELEASENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisReleaseNativeBuffer");
    assert(glamor_egl->eglHybrisReleaseNativeBuffer != NULL);

    glamor_egl->eglHybrisReleaseNativeBuffer = (PFNEGLHYBRISRELEASENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisReleaseNativeBuffer");
    assert(glamor_egl->eglHybrisReleaseNativeBuffer != NULL);

    glamor_egl->eglHybrisGetNativeBufferInfo = (PFNEGLHYBRISGETNATIVEBUFFERINFOPROC) eglGetProcAddress("eglHybrisGetNativeBufferInfo");
    assert(glamor_egl->eglHybrisGetNativeBufferInfo != NULL);

    glamor_egl->eglHybrisSerializeNativeBuffer = (PFNEGLHYBRISSERIALIZENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisSerializeNativeBuffer");
    assert(glamor_egl->eglHybrisSerializeNativeBuffer != NULL);
    glamor_egl->eglCreateWaylandBufferFromImageWL = (PFNEGLHYBRISCREATEWAYLANDBUFFERFROMIMAGEWLPROC) eglGetProcAddress("eglCreateWaylandBufferFromImageWL");
    glamor_egl->eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    return TRUE;
}


static Bool
xwl_glamor_hybris_init_egl(struct xwl_screen *xwl_screen)
{

    const char *version;

    EGLint config_attribs[] = {
#ifdef GLAMOR_GLES2
        EGL_CONTEXT_CLIENT_VERSION, 2,
#endif
        EGL_NONE
    };
    static const EGLint config_attribs_core[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MAJOR,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MINOR,
        EGL_NONE
    };

    const EGLint config_attribs_gles2[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_NONE
    };

    EGLint num_configs;
    EGLConfig egl_config;

    //glamor_identify(0);
    //glamor_egl = calloc(sizeof(*glamor_egl), 1);

    //if (!dixRegisterPrivateKey(&xwl_hybris_private_key, PRIVATE_SCREEN, 0))
    //    return FALSE;

    //dixSetPrivate(&xwl_screen->screen->devPrivates, &xwl_hybris_private_key,
    //              glamor_egl);


    int fd = 0;
    int major = 0;
    int minor = 0;
    xwl_screen->egl_display = eglGetDisplay((EGLNativeDisplayType) (intptr_t) fd);

    if (!eglInitialize
        (xwl_screen->egl_display, &major, &minor)) {
        //xf86DrvMsg(scrn->scrnIndex, X_ERROR, "eglInitialize() failed\n");
        xwl_screen->egl_display = EGL_NO_DISPLAY;
        goto error;
    }

#ifndef GLAMOR_GLES2
    eglBindAPI(EGL_OPENGL_API);
#else
    eglBindAPI(EGL_OPENGL_ES_API);
#endif
    
    version = eglQueryString(xwl_screen->egl_display , EGL_VERSION);

#define GLAMOR_CHECK_EGL_EXTENSION(EXT)  \
        if (!epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT)) {  \
                ErrorF("EGL_" #EXT " required.\n");  \
/*              goto error; */ \
        }

#define GLAMOR_CHECK_EGL_EXTENSIONS(EXT1, EXT2)  \
        if (!epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT1) &&  \
            !epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT2)) {  \
                ErrorF("EGL_" #EXT1 " or EGL_" #EXT2 " required.\n");  \
                goto error;  \
        }

    GLAMOR_CHECK_EGL_EXTENSION(KHR_gl_renderbuffer_image);
#ifdef GLAMOR_GLES2
    GLAMOR_CHECK_EGL_EXTENSION(KHR_surfaceless_context);
    GLAMOR_CHECK_EGL_EXTENSION(KHR_surfaceless_gles2);
//    GLAMOR_CHECK_EGL_EXTENSIONS(KHR_surfaceless_context, KHR_surfaceless_gles2);
#else
    GLAMOR_CHECK_EGL_EXTENSIONS(KHR_surfaceless_context,
                                KHR_surfaceless_opengl);
#endif

#ifndef GLAMOR_GLES2
    xwl_screen->egl_context = eglCreateContext(xwl_screen->egl_display,
                                           NULL, EGL_NO_CONTEXT,
                                           config_attribs_core);
#else
    xwl_screen->egl_context = NULL;
#endif
    if (!eglChooseConfig(xwl_screen->egl_display , config_attribs_gles2, 0, 0, &num_configs)) {
        ErrorF("eglChooseConfig Fail to get Confings\n");
        return false;
    }

    if (!eglChooseConfig(xwl_screen->egl_display, config_attribs_gles2, &egl_config, 1, &num_configs)) {
        ErrorF("Fail to get Config, num_configs=%d\n",num_configs);
        return false;
    }
    xwl_screen->egl_context = eglCreateContext(xwl_screen->egl_display,
                                           egl_config, EGL_NO_CONTEXT,
                                           config_attribs);
    if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
        //xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed to create EGL context\n");
        goto error;
    }
    //glamor_egl->surface = EGL_NO_SURFACE;

    if (!eglMakeCurrent(xwl_screen->egl_display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE, xwl_screen->egl_context)) {
        //xf86DrvMsg(scrn->scrnIndex, X_ERROR,
        //           "Failed to make EGL context currentgl%x egl%x\n", glGetError(), eglGetError());
        goto error;
    }

    hybris_init_hybris_native_buffer(xwl_screen);

    //lastGLContext = NULL;

    return TRUE;

error:

    if (xwl_screen->egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(xwl_screen->egl_display, xwl_screen->egl_context);
        xwl_screen->egl_context = EGL_NO_CONTEXT;
    }

    if (xwl_screen->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(xwl_screen->egl_display);
        xwl_screen->egl_display = EGL_NO_DISPLAY;
    }

    free(glamor_egl);
    //glamor_egl_cleanup(glamor_egl);
    return FALSE;


}

static Bool
glamor_create_texture_from_image(ScreenPtr screen,
                                 EGLImageKHR image, GLuint * texture)
{
    struct glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);

    glamor_make_current(glamor_priv);

    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    glBindTexture(GL_TEXTURE_2D, 0);

    return TRUE;
}

static void
glamor_egl_set_pixmap_image(PixmapPtr pixmap, EGLImageKHR image,
                            Bool used_modifiers)
{
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    EGLImageKHR old;

    old = pixmap_priv->image;
    if (old) {
        ScreenPtr                               screen = pixmap->drawable.pScreen;
       // ScrnInfoPtr                             scrn = xf86ScreenToScrn(screen);
       // struct glamor_egl_screen_private        *glamor_egl = glamor_egl_get_screen_private(scrn);

        eglDestroyImageKHR(glamor_egl->display, old);
    }
    pixmap_priv->image = image;
    pixmap_priv->used_modifiers = used_modifiers;
}

Bool
glamor_egl_create_textured_pixmap_from_egl_buffer(PixmapPtr pixmap,
                                              EGLClientBuffer buf)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    //ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);
    glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    //struct glamor_egl_screen_private *glamor_egl;
    EGLImageKHR image;
    GLuint texture;
    Bool ret = FALSE;

    //glamor_egl = glamor_egl_get_screen_private(scrn);

    if (pixmap_priv->buf)
        glamor_egl->eglHybrisReleaseNativeBuffer(pixmap_priv->buf);

    glamor_make_current(glamor_priv);

    image = glamor_egl->eglCreateImageKHR(glamor_egl->display,
                              /* glamor_egl->context*/ EGL_NO_CONTEXT,
                             EGL_NATIVE_BUFFER_HYBRIS, buf, NULL);
    if (image == EGL_NO_IMAGE_KHR) {
        glamor_set_pixmap_type(pixmap, GLAMOR_DRM_ONLY);
        goto done;
    }

    glamor_create_texture_from_image(screen, image, &texture);
    pixmap_priv->buf = buf;

    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    glamor_set_pixmap_texture(pixmap, texture);
    glamor_egl_set_pixmap_image(pixmap, image, false);
    ret = TRUE;

 done:
    return ret;
}

_X_EXPORT Bool
glamor_back_pixmap_from_hybris_buffer(ScreenPtr screen, PixmapPtr * pixmap,
                           CARD16 width,
                           CARD16 height,
                           CARD16 stride, CARD8 depth, CARD8 bpp,
                           int numInts, int *ints,
                           int numFds, int *fds)
{
    //ScreenPtr screen = pixmap->drawable.pScreen;
    //ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    //struct glamor_egl_screen_private *glamor_egl;
    Bool ret;

    //glamor_egl = glamor_egl_get_screen_private(scrn);

    if (bpp != 32 || !(depth == 24 || depth == 32) || width == 0 || height == 0)
        return FALSE;

    EGLClientBuffer buf;
    glamor_egl->eglHybrisCreateRemoteBuffer(width, height, HYBRIS_USAGE_HW_TEXTURE,
                                            HYBRIS_PIXEL_FORMAT_RGBA_8888, stride,
                                            numInts, ints, numFds, fds, &buf);

    *pixmap = xwl_glamor_hybris_create_pixmap_for_native_buffer(screen, buf,width, height, depth);
    if (!*pixmap) {
        ErrorF("%s  LINE: %d \n", __func__ , __LINE__);
    }

    //screen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, NULL);

    return true;


#if 0
    struct android_wlegl_handle *wlegl_handle;
    struct wl_array wl_ints;
    int *the_ints;

    wl_array_init(&wl_ints);
    the_ints = (int *)wl_array_add(&wl_ints, numInts * sizeof(int));
    memcpy(the_ints, ints, numInts * sizeof(int));
    wlegl_handle = android_wlegl_create_handle(glamor_egl->android_wlegl, numFds, &wl_ints);
    wl_array_release(&wl_ints);

    for (int i = 0; i < numFds; i++) {
            android_wlegl_handle_add_fd(wlegl_handle, fds[i]);
    }

    int m_format = 1;

    struct wl_buffer * wlbuffer = android_wlegl_create_buffer((struct android_wlegl_handle *)glamor_egl->android_wlegl, width, height, stride, m_format, GRALLOC_USAGE_HW_RENDER, wlegl_handle);
    android_wlegl_handle_destroy(wlegl_handle);

    //wl_buffer_add_listener(wlbuffer, &buffer_listener, buffer);
    wl_buffer_add_listener(wlbuffer, &wl_buffer_listener, NULL);
 

    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
    //struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    if (xwl_pixmap == NULL) {
        xwl_pixmap = malloc(sizeof *xwl_pixmap);
    }

    xwl_pixmap->buffer = wlbuffer;
    xwl_pixmap->handle = 0;
    xwl_pixmap->remote_buffer = buf;
    //glamor_set_pixmap_type(pixmap, GLAMOR_DRM_ONLY);
    xwl_pixmap_set_private(pixmap, xwl_pixmap);
#endif

//    screen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, NULL);

    //ret = glamor_egl_create_textured_pixmap_from_egl_buffer(pixmap, buf);
    return ret;
}

_X_EXPORT PixmapPtr
glamor_pixmap_from_hybris_buffer(ScreenPtr screen,
                      CARD16 width,
                      CARD16 height,
                      CARD16 stride, CARD8 depth, CARD8 bpp,
                      int numInts, int *ints,
                      int numFds, int *fds)
{
    PixmapPtr pixmap;
    Bool ret;

    //pixmap = screen->CreatePixmap(screen, 0, 0, depth, 0);
    ret = glamor_back_pixmap_from_hybris_buffer(screen, &pixmap, width, height,
                                     stride, depth, bpp,
                                     numInts, ints,
                                     numFds, fds);

    if (ret == FALSE) {
        screen->DestroyPixmap(pixmap);
        return NULL;
    }
    return pixmap;
}

_X_EXPORT void
glamor_egl_exchange_buffers(PixmapPtr front, PixmapPtr back)
{
    EGLImageKHR temp_img;
    Bool temp_mod;
    struct glamor_pixmap_private *front_priv =
        glamor_get_pixmap_private(front);
    struct glamor_pixmap_private *back_priv =
        glamor_get_pixmap_private(back);

    glamor_pixmap_exchange_fbos(front, back);

    temp_img = back_priv->image;
    temp_mod = back_priv->used_modifiers;
    back_priv->image = front_priv->image;
    back_priv->used_modifiers = front_priv->used_modifiers;
    front_priv->image = temp_img;
    front_priv->used_modifiers = temp_mod;

    glamor_set_pixmap_type(front, GLAMOR_TEXTURE_DRM);
    glamor_set_pixmap_type(back, GLAMOR_TEXTURE_DRM);
}


static Bool
glamor_hybris_make_pixmap_exportable(PixmapPtr pixmap)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    //ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    //struct glamor_egl_screen_private *glamor_egl =
    //    glamor_egl_get_screen_private(scrn);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    unsigned width = pixmap->drawable.width;
    unsigned height = pixmap->drawable.height;
    PixmapPtr exported;
    GCPtr scratch_gc;
    int stride;
    int err;

    if (pixmap_priv->image)
        return TRUE;

    if (pixmap->drawable.bitsPerPixel != 32) {
       // xf86DrvMsg(scrn->scrnIndex, X_ERROR,
       //            "Failed to make %dbpp pixmap exportable\n",
       //            pixmap->drawable.bitsPerPixel);
        return FALSE;
    }

    EGLClientBuffer buf;
    err = glamor_egl->eglHybrisCreateNativeBuffer(width, height,
                                      HYBRIS_USAGE_HW_TEXTURE |
                                      HYBRIS_USAGE_SW_READ_NEVER | HYBRIS_USAGE_SW_WRITE_NEVER,
                                      HYBRIS_PIXEL_FORMAT_RGBA_8888,
                                      &stride, &buf);

    exported = screen->CreatePixmap(screen, 0, 0, pixmap->drawable.depth, 0);
    screen->ModifyPixmapHeader(exported, width, height, 0, 0,
                               stride, NULL);
    if (!glamor_egl_create_textured_pixmap_from_egl_buffer(exported, buf)) {
       // xf86DrvMsg(scrn->scrnIndex, X_ERROR,
       //            "Failed to make %dx%dx%dbpp pixmap from EGLClientBuffer\n",
        //           width, height, pixmap->drawable.bitsPerPixel);
        screen->DestroyPixmap(exported);
        glamor_egl->eglHybrisReleaseNativeBuffer(buf);
        return FALSE;
    }

    scratch_gc = GetScratchGC(pixmap->drawable.depth, screen);
    ValidateGC(&pixmap->drawable, scratch_gc);
    scratch_gc->ops->CopyArea(&pixmap->drawable, &exported->drawable,
                              scratch_gc,
                              0, 0, width, height, 0, 0);
    FreeScratchGC(scratch_gc);

    /* Now, swap the tex/gbm/EGLImage/etc. of the exported pixmap into
     * the original pixmap struct.
     */
    glamor_egl_exchange_buffers(pixmap, exported);

    screen->DestroyPixmap(exported);

    return TRUE;
}

_X_EXPORT int
glamor_hybris_buffer_from_pixmap(ScreenPtr screen,
                            PixmapPtr pixmap, CARD16 *stride,
                            int *numInts, int **ints,
                            int *numFds, int **fds)
{
    //glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
//    glamor_screen_private *glamor_priv =
//        glamor_get_screen_private(pixmap->drawable.pScreen);
//    struct glamor_egl_screen_private *glamor_egl =
//        glamor_egl_get_screen_private(xf86ScreenToScrn(screen));

    //unsigned int tex;


    glamor_egl->eglHybrisGetNativeBufferInfo(xwl_pixmap->buffer, numInts, numFds);

    *ints = malloc(*numInts * sizeof(int));
    *fds = malloc(*numFds * sizeof(int));

    glamor_egl->eglHybrisSerializeNativeBuffer(xwl_pixmap->buffer, *ints, *fds);
    return 0;

//    switch (pixmap_priv->type) {
//    case GLAMOR_TEXTURE_DRM:
//    case GLAMOR_TEXTURE_ONLY:
//        if (!glamor_pixmap_ensure_fbo(pixmap, GL_RGBA, 0))
//            return -1;
//
//        if (!glamor_hybris_make_pixmap_exportable(pixmap))
//            return -1;
//
//        glamor_egl->eglHybrisGetNativeBufferInfo(pixmap_priv->buf, numInts, numFds);
//
//        *ints = malloc(*numInts * sizeof(int));
//        *fds = malloc(*numFds * sizeof(int));
//
//        glamor_egl->eglHybrisSerializeNativeBuffer(pixmap_priv->buf, *ints, *fds);
//
//        return 0;
//
//    default:
//        break;
//    }
//    return -1;
}

static drihybris_screen_info_rec glamor_drihybris_info = {
    .version = 1,
    .pixmap_from_buffer = glamor_pixmap_from_hybris_buffer,
    .buffer_from_pixmap = glamor_hybris_buffer_from_pixmap,
};

static Bool
xwl_glamor_hybris_init_screen(struct xwl_screen *xwl_screen)
{
     //hybris_gralloc_initialize(0);
//#ifdef DRIHYBRIS
//    if (glamor_egl->drihybris_capable) {
        if (!drihybris_screen_init(xwl_screen->screen, &glamor_drihybris_info)) {
           // xf86DrvMsg(scrn->scrnIndex, X_ERROR,
           //             "Failed to initialize DRIHYBRIS.\n");
        }
//    }
//#endif

    xwl_screen->screen->CreatePixmap = xwl_glamor_hybris_create_pixmap;
    xwl_screen->screen->DestroyPixmap = xwl_glamor_hybris_destroy_pixmap;
    return TRUE;
}

void
xwl_glamor_init_hybris(struct xwl_screen *xwl_screen)
{
    glamor_egl = calloc(sizeof(*glamor_egl), 1);

    xwl_screen->glamor_hybris_backend.is_available = FALSE;
    drihybris_extension_init();
    xwl_screen->glamor_hybris_backend.init_wl_registry = xwl_glamor_hybris_init_wl_registry;
    xwl_screen->glamor_hybris_backend.has_wl_interfaces = xwl_glamor_hybris_has_wl_interfaces;
    xwl_screen->glamor_hybris_backend.init_egl = xwl_glamor_hybris_init_egl;
    xwl_screen->glamor_hybris_backend.init_screen = xwl_glamor_hybris_init_screen;
    xwl_screen->glamor_hybris_backend.get_wl_buffer_for_pixmap = xwl_glamor_hybris_get_wl_buffer_for_pixmap;
    xwl_screen->glamor_hybris_backend.is_available = TRUE;
}
