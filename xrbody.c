#include "openxr.h"
#include "xrbody.h"
#include "../candle/components/camera.h"
#include "../candle/components/node.h"

#include "internals.h"

int xrbody_internal_init(struct xrbody_internal *self, entity_t entity, const char *path)
{
	XrResult result;
	struct openxr_internal *xr = c_openxr(&SYS)->internal;
	char *end;
	char buffer[256];
#ifdef _WIN32
	sprintf(buffer, "ent_%I64u", entity);
#elif __EMSCRIPTEN__
	sprintf(buffer, "ent_%lld", entity);
#else
	sprintf(buffer, "ent_%ld", entity);
#endif
	end = &buffer[strlen(buffer)];
	result = xrStringToPath(xr->instance, path, &self->path);
	if (!xr_result(xr->instance, result, "failed to create path"))
		return 1;

	{
		XrActionCreateInfo actionInfo = {
			.type = XR_TYPE_ACTION_CREATE_INFO,
			.next = NULL,
			.actionType = XR_ACTION_TYPE_FLOAT_INPUT,
			.countSubactionPaths = 1,
			.subactionPaths = &self->path
		};
		strcpy(end, "_trigger");
		strcpy(actionInfo.actionName, buffer);
		strcpy(end, "_trigger local");
		strcpy(actionInfo.localizedActionName, buffer);

		result = xrCreateAction(xr->main_set, &actionInfo, &self->grabAction);
		if (!xr_result(xr->instance, result, "failed to create grab action"))
			return 1;
	}

	// just an example that could sensibly use one axis of e.g. a thumbstick
	{
		XrActionCreateInfo actionInfo = {
			.type = XR_TYPE_ACTION_CREATE_INFO,
			.next = NULL,
			.actionType = XR_ACTION_TYPE_FLOAT_INPUT,
			.countSubactionPaths = 1,
			.subactionPaths = &self->path
		};
		strcpy(end, "_lever");
		strcpy(actionInfo.actionName, buffer);
		strcpy(end, "_lever local");
		strcpy(actionInfo.localizedActionName, buffer);

		result = xrCreateAction(xr->main_set, &actionInfo, &self->leverAction);
		if (!xr_result(xr->instance, result, "failed to create lever action"))
			return 1;
	}

	{
		XrActionCreateInfo actionInfo = {
			.type = XR_TYPE_ACTION_CREATE_INFO,
			.next = NULL,
			.actionType = XR_ACTION_TYPE_POSE_INPUT,
			.countSubactionPaths = 1,
			.subactionPaths = &self->path
		};
		strcpy(end, "_handpose");
		strcpy(actionInfo.actionName, buffer);
		strcpy(end, "_handpose local");
		strcpy(actionInfo.localizedActionName, buffer);

		result = xrCreateAction(xr->main_set, &actionInfo, &self->poseAction);
		if (!xr_result(xr->instance, result, "failed to create pose action"))
			return 1;
	}

	{
		XrActionCreateInfo actionInfo = {
			.type = XR_TYPE_ACTION_CREATE_INFO,
			.next = NULL,
			.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT,
			.countSubactionPaths = 1,
			.subactionPaths = &self->path
		};
		strcpy(end, "_haptic");
		strcpy(actionInfo.actionName, buffer);
		strcpy(end, "_haptic local");
		strcpy(actionInfo.localizedActionName, buffer);
		result = xrCreateAction(xr->main_set, &actionInfo, &self->hapticAction);
		if (!xr_result(xr->instance, result, "failed to create haptic action"))
			return 1;
	}

	XrActionSpaceCreateInfo actionSpaceInfo = {
		.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
		.next = NULL,
		.action = self->poseAction,
		.poseInActionSpace.orientation.w = 1.f,
		.subactionPath = self->path
	};

	XrPath selectClickPath;
	XrPath triggerValuePath;
	XrPath thumbstickYPath;
	XrPath posePath;
	XrPath hapticPath;

	strcpy(buffer, path);
	end = &buffer[strlen(buffer)];
	strcpy(end, "/input/select/click");
	printf("%s\n", buffer);
	xrStringToPath(xr->instance, buffer, &selectClickPath);
	strcpy(end, "/input/trigger/value");
	printf("%s\n", buffer);
	xrStringToPath(xr->instance, buffer, &triggerValuePath);
	strcpy(end, "/input/thumbstick/y");
	printf("%s\n", buffer);
	xrStringToPath(xr->instance, buffer, &thumbstickYPath);
	strcpy(end, "/input/grip/pose");
	printf("%s\n", buffer);
	xrStringToPath(xr->instance, buffer, &posePath);
	strcpy(end, "/output/haptic");
	printf("%s\n", buffer);
	xrStringToPath(xr->instance, buffer, &hapticPath);

	xr->bindings[xr->bindings_num  ].action = self->poseAction;
	xr->bindings[xr->bindings_num++].binding = posePath;
	xr->bindings[xr->bindings_num  ].action = self->grabAction;
	xr->bindings[xr->bindings_num++].binding = triggerValuePath;
	xr->bindings[xr->bindings_num  ].action = self->leverAction;
	xr->bindings[xr->bindings_num++].binding = thumbstickYPath;
	xr->bindings[xr->bindings_num  ].action = self->hapticAction;
	xr->bindings[xr->bindings_num++].binding = hapticPath;

	result = xrCreateActionSpace(xr->session, &actionSpaceInfo, &self->space);
	if (!xr_result(xr->instance, result, "failed to create left hand pose space"))
		return 1;

	self->initiated = true;
	return 0;
}

void c_xrbody_init(c_xrbody_t *self)
{
	self->internal = calloc(sizeof(*self->internal), 1);
}

int c_xrbody_pre_draw(c_xrbody_t *self)
{
	if (!self->internal->initiated)
		return CONTINUE;
	XrResult result;
	struct openxr_internal *xr = c_openxr(&SYS)->internal;

	XrActionStateFloat grabValue;
	XrActionStateFloat leverValue;
	XrSpaceLocation spaceLocation;
	bool_t spaceLocationValid;

	XrActionStatePose poseState = {
		.type = XR_TYPE_ACTION_STATE_POSE,
		.next = NULL
	};
	{
		XrActionStateGetInfo getInfo = {
			.type = XR_TYPE_ACTION_STATE_GET_INFO,
			.next = NULL,
			.action = self->internal->poseAction,
			.subactionPath = self->internal->path
		};
		result = xrGetActionStatePose(xr->session, &getInfo, &poseState);
		xr_result(xr->instance, result, "failed to get pose value!");
	}

	spaceLocation.type = XR_TYPE_SPACE_LOCATION;
	spaceLocation.next = NULL;

	result = xrLocateSpace(self->internal->space, xr->local_space, xr->frame_state.predictedDisplayTime, &spaceLocation);
	xr_result(xr->instance, result, "failed to locate space %s!", self->path);
	spaceLocationValid =
		//(spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
		(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

	if (c_openxr(&SYS)->renderer) {
                vec3_t grip_rot = vec3(15.392f, 2.071f, 0.303f);
		mat4_t model_matrix = mat4();

		mat4_t start = c_openxr(&SYS)->renderer->glvars[0].model;

		vec3_t translation = vec3(_vec3(spaceLocation.pose.position));
		vec4_t rot_quat = vec4(_vec4(spaceLocation.pose.orientation));
		mat4_t rot_matrix = quat_to_mat4(rot_quat);

		model_matrix = mat4_mul(model_matrix, mat4_translate(translation));
		model_matrix = mat4_mul(model_matrix, rot_matrix);

		model_matrix = mat4_rotate_X(model_matrix,  -grip_rot.x * (M_PI / 180.0f));
		model_matrix = mat4_rotate_Y(model_matrix,  -grip_rot.y * (M_PI / 180.0f));
		model_matrix = mat4_rotate_Z(model_matrix,  -grip_rot.z * (M_PI / 180.0f));

                vec3_t grip_origin = vec3(0.0f, -0.015f, 0.13f);
		model_matrix = mat4_mul(model_matrix, mat4_translate(vec3_inv(grip_origin)));

		c_spatial_set_model(c_spatial(self), mat4_mul(start, model_matrix));
	}

	grabValue.type = XR_TYPE_ACTION_STATE_FLOAT;
	grabValue.next = NULL;
	{
		XrActionStateGetInfo getInfo = {
			.type = XR_TYPE_ACTION_STATE_GET_INFO,
			.next = NULL,
			.action = self->internal->grabAction,
			.subactionPath = self->internal->path
		};

		result = xrGetActionStateFloat(xr->session, &getInfo, &grabValue);
		xr_result(xr->instance, result, "failed to get grab value!");
	}

	if (grabValue.isActive && grabValue.currentState > 0.75) {
		XrHapticVibration vibration =  {
			.type = XR_TYPE_HAPTIC_VIBRATION,
			.next = NULL,
			.amplitude = 0.5,
			.duration = XR_MIN_HAPTIC_DURATION,
			.frequency = XR_FREQUENCY_UNSPECIFIED
		};

		XrHapticActionInfo hapticActionInfo = {
			.type = XR_TYPE_HAPTIC_ACTION_INFO,
			.next = NULL,
			.action = self->internal->hapticAction,
			.subactionPath = self->internal->path
		};
		result = xrApplyHapticFeedback(xr->session, &hapticActionInfo, (const XrHapticBaseHeader*)&vibration);
		xr_result(xr->instance, result, "failed to apply haptic feedback!");
	}


	leverValue.type = XR_TYPE_ACTION_STATE_FLOAT;
	leverValue.next = NULL;
	{
		XrActionStateGetInfo getInfo = {
			.type = XR_TYPE_ACTION_STATE_GET_INFO,
			.next = NULL,
			.action = self->internal->leverAction,
			.subactionPath = self->internal->path
		};

		result = xrGetActionStateFloat(xr->session, &getInfo, &leverValue);
		xr_result(xr->instance, result, "failed to get lever value!");
	}
	if (leverValue.isActive && leverValue.currentState != 0) {
		/* printf("Lever value %s: changed %d: %f\n", self->path, */
				/* leverValue.changedSinceLastSync, leverValue.currentState); */
	}
	return CONTINUE;
}

c_xrbody_t *c_xrbody_new(const char *path)
{
	c_xrbody_t *self = component_new(ct_xrbody);
	strcpy(self->path, path);
	xrbody_internal_init(self->internal, c_entity(self), self->path);
	return self;
}

void ct_xrbody(ct_t *self)
{
	ct_init(self, "xrbody", sizeof(c_xrbody_t));
	ct_set_init(self, (init_cb)c_xrbody_init);
	ct_add_dependency(self, ct_node);
	ct_add_listener(self, WORLD, 101, ref("world_pre_draw"), c_xrbody_pre_draw);
}

