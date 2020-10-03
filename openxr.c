#include "openxr.h"
#include "xrbody.h"

#include "../candle/components/camera.h"
#include "../candle/components/node.h"
#include "../candle/systems/sauces.h"

#include "internals.h"

bool_t is_extension_supported(char* extensionName, XrExtensionProperties* instanceExtensionProperties,
                            uint32_t instanceExtensionCount)
{
	for (uint32_t supportedIndex = 0; supportedIndex < instanceExtensionCount;
	     supportedIndex++)
	{
		printf("%s\n", instanceExtensionProperties[supportedIndex].extensionName);
	}
	for (uint32_t supportedIndex = 0; supportedIndex < instanceExtensionCount;
	     supportedIndex++)
	{
		if (!strcmp(extensionName, instanceExtensionProperties[supportedIndex].extensionName))
		{
			return true;
		}
	}
	return false;
}
XrDebugUtilsMessengerEXT xr_debug;

static void c_openxr_init_actions(struct openxr_internal *self);
PFN_xrCreateDebugUtilsMessengerEXT    ext_xrCreateDebugUtilsMessengerEXT;
PFN_xrDestroyDebugUtilsMessengerEXT   ext_xrDestroyDebugUtilsMessengerEXT;

static XrBool32 _debug_cb(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types,
		const XrDebugUtilsMessengerCallbackDataEXT *msg, void* user_data) {
	printf("%s: %s\n", msg->functionName, msg->message);
	return (XrBool32)XR_FALSE;
}

int openxr_internal_init(struct openxr_internal *self)
{
	glFinish();
	glerr();
	XrResult result;

	uint32_t extensionCount = 0;
	result =
	    xrEnumerateInstanceExtensionProperties(NULL, 0, &extensionCount, NULL);

	/* TODO: instance null will not be able to convert XrResult to string */
	if (!xr_result(NULL, result,
	               "failed to enumerate number of extension properties"))
		return 0;

	printf("Runtime supports %d extensions\n", extensionCount);

	XrExtensionProperties extensionProperties[256];
	for (uint16_t i = 0; i < extensionCount; i++) {
		extensionProperties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		extensionProperties[i].next = NULL;
	}

	result = xrEnumerateInstanceExtensionProperties(
	    NULL, extensionCount, &extensionCount, extensionProperties);
	if (!xr_result(NULL, result, "failed to enumerate extension properties"))
		return 0;

	if (!is_extension_supported(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,
	                          extensionProperties, extensionCount)) {
		printf("Runtime does not support OpenGL extension!\n");
		return 0;
	}

	printf("Runtime supports required extension %s\n",
	       XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);

	// --- Enumerate API layers
	bool_t lunargCoreValidationSupported = false;
	uint32_t apiLayerCount;
	xrEnumerateApiLayerProperties(0, &apiLayerCount, NULL);
	printf("Loader found %d api layers%s", apiLayerCount,
	       apiLayerCount == 0 ? "\n" : ": ");
	XrApiLayerProperties apiLayerProperties[512];
	memset(apiLayerProperties, 0, apiLayerCount * sizeof(XrApiLayerProperties));

	for (uint32_t i = 0; i < apiLayerCount; i++) {
		apiLayerProperties[i].type = XR_TYPE_API_LAYER_PROPERTIES;
		apiLayerProperties[i].next = NULL;
	}
	xrEnumerateApiLayerProperties(apiLayerCount, &apiLayerCount,
	                              apiLayerProperties);
	for (uint32_t i = 0; i < apiLayerCount; i++) {
		if (strcmp(apiLayerProperties[i].layerName,
		                  "XR_APILAYER_LUNARG_core_validation") == 0) {
			lunargCoreValidationSupported = true;
		}
		printf("%s%s", apiLayerProperties[i].layerName,
		       i < apiLayerCount - 1 ? ", " : "\n");
	}

	// --- Create XrInstance
	const char* const enabledExtensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};

	XrInstanceCreateInfo instanceCreateInfo = {
	    .type = XR_TYPE_INSTANCE_CREATE_INFO,
	    .next = NULL,
	    .createFlags = 0,
	    .enabledExtensionCount = 1,
	    .enabledApiLayerCount = 0,
	    .enabledApiLayerNames = NULL,
	    .applicationInfo =
	        {
	            .applicationName = "OpenXR OpenGL Example",
	            .engineName = "Candle",
	            .applicationVersion = 1,
	            .engineVersion = 0,
	            .apiVersion = XR_CURRENT_API_VERSION,
	        },
	};
	instanceCreateInfo.enabledExtensionNames = enabledExtensions;
	const bool_t useCoreValidationLayer = true;

	if (lunargCoreValidationSupported && useCoreValidationLayer) {
		instanceCreateInfo.enabledApiLayerCount = 1;
		const char* const enabledApiLayers[] = {
		    "XR_APILAYER_LUNARG_core_validation"};
		instanceCreateInfo.enabledApiLayerNames = enabledApiLayers;
	}

	result = xrCreateInstance(&instanceCreateInfo, &self->instance);
	if (!xr_result(NULL, result, "failed to create XR instance."))
		return 0;

	// Checking instance properties is optional!
	{
		XrInstanceProperties instanceProperties = {
		    .type = XR_TYPE_INSTANCE_PROPERTIES,
		    .next = NULL,
		};

		result = xrGetInstanceProperties(self->instance, &instanceProperties);
		if (!xr_result(NULL, result, "failed to get instance info"))
			return 0;

		printf("Runtime Name: %s\n", instanceProperties.runtimeName);
		printf("Runtime Version: %d.%d.%d\n",
		       XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
		       XR_VERSION_MINOR(instanceProperties.runtimeVersion),
		       XR_VERSION_PATCH(instanceProperties.runtimeVersion));
	}


	xrGetInstanceProcAddr(self->instance, "xrCreateDebugUtilsMessengerEXT",    (PFN_xrVoidFunction *)(&ext_xrCreateDebugUtilsMessengerEXT   ));
	xrGetInstanceProcAddr(self->instance, "xrDestroyDebugUtilsMessengerEXT",   (PFN_xrVoidFunction *)(&ext_xrDestroyDebugUtilsMessengerEXT  ));

	// Set up a really verbose debug log! Great for dev, but turn this off or
	// down for final builds. WMR doesn't produce much output here, but it
	// may be more useful for other runtimes?
	// Here's some extra information aboutxr_instance the message types and severities:
	// https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#debug-message-categorization
	XrDebugUtilsMessengerCreateInfoEXT debug_info = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
	debug_info.messageTypes =
		XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
		XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
		XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
	debug_info.messageSeverities =
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debug_info.userCallback = _debug_cb; // Start up the debug utils!
	if (ext_xrCreateDebugUtilsMessengerEXT)
		ext_xrCreateDebugUtilsMessengerEXT(self->instance, &debug_info, &xr_debug);
	

	// --- Create XrSystem
	XrSystemGetInfo systemGetInfo = {.type = XR_TYPE_SYSTEM_GET_INFO,
	                                 .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
	                                 .next = NULL};

	XrSystemId systemId;
	result = xrGetSystem(self->instance, &systemGetInfo, &systemId);
	if (!xr_result(self->instance, result,
	               "failed to get system for HMD form factor."))
		return 0;

	printf("Successfully got XrSystem %lu for HMD form factor\n", (unsigned long)systemId);

	// checking system properties is optional!
	{
		XrSystemProperties systemProperties = {
		    .type = XR_TYPE_SYSTEM_PROPERTIES,
		    .next = NULL,
		    .graphicsProperties = {0},
		    .trackingProperties = {0},
		};

		result = xrGetSystemProperties(self->instance, systemId, &systemProperties);
		if (!xr_result(self->instance, result, "failed to get System properties"))
			return 0;

		printf("System properties for system %lu: \"%s\", vendor ID %d\n",
		       (unsigned long)systemProperties.systemId, systemProperties.systemName,
		       (int)systemProperties.vendorId);
		printf("\tMax layers          : %d\n",
		       systemProperties.graphicsProperties.maxLayerCount);
		printf("\tMax swapchain height: %d\n",
		       systemProperties.graphicsProperties.maxSwapchainImageHeight);
		printf("\tMax swapchain width : %d\n",
		       systemProperties.graphicsProperties.maxSwapchainImageWidth);
		printf("\tOrientation Tracking: %d\n",
		       systemProperties.trackingProperties.orientationTracking);
		printf("\tPosition Tracking   : %d\n",
		       systemProperties.trackingProperties.positionTracking);
	}

	// --- Enumerate and set up Views
	uint32_t viewConfigurationCount;
	result = xrEnumerateViewConfigurations(self->instance, systemId, 0,
	                                       &viewConfigurationCount, NULL);
	if (!xr_result(self->instance, result,
	               "failed to get view configuration count"))
		return 0;

	printf("Runtime supports %d view configurations\n", viewConfigurationCount);

	XrViewConfigurationType viewConfigurations[512];
	result = xrEnumerateViewConfigurations(
	    self->instance, systemId, viewConfigurationCount, &viewConfigurationCount,
	    viewConfigurations);
	if (!xr_result(self->instance, result,
	               "failed to enumerate view configurations!"))
		return 0;

	XrViewConfigurationType stereoViewConfigType =
	    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;


	uint32_t blend_count = 0;
	xrEnumerateEnvironmentBlendModes(self->instance, systemId, stereoViewConfigType ,
		1, &blend_count, &self->xr_blend);
	/* Checking if the runtime supports the view configuration we want to use is
	 * optional! If stereoViewConfigType.type is unset after the loop, the runtime
	 * does not support Stereo VR. */
	{
		XrViewConfigurationProperties stereoViewConfigProperties = {0};
		for (uint32_t i = 0; i < viewConfigurationCount; ++i) {
			XrViewConfigurationProperties properties = {
			    .type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES, .next = NULL};

			result = xrGetViewConfigurationProperties(
			    self->instance, systemId, viewConfigurations[i], &properties);
			if (!xr_result(self->instance, result,
			               "failed to get view configuration info %d!", i))
				return 0;

			if (viewConfigurations[i] == stereoViewConfigType &&
			    /* just to verify */ properties.viewConfigurationType ==
			        stereoViewConfigType) {
				printf("Runtime supports our VR view configuration, yay!\n");
				stereoViewConfigProperties = properties;
			} else {
				printf(
				    "Runtime supports a view configuration we are not interested in: "
				    "%d\n",
				    properties.viewConfigurationType);
			}
		}
		if (stereoViewConfigProperties.type !=
		    XR_TYPE_VIEW_CONFIGURATION_PROPERTIES) {
			printf("Couldn't get VR View Configuration from Runtime!\n");
			return 0;
		}

		printf("VR View Configuration:\n");
		printf("\tview configuratio type: %d\n",
		       stereoViewConfigProperties.viewConfigurationType);
		printf("\tFOV mutable           : %s\n",
		       stereoViewConfigProperties.fovMutable ? "yes" : "no");
	}

	result = xrEnumerateViewConfigurationViews(self->instance, systemId,
	                                           stereoViewConfigType, 0,
	                                           &self->view_count, NULL);
	if (!xr_result(self->instance, result,
	               "failed to get view configuration view count!"))
		return 0;

	self->configuration_views =
	    malloc(sizeof(XrViewConfigurationView) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		self->configuration_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		self->configuration_views[i].next = NULL;
	}

	result = xrEnumerateViewConfigurationViews(
	    self->instance, systemId, stereoViewConfigType, self->view_count,
	    &self->view_count, self->configuration_views);
	if (!xr_result(self->instance, result,
	               "failed to enumerate view configuration views!"))
		return 0;

	printf("View count: %d\n", self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		printf("View %d:\n", i);
		printf("\tResolution       : Recommended %dx%d, Max: %dx%d\n",
		       self->configuration_views[i].recommendedImageRectWidth,
		       self->configuration_views[i].recommendedImageRectHeight,
		       self->configuration_views[i].maxImageRectWidth,
		       self->configuration_views[i].maxImageRectHeight);
		printf("\tSwapchain Samples: Recommended: %d, Max: %d)\n",
		       self->configuration_views[i].recommendedSwapchainSampleCount,
		       self->configuration_views[i].maxSwapchainSampleCount);
	}

	// For all graphics APIs, it's required to make the
	// "xrGet...GraphicsRequirements" call before creating a session. The
	// information retrieved by the OpenGL version of this call isn't very useful.
	// Other APIs have more useful requirements.
	{
		XrGraphicsRequirementsOpenGLKHR opengl_reqs = {
		    .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR, .next = NULL};

		PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = NULL;
		result = xrGetInstanceProcAddr(self->instance, "xrGetOpenGLGraphicsRequirementsKHR",
		                               (PFN_xrVoidFunction *)&pfnGetOpenGLGraphicsRequirementsKHR);
		result = pfnGetOpenGLGraphicsRequirementsKHR(self->instance, systemId, &opengl_reqs);
		if (!xr_result(self->instance, result,
		               "failed to get OpenGL graphics requirements!"))
			return 0;

		XrVersion desired_opengl_version = XR_MAKE_VERSION(4, 5, 0);
		if (desired_opengl_version > opengl_reqs.maxApiVersionSupported ||
		    desired_opengl_version < opengl_reqs.minApiVersionSupported) {
			printf(
			    "We want OpenGL %d.%d.%d, but runtime only supports OpenGL "
			    "%d.%d.%d - %d.%d.%d!\n",
			    XR_VERSION_MAJOR(desired_opengl_version),
			    XR_VERSION_MINOR(desired_opengl_version),
			    XR_VERSION_PATCH(desired_opengl_version),
			    XR_VERSION_MAJOR(opengl_reqs.minApiVersionSupported),
			    XR_VERSION_MINOR(opengl_reqs.minApiVersionSupported),
			    XR_VERSION_PATCH(opengl_reqs.minApiVersionSupported),
			    XR_VERSION_MAJOR(opengl_reqs.maxApiVersionSupported),
			    XR_VERSION_MINOR(opengl_reqs.maxApiVersionSupported),
			    XR_VERSION_PATCH(opengl_reqs.maxApiVersionSupported));
			return 0;
		}
	}

	// --- Create session

	self->graphics_binding_gl.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
	self->graphics_binding_gl.next = NULL;
	self->graphics_binding_gl.hGLRC = wglGetCurrentContext();
	self->graphics_binding_gl.hDC = wglGetCurrentDC();

	XrSessionCreateInfo session_create_info = {.type =
	                                               XR_TYPE_SESSION_CREATE_INFO,
	                                           .next = &self->graphics_binding_gl,
	                                           .systemId = systemId};


	result =
	    xrCreateSession(self->instance, &session_create_info, &self->session);
	if (!xr_result(self->instance, result, "failed to create session"))
		return 0;

	// --- Check supported reference spaces
	// we don't *need* to check the supported reference spaces if we're confident
	// the runtime will support whatever we use
	{
		uint32_t referenceSpacesCount;
		result = xrEnumerateReferenceSpaces(self->session, 0, &referenceSpacesCount,
		                                    NULL);
		if (!xr_result(self->instance, result,
		               "Getting number of reference spaces failed!"))
			return 0;

		XrReferenceSpaceType referenceSpaces[512];
		for (uint32_t i = 0; i < referenceSpacesCount; i++)
			referenceSpaces[i] = XR_REFERENCE_SPACE_TYPE_VIEW;
		result = xrEnumerateReferenceSpaces(self->session, referenceSpacesCount,
		                                    &referenceSpacesCount, referenceSpaces);
		if (!xr_result(self->instance, result,
		               "Enumerating reference spaces failed!"))
			return 0;

		bool_t stageSpaceSupported = false;
		bool_t localSpaceSupported = false;
		printf("Runtime supports %d reference spaces: ", referenceSpacesCount);
		for (uint32_t i = 0; i < referenceSpacesCount; i++) {
			if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_LOCAL) {
				printf("XR_REFERENCE_SPACE_TYPE_LOCAL%s",
				       i == referenceSpacesCount - 1 ? "\n" : ", ");
				localSpaceSupported = true;
			} else if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
				printf("XR_REFERENCE_SPACE_TYPE_STAGE%s",
				       i == referenceSpacesCount - 1 ? "\n" : ", ");
				stageSpaceSupported = true;
			} else if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_VIEW) {
				printf("XR_REFERENCE_SPACE_TYPE_VIEW%s",
				       i == referenceSpacesCount - 1 ? "\n" : ", ");
			} else {
				printf("some other space with type %u%s", referenceSpaces[i],
				       i == referenceSpacesCount - 1 ? "\n" : ", ");
			}
		}

		if (/* !stageSpaceSupported || */ !localSpaceSupported) {
			printf(
			    "runtime does not support required spaces! stage: %s, "
			    "local: %s\n",
			    stageSpaceSupported ? "supported" : "NOT SUPPORTED",
			    localSpaceSupported ? "supported" : "NOT SUPPORTED");
			return 0;
		}
	}

	XrPosef identityPose = {.orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
	                        .position = {.x = 0, .y = 0, .z = 0}};

	XrReferenceSpaceCreateInfo localSpaceCreateInfo = {
	    .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
	    .next = NULL,
	    .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
	    .poseInReferenceSpace = identityPose};

	result = xrCreateReferenceSpace(self->session, &localSpaceCreateInfo,
	                                &self->local_space);
	if (!xr_result(self->instance, result, "failed to create local space!"))
		return 0;

	// --- Begin session
	XrSessionBeginInfo sessionBeginInfo = {.type = XR_TYPE_SESSION_BEGIN_INFO,
	                                       .next = NULL,
	                                       .primaryViewConfigurationType =
	                                           stereoViewConfigType};
	result = xrBeginSession(self->session, &sessionBeginInfo);
	if (!xr_result(self->instance, result, "failed to begin session!"))
		return 0;
	printf("Session started!\n");



	// --- Create Swapchains
	uint32_t swapchainFormatCount;
	result = xrEnumerateSwapchainFormats(self->session, 0, &swapchainFormatCount,
	                                     NULL);
	if (!xr_result(self->instance, result,
	               "failed to get number of supported swapchain formats"))
		return 0;

	printf("Runtime supports %d swapchain formats\n", swapchainFormatCount);
	int64_t swapchainFormats[512];
	result = xrEnumerateSwapchainFormats(self->session, swapchainFormatCount,
	                                     &swapchainFormatCount, swapchainFormats);
	if (!xr_result(self->instance, result,
	               "failed to enumerate swapchain formats"))
		return 0;

	// TODO: Determine which format we want to use instead of using the first one
	int64_t swapchainFormatToUse = swapchainFormats[0];

	/* First create swapchains and query the length for each swapchain. */
	self->swapchains = malloc(sizeof(XrSwapchain) * self->view_count);

	uint32_t swapchainLength[512];

retry:
	for (uint32_t i = 0; i < self->view_count; i++) {
		XrSwapchainCreateInfo swapchainCreateInfo = {
		    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		    /* .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | */
		    /*               XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT, */
		    .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
		    .createFlags = 0,
		    .format = swapchainFormatToUse,
		    /* .format = GL_SRGB8, */
		    .sampleCount = 1,
		    .width = self->configuration_views[i].recommendedImageRectWidth,
		    .height = self->configuration_views[i].recommendedImageRectHeight,
		    .faceCount = 1,
		    .arraySize = 1,
		    .mipCount = 1,
		    .next = NULL,
		};

		result = xrCreateSwapchain(self->session, &swapchainCreateInfo,
		                           &self->swapchains[i]);
		if (!xr_result(self->instance, result, "failed to create swapchain %d!", i))
			goto retry;

		result = xrEnumerateSwapchainImages(self->swapchains[i], 0,
		                                    &swapchainLength[i], NULL);
		if (!xr_result(self->instance, result, "failed to enumerate swapchains"))
			return 0;
		printf("Created swapchain %d\n", i);
	}

	// allocate one array of images and framebuffers per view
	self->images = malloc(sizeof(XrSwapchainImageOpenGLKHR*) * self->view_count);
	self->framebuffers = malloc(sizeof(GLuint*) * self->view_count);

	for (uint32_t i = 0; i < self->view_count; i++) {
		// allocate array of images and framebuffers for this view
		self->images[i] =
		    malloc(sizeof(XrSwapchainImageOpenGLKHR) * swapchainLength[i]);

		// get OpenGL image ids from runtime
		for (uint32_t j = 0; j < swapchainLength[i]; j++) {
			self->images[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			self->images[i][j].next = NULL;
		}
		result = xrEnumerateSwapchainImages(
		    self->swapchains[i], swapchainLength[i], &swapchainLength[i],
		    (XrSwapchainImageBaseHeader*)self->images[i]);
		if (!xr_result(self->instance, result,
		               "failed to enumerate swapchain images"))
			return 0;

		// framebuffers are not managed or mandated by OpenXR, it's just how we
		// happen to render into textures in this example
		self->framebuffers[i] = malloc(sizeof(GLuint) * swapchainLength[i]);
		glGenFramebuffers(swapchainLength[i], self->framebuffers[i]);
	}

	c_openxr_init_actions(self);
	self->initiated = true;
	return 0;
}

void c_openxr_init(c_openxr_t *self)
{
	self->internal = calloc(sizeof(*self->internal), 1);
	self->internal->previous_view[0] = mat4();
	self->internal->previous_view[1] = mat4();
}

void renderFrame(renderer_t *renderer, int w, int h,
		 mat4_t absolute,
                 mat4_t projectionmatrix,
                 mat4_t cammatrix,
		 mat4_t *previous_view,
                 GLuint framebuffer)
{

	if (renderer)
	{
		renderer_resize(renderer, w, h);
		/* renderer_update_projection(renderer); */
		renderer->glvars[0].projection = projectionmatrix;
		renderer->glvars[0].inv_projection = mat4_invert(renderer->glvars[0].projection); 

		/* renderer->glvars[0].model = mat4_mul(absolute, cammatrix); */
		renderer->glvars[0].model = mat4_mul(absolute, cammatrix);
		renderer->glvars[0].inv_model = mat4_invert(renderer->glvars[0].model);
		renderer->glvars[0].pos = vec4_xyz(mat4_mul_vec4(renderer->glvars[0].model,
					vec4(0.0f, 0.0f, 0.0f, 1.0f)));
		renderer->glvars[0].previous_view = *previous_view; 
		renderer->ubo_changed[0] = true;

		*previous_view = renderer->glvars[0].inv_model;

		renderer_draw(renderer);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer->output->frame_buffer[0]);
		glBlitFramebuffer((GLint)0,     // srcX0
		                  (GLint)0,     // srcY0
		                  (GLint)w,     // srcX1
		                  (GLint)h,     // srcY1
		                  (GLint)0,     // dstX0
		                  (GLint)0,     // dstY0
		                  (GLint)w,     // dstX1
		                  (GLint)h,     // dstY1
		                  (GLbitfield)GL_COLOR_BUFFER_BIT, // mask
		                  (GLenum)GL_LINEAR);              // filter

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}
	glFinish();

	/* if (leftHand) { */
	/* 	mat4_t leftMatrix; */
	/* 	vec3 uniformScale = vec3(.05f, .05f, .2f); */
	/* 	mat4_CreateTranslationRotationScaleRotate( */
	/* 	    &leftMatrix, &leftHand->pose.position, &leftHand->pose.orientation, */
	/* 	    &uniformScale); */
	/* 	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)leftMatrix.m); */
	/* 	glUniform3f(color, 1.0, 0.5, 0.5); */
	/* 	glDrawArrays(GL_TRIANGLES, 0, 36); */
	/* } */

	/* if (rightHand) { */
	/* 	mat4_t rightMatrix; */
	/* 	vec3 uniformScale = {.x = .05f, .y = .05f, .z = .2f}; */
	/* 	mat4_CreateTranslationRotationScaleRotate( */
	/* 	    &rightMatrix, &rightHand->pose.position, &rightHand->pose.orientation, */
	/* 	    &uniformScale); */
	/* 	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)rightMatrix.m); */
	/* 	glUniform3f(color, 0.5, 1.0, 0.5); */
	/* 	glDrawArrays(GL_TRIANGLES, 0, 36); */
	/* } */


	/* if (viewIndex == 0) { */
	/* 	glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer); */
	/* 	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); */
	/* 	glBlitFramebuffer((GLint)0,     // srcX0 */
	/* 	                  (GLint)0,     // srcY0 */
	/* 	                  (GLint)w,     // srcX1 */
	/* 	                  (GLint)h,     // srcY1 */
	/* 	                  (GLint)0,     // dstX0 */
	/* 	                  (GLint)0,     // dstY0 */
	/* 	                  (GLint)w / 2, // dstX1 */
	/* 	                  (GLint)h / 2, // dstY1 */
	/* 	                  (GLbitfield)GL_COLOR_BUFFER_BIT, // mask */
	/* 	                  (GLenum)GL_LINEAR);              // filter */
		/* glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); */
		/* glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); */
	/* } */
}

static void c_openxr_init_actions(struct openxr_internal *self)
{

	XrResult result;
	XrActionSetCreateInfo exampleSetInfo = {
		.type = XR_TYPE_ACTION_SET_CREATE_INFO,
		.next = NULL,
		.priority = 0,
		.actionSetName = "mainset",
		.localizedActionSetName = "Candle Action Set"
	};
	result = xrCreateActionSet(self->instance, &exampleSetInfo, &self->main_set);
	mesh_t *right = sauces("body_right.obj");
	mesh_t *left = sauces("body_left.obj");
	mat_t *mat_right = mat_new("right", "default");
	mat_t *mat_left = mat_new("left", "default");

	mat1t(mat_left, ref("albedo.texture"), sauces("valve_controller_knu_1_0_left_diff.png"));
	mat1f(mat_left, ref("albedo.blend"), 1.0f);
	mat1t(mat_left, ref("roughness.texture"), sauces("valve_controller_knu_1_0_left_spec.png"));
	mat1f(mat_left, ref("roughness.blend"), 1.0f);
	mat1f(mat_left, ref("metalness.value"), 0.5f);

	mat1t(mat_right, ref("albedo.texture"), sauces("valve_controller_knu_1_0_right_diff.png"));
	mat1f(mat_right, ref("albedo.blend"), 1.0f);
	mat1t(mat_right, ref("roughness.texture"), sauces("valve_controller_knu_1_0_right_spec.png"));
	mat1f(mat_right, ref("roughness.blend"), 1.0f);
	mat1f(mat_right, ref("metalness.value"), 0.5f);

	entity_new({
		c_xrbody_new("/user/hand/right");
		c_model_new(right, mat_right, true, true);
	});
	entity_new({
		c_xrbody_new("/user/hand/left");
		c_model_new(left, mat_left, true, true);
	});

	/* for (int i = 0; i < 2; i++) */
	do
	{
		XrPath interactionProfilePath;
		/* if (i == 0) */
			/* result = xrStringToPath(self->instance, "/interaction_profiles/khr/simple_controller", &interactionProfilePath); */
		/* else */
		result = xrStringToPath(self->instance, "/interaction_profiles/valve/index_controller", &interactionProfilePath);
		if (!xr_result(self->instance, result, "failed to get interaction profile"))
			continue;
		const XrInteractionProfileSuggestedBinding suggestedBindings = {
			.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
			.next = NULL,
			.interactionProfile = interactionProfilePath,
			.countSuggestedBindings = self->bindings_num,
			.suggestedBindings = self->bindings
		};

		result = xrSuggestInteractionProfileBindings(self->instance, &suggestedBindings);
		if (!xr_result(self->instance, result, "failed to suggest bindings"))
			continue;

	} while (0);



	XrSessionActionSetsAttachInfo attachInfo = {
		.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
		.next = NULL,
		.countActionSets = 1,
		.actionSets = &self->main_set
	};

	result = xrAttachSessionActionSets(self->session, &attachInfo);
	if (!xr_result(self->instance, result, "failed to attach action set"))
		return;
}

static mat4_t mat4_asymmetrical_perspective(const float tanAngleLeft,
		const float tanAngleRight,
		const float tanAngleUp,
		float const tanAngleDown,
		const float nearZ,
		const float farZ)
{
	mat4_t M;
	const float tanAngleWidth = tanAngleRight - tanAngleLeft;
	const float tanAngleHeight = tanAngleUp - tanAngleDown;
	const float offsetZ = nearZ;

	// Normal projection
	M._[0][0] = 2.0f / tanAngleWidth;
	M._[1][0] = 0.0f;
	M._[2][0] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
	M._[3][0] = 0.0f;

	M._[0][1] = 0.0f;
	M._[1][1] = 2.0f / tanAngleHeight;
	M._[2][1] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
	M._[3][1] = 0.0f;

	M._[0][2] = 0.0f;
	M._[1][2] = 0.0f;
	M._[2][2] = -(farZ + offsetZ) / (farZ - nearZ);
	M._[3][2] = -(farZ * (nearZ + offsetZ)) / (farZ - nearZ);

	M._[0][3] = 0.0f;
	M._[1][3] = 0.0f;
	M._[2][3] = -1.0f;
	M._[3][3] = 0.0f;
	return M;
}

int c_openxr_pre_draw(c_openxr_t *self)
{
	bool_t running = true;
	bool_t isVisible = true;
	XrResult result;

	XrEventDataBuffer runtimeEvent = {.type = XR_TYPE_EVENT_DATA_BUFFER,
					  .next = NULL};
	bool_t isStopping = false;
	XrResult pollResult = xrPollEvent(self->internal->instance, &runtimeEvent);
	if (pollResult == XR_SUCCESS) {
		switch (runtimeEvent.type) {
		case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
			XrEventDataEventsLost* event = (XrEventDataEventsLost*)&runtimeEvent;
			printf("EVENT: %d events data lost!\n", event->lostEventCount);
			// do we care if the runtime loses events?
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
			XrEventDataInstanceLossPending* event =
			    (XrEventDataInstanceLossPending*)&runtimeEvent;
			printf("EVENT: instance loss pending at %lu! Destroying instance.\n",
			       (unsigned long)event->lossTime);
			// Handling this: spec says destroy instance
			// (can optionally recreate it)
			running = false;
			break;
		}
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			printf("EVENT: session state changed ");
			XrEventDataSessionStateChanged* event =
			    (XrEventDataSessionStateChanged*)&runtimeEvent;
			XrSessionState state = event->state;

			// it would be better to handle each state change
			isVisible = event->state <= XR_SESSION_STATE_FOCUSED;
			printf("to %d. Visible: %d", state, isVisible);
			if (event->state >= XR_SESSION_STATE_STOPPING) {
				printf("\nSession is in state stopping...");
				isStopping = true;
			}
			printf("\n");
			break;
		}
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
			printf("EVENT: reference space change pending!\n");
			XrEventDataReferenceSpaceChangePending* event =
			    (XrEventDataReferenceSpaceChangePending*)&runtimeEvent;
			(void)event;
			// TODO: do something
			break;
		}
		case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
			printf("EVENT: interaction profile changed!\n");
			XrEventDataInteractionProfileChanged* event =
			    (XrEventDataInteractionProfileChanged*)&runtimeEvent;
			(void)event;
			// TODO: do something
			break;
		}

		case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR: {
			printf("EVENT: visibility mask changed!!\n");
			XrEventDataVisibilityMaskChangedKHR* event =
			    (XrEventDataVisibilityMaskChangedKHR*)&runtimeEvent;
			(void)event;
			// this event is from an extension
			break;
		}
		case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
			printf("EVENT: perf settings!\n");
			XrEventDataPerfSettingsEXT* event =
			    (XrEventDataPerfSettingsEXT*)&runtimeEvent;
			(void)event;
			// this event is from an extension
			break;
		}
		default: printf("Unhandled event type %d\n", runtimeEvent.type);
		}
	} else if (pollResult == XR_EVENT_UNAVAILABLE) {
		// this is the usual case
	} else {
		printf("failed to poll events!\n");
		return CONTINUE;
	}

	if (isStopping) {
		printf("Ending session...\n");
		xrEndSession(self->internal->session);
		return CONTINUE;
	}
	//
	// --- Wait for our turn to do head-pose dependent computation and render a
	// frame
	self->internal->frame_state.type = XR_TYPE_FRAME_STATE;
	self->internal->frame_state.next = NULL;
	XrFrameWaitInfo frameWaitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO,
					 .next = NULL};
	result = xrWaitFrame(self->internal->session, &frameWaitInfo, &self->internal->frame_state);
	if (!xr_result(self->internal->instance, result,
		       "xrWaitFrame() was not successful, exiting..."))
		return CONTINUE;

	const XrActiveActionSet activeActionSet = {
		.actionSet = self->internal->main_set,
		.subactionPath = XR_NULL_PATH,
	};

	XrActionsSyncInfo syncInfo = {
		.type = XR_TYPE_ACTIONS_SYNC_INFO,
		.countActiveActionSets = 1,
		.activeActionSets = &activeActionSet,
	};

	result = xrSyncActions(self->internal->session, &syncInfo);
	xr_result(self->internal->instance, result, "failed to sync actions!");


	return CONTINUE;
}


int c_openxr_draw(c_openxr_t *self)
{
	XrResult result;
	if (self->internal->failed)
		return CONTINUE;
	if (!self->internal->initiated)
	{
		self->internal->failed = true;
		openxr_internal_init(self->internal);
		if (self->internal->initiated)
			self->internal->failed = false;
		return CONTINUE;
	}

	// --- Create projection matrices and view matrices for each eye
	XrViewLocateInfo viewLocateInfo = {
	    .type = XR_TYPE_VIEW_LOCATE_INFO,
	    .next = NULL,
	    .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	    .displayTime = self->internal->frame_state.predictedDisplayTime,
	    .space = self->internal->local_space};

	XrView views[512];
	for (uint32_t i = 0; i < self->internal->view_count; i++) {
		views[i].type = XR_TYPE_VIEW;
		views[i].next = NULL;
	};

	XrViewState viewState = {.type = XR_TYPE_VIEW_STATE, .next = NULL};
	uint32_t viewCountOutput;
	result = xrLocateViews(self->internal->session, &viewLocateInfo, &viewState,
			       self->internal->view_count, &viewCountOutput, views);
	if (!xr_result(self->internal->instance, result, "Could not locate views"))
		return CONTINUE;

	// --- Begin frame
	XrFrameBeginInfo frameBeginInfo = {.type = XR_TYPE_FRAME_BEGIN_INFO,
					   .next = NULL};

	result = xrBeginFrame(self->internal->session, &frameBeginInfo);
	if (!xr_result(self->internal->instance, result, "failed to begin frame!"))
		exit(1);

	XrCompositionLayerProjectionView projection_views[512];

	mat4_t start = mat4();
	if (self->renderer)
		start = self->renderer->glvars[0].model;

	// render each eye and fill projection_views with the result
	for (uint32_t i = 0; i < self->internal->view_count; i++) {
		mat4_t projection;
		const XrFovf fov = views[i].fov;
		const float tanLeft = tanf(fov.angleLeft);
		const float tanRight = tanf(fov.angleRight);
		const float tanDown = tanf(fov.angleDown);
		const float tanUp = tanf(fov.angleUp);
		projection = mat4_asymmetrical_perspective(tanLeft, tanRight, tanUp,
				tanDown, 0.1f, 1000.f);
		/* projection = mat4_perspective((tanUp - tanDown), 1.f, 0.1f, 1000.f); */

		vec3_t translation = vec3(_vec3(views[i].pose.position));
		vec4_t rot_quat = vec4(_vec4(views[i].pose.orientation));
		mat4_t rot_matrix = quat_to_mat4(rot_quat);

		mat4_t model_matrix = mat4();
		model_matrix = mat4_mul(model_matrix, mat4_translate(translation));
		model_matrix = mat4_mul(model_matrix, rot_matrix);

		XrSwapchainImageAcquireInfo swapchainImageAcquireInfo = {
		    .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, .next = NULL};
		uint32_t bufferIndex;
		result = xrAcquireSwapchainImage(
		    self->internal->swapchains[i], &swapchainImageAcquireInfo, &bufferIndex);
		if (!xr_result(self->internal->instance, result,
			       "failed to acquire swapchain image!"))
			exit(1);

		XrSwapchainImageWaitInfo swapchainImageWaitInfo = {
		    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
		    .next = NULL,
		    .timeout = 1000};
		result =
		    xrWaitSwapchainImage(self->internal->swapchains[i], &swapchainImageWaitInfo);
		if (!xr_result(self->internal->instance, result,
			       "failed to wait for swapchain image!"))
			exit(1);

		projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		projection_views[i].next = NULL;
		projection_views[i].pose = views[i].pose;
		projection_views[i].fov = views[i].fov;
		projection_views[i].subImage.swapchain = self->internal->swapchains[i];
		projection_views[i].subImage.imageArrayIndex = 0;
		projection_views[i].subImage.imageRect.offset.x = 0;
		projection_views[i].subImage.imageRect.offset.y = 0;
		projection_views[i].subImage.imageRect.extent.width =
		    self->internal->configuration_views[i].recommendedImageRectWidth;
		projection_views[i].subImage.imageRect.extent.height =
		    self->internal->configuration_views[i].recommendedImageRectHeight;


		uint32_t framebuffer = self->internal->framebuffers[i][bufferIndex];
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
				self->internal->images[i][bufferIndex].image, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		renderFrame(self->renderer,
				self->internal->configuration_views[i].recommendedImageRectWidth,
				self->internal->configuration_views[i].recommendedImageRectHeight,
				start, projection, model_matrix, &self->internal->previous_view[i],
				framebuffer);

		XrSwapchainImageReleaseInfo swapchainImageReleaseInfo = {
		    .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, .next = NULL};
		result = xrReleaseSwapchainImage(self->internal->swapchains[i],
						 &swapchainImageReleaseInfo);
		if (!xr_result(self->internal->instance, result,
			       "failed to release swapchain image!"))
			exit(1);
	}

	XrCompositionLayerProjection projectionLayer = {
	    .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	    .next = NULL,
	    .layerFlags = 0,
	    .space = self->internal->local_space,
	    .viewCount = self->internal->view_count,
	    .views = projection_views,
	};

	const XrCompositionLayerBaseHeader* const submittedLayers[] = {
	    (const XrCompositionLayerBaseHeader* const) & projectionLayer };
	    /* (const XrCompositionLayerBaseHeader* const) & quadLayer}; */
	XrFrameEndInfo frameEndInfo = {
	    .type = XR_TYPE_FRAME_END_INFO,
	    .displayTime = self->internal->frame_state.predictedDisplayTime,
	    .layerCount = sizeof(submittedLayers) / sizeof(submittedLayers[0]),
	    .layers = submittedLayers,
	    .environmentBlendMode = self->internal->xr_blend,
	    .next = NULL};
	result = xrEndFrame(self->internal->session, &frameEndInfo);
	if (!xr_result(self->internal->instance, result, "failed to end frame!"))
		return CONTINUE;
	return CONTINUE;
}

c_openxr_t *c_openxr_new()
{
	c_openxr_t *self = component_new(ct_openxr);

	return self;
}

void c_openxr_destroy(c_openxr_t *self)
{
	xrDestroySession(self->internal->session);
	xrDestroyInstance(self->internal->instance);
}

void ct_openxr(ct_t *self)
{
	ct_init(self, "openxr", sizeof(c_openxr_t));
	ct_set_init(self, (init_cb)c_openxr_init);
	ct_set_destroy(self, (destroy_cb)c_openxr_destroy);
	ct_add_listener(self, WORLD, 5, ref("world_draw"), c_openxr_draw);
	ct_add_listener(self, WORLD, 102, ref("world_pre_draw"), c_openxr_pre_draw);
}

