/* X11/extensions/XShm.h -- add the one MIT-SHM function AMIX forgot.
 *
 * AMIX's X11R5 header declares the shared-memory functions in K&R style --
 *     Status XShmAttach(), XShmDetach(), XShmPutImage(), XShmGetImage();
 * -- and simply omits XShmQueryExtension, which is the one a client has to call
 * FIRST to find out whether the server supports the extension at all.  The
 * symbol is present in libXext.a; only the declaration is missing.
 *
 * Declared K&R here too, matching the rest of the file rather than imposing a
 * prototype on a library built without one.
 */
#ifndef AMIX_FIXED_X11_XSHM_H
#define AMIX_FIXED_X11_XSHM_H

#ifdef __cplusplus
extern "C" {
#endif

#include_next <X11/extensions/XShm.h>

#ifndef _XSHM_SERVER_
Bool XShmQueryExtension();
#endif

#ifdef __cplusplus
}
#endif

#endif /* AMIX_FIXED_X11_XSHM_H */
