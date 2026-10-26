/* librsvg-2-2.dll with only the three functions FFmpeg's SVG decoder (avcodec) imports. The game
 * never decodes SVG. The real librsvg loads GIO, whose DllMain starts a thread that enumerates the
 * installed UWP apps; that thread crashed inside iertutil.dll at startup (1 run in 40). Placed
 * next to the executable, this stub is loaded instead and GIO never is. */
#include <stddef.h>

__declspec(dllexport) void *rsvg_handle_new_from_data(const void *data, size_t size, void **error) {
    (void)data;
    (void)size;
    (void)error;
    return NULL;
}

__declspec(dllexport) int rsvg_handle_get_intrinsic_size_in_pixels(void *handle, double *width,
                                                                    double *height) {
    (void)handle;
    if (width) *width = 0;
    if (height) *height = 0;
    return 0;
}

__declspec(dllexport) int rsvg_handle_render_document(void *handle, void *cairo, const void *viewport,
                                                      void **error) {
    (void)handle;
    (void)cairo;
    (void)viewport;
    (void)error;
    return 0;
}
