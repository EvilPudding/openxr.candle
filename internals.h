#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
typedef struct IUnknown IUnknown;
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif

struct xrbody_internal
{
	bool_t initiated;
	XrSpace space;
	XrPath path;
	XrActionSet bodyset;
	XrAction poseAction;
	XrAction grabAction;
	XrAction hapticAction;
	XrAction leverAction;
};

struct openxr_internal
{
	bool_t initiated;
	bool_t failed;
	/* every OpenXR app that displays something needs at least an instance and a
	 * session */
	XrInstance instance;
	XrSession session;

	/* local space is used for "simple" small scale tracking. */
	/* A room scale VR application with bounds would use stage space. */
	XrSpace local_space;

	/* The runtime interacts with the OpenGL images (textures) via a Swapchain. */
	XrGraphicsBindingOpenGLWin32KHR graphics_binding_gl;
	/* one array of images per view */
	XrSwapchainImageOpenGLKHR** images;
	XrSwapchain* swapchains;
	XrEnvironmentBlendMode xr_blend;

	/* Each physical Display/Eye is described by a view */
	uint32_t view_count;
	XrViewConfigurationView* configuration_views;

	XrActionSet main_set;
	XrFrameState frame_state;

	XrActionSuggestedBinding bindings[64];
	uint32_t bindings_num;
	/* To render into a texture we need a framebuffer (one per texture to make it
	 * easy) */
	GLuint **framebuffers;
	mat4_t previous_view[2];
};

static
bool_t xr_result(XrInstance instance, XrResult result, const char* format, ...)
{
	if (XR_SUCCEEDED(result))
		return true;

	char resultString[XR_MAX_RESULT_STRING_SIZE];
	xrResultToString(instance, result, resultString);

	char formatRes[1024] = ""; // + " []\n"
	snprintf(formatRes, 1023, "%s [%s]\n", format, resultString);

	va_list args;
	va_start(args, format);
	vprintf(formatRes, args);
	va_end(args);
	return false;
}
