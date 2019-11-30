#include "game.h"

#include "chunks.h"
#include "shapes.h"
#include "util.h"

#include "GL/gl3w.h"
#include "GLFW/glfw3.h"

#include <algorithm> // howbig?
#include <assert.h>
#include <cmath>
#include <math.h>
#include <numeric>
#include <string>
#include <vmath.h> // TODO: Upgrade version, or use better library?
#include <windows.h>



// 1. TODO: Apply C++11 features
// 2. TODO: Apply C++14 features
// 3. TODO: Apply C++17 features
// 4. TODO: Make everything more object-oriented.
//		That way, I can define functions without having to declare them first, and shit.
//		And more good shit comes of it too.
//		Then from WinMain(), just call MyApp a = new MyApp, a.run(); !!

using namespace std;
using namespace vmath;




// Windows main
int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	glfwSetErrorCallback(glfw_onError);
	App::app = new App();
	App::app->run();
}

void App::run() {
	if (!glfwInit()) {
		MessageBox(NULL, "Failed to initialize GLFW.", "GLFW error", MB_OK);
		return;
	}

	// OpenGL 4.5
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

	// using OpenGL core profile
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// remove deprecated functionality (might as well, 'cause I'm using gl3w)
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	// disable MSAA
	glfwWindowHint(GLFW_SAMPLES, info.msaa);

	// debug mode
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, info.debug);

	// create window
	window = glfwCreateWindow(info.width, info.height, info.title.c_str(), nullptr, nullptr);

	if (!window) {
		MessageBox(NULL, "Failed to create window.", "GLFW error", MB_OK);
		return;
	}

	// set this window as current window
	glfwMakeContextCurrent(window);

	// lock mouse into screen, for camera controls
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

	//// TODO: set callbacks
	glfwSetWindowSizeCallback(window, glfw_onResize);
	glfwSetKeyCallback(window, glfw_onKey);
	//glfwSetMouseButtonCallback(window, glfw_onMouseButton);
	glfwSetCursorPosCallback(window, glfw_onMouseMove);
	//glfwSetScrollCallback(window, glfw_onMouseWheel);

	// finally init gl3w
	if (gl3wInit()) {
		MessageBox(NULL, "Failed to initialize OpenGL.", "gl3w error", MB_OK);
		return;
	}

	// set debug message callback
	if (info.debug) {
		if (gl3wIsSupported(4, 3))
		{
			glEnable(GL_DEBUG_OUTPUT);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback((GLDEBUGPROC)gl_onDebugMessage, nullptr);
			glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		}
	}

	char buf[256];
	GLint tmpi;

	auto props = {
		GL_MAX_VERTEX_UNIFORM_COMPONENTS,
		GL_MAX_UNIFORM_LOCATIONS
	};


	for (auto prop : props) {
		glGetIntegerv(prop, &tmpi);
		sprintf(buf, "%x:\t%d\n", prop, tmpi);
		OutputDebugString(buf);
	}

	// Start up app
	startup();


	// run until user presses ESC or tries to close window
	last_render_time = (float)glfwGetTime(); // updated in render()
	while ((glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) && (!glfwWindowShouldClose(window))) {
		// run rendering function
		render((float)glfwGetTime());

		// display drawing buffer on screen
		glfwSwapBuffers(window);

		// poll window system for events
		glfwPollEvents();
	}

	shutdown();
}

void App::startup() {
	const GLfloat(&cube)[108] = shapes::cube_full;

	// set vars
	memset(held_keys, false, sizeof(held_keys));
	glfwGetCursorPos(window, &last_mouse_x, &last_mouse_y); // reset mouse position

	// list of shaders to create program with
	// TODO: Embed these into binary somehow - maybe generate header file with cmake.
	std::vector <std::tuple<std::string, GLenum>> shader_fnames = {
		{ "../src/simple.vs.glsl", GL_VERTEX_SHADER },
		//{"../src/simple.tcs.glsl", GL_TESS_CONTROL_SHADER },
		//{"../src/simple.tes.glsl", GL_TESS_EVALUATION_SHADER },
		{"../src/simple.gs.glsl", GL_GEOMETRY_SHADER },
		{ "../src/simple.fs.glsl", GL_FRAGMENT_SHADER },
	};

	// create program
	rendering_program = compile_shaders(shader_fnames);

	/* OPENGL SETUP */

	// Set up our binding indices
	const GLuint trans_buf_uni_bidx = 0; // transformation buffer's uniform binding-point index
	const GLuint vert_buf_bidx = 0; // vertex buffer's binding-point index
	const GLuint position_attr_idx = 0; // index of 'position' attribute
	const GLuint chunk_types_bidx = 1; // chunk types buffer's binding-point index
	const GLuint chunk_types_attr_idx = 1; // index of 'chunk_type' attribute

	/* HANDLE CUBES FIRST */

	// vao: create VAO for cube[s], so we can tell OpenGL how to use it when it's bound
	glCreateVertexArrays(1, &vao_cube);

	// buffers: create
	glCreateBuffers(1, &vert_buf);
	glCreateBuffers(1, &chunk_types_buf);

	// buffers: allocate space
	glNamedBufferStorage(vert_buf, sizeof(cube), NULL, GL_DYNAMIC_STORAGE_BIT); // allocate enough for all vertices, and allow editing
	glNamedBufferStorage(chunk_types_buf, CHUNK_SIZE * sizeof(uint8_t), NULL, GL_DYNAMIC_STORAGE_BIT); // allocate enough for all vertices, and allow editing

	// buffers: insert static data
	glNamedBufferSubData(vert_buf, 0, sizeof(cube), cube); // vertex positions

	// vao: enable all cube's attributes, 1 at a time
	glEnableVertexArrayAttrib(vao_cube, position_attr_idx);
	glEnableVertexArrayAttrib(vao_cube, chunk_types_attr_idx);

	// vao: set up formats for cube's attributes, 1 at a time
	glVertexArrayAttribFormat(vao_cube, position_attr_idx, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribIFormat(vao_cube, chunk_types_attr_idx, 1, GL_BYTE, 0);

	// vao: set binding points for all attributes, 1 at a time
	//      - 1 buffer per binding point; for clarity, to keep it clear, I should only bind 1 attr per binding point
	glVertexArrayAttribBinding(vao_cube, position_attr_idx, vert_buf_bidx);
	glVertexArrayAttribBinding(vao_cube, chunk_types_attr_idx, chunk_types_bidx);

	// vao: bind buffers to their binding points, 1 at a time
	glVertexArrayVertexBuffer(vao_cube, vert_buf_bidx, vert_buf, 0, sizeof(vec3));
	glVertexArrayVertexBuffer(vao_cube, chunk_types_bidx, chunk_types_buf, 0, sizeof(uint8_t));

	// vao: extra properties
	glBindVertexArray(vao_cube);
	glVertexAttribDivisor(chunk_types_attr_idx, 1);
	glBindVertexArray(0);


	/* HANDLE UNIFORM NOW */

	// create buffers
	glCreateBuffers(1, &trans_buf);

	// bind them
	glBindBufferBase(GL_UNIFORM_BUFFER, trans_buf_uni_bidx, trans_buf); // bind transformation buffer to uniform buffer binding point

	// allocate
	glNamedBufferStorage(trans_buf, sizeof(mat4) * 2 + sizeof(vec2), NULL, GL_DYNAMIC_STORAGE_BIT); // allocate 2 matrices of space for transforms, and allow editing


	/*
	* ETC
	*/

	glPointSize(5.0f);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	// use our program object for rendering
	glUseProgram(rendering_program);

	//// generate a chunk
	//for (int x = 0; x < 5; x++) {
	//	for (int z = 0; z < 5; z++) {
	//		Chunk* chunk = gen_chunk(x, z);
	//		add_chunk(x, z, chunk);
	//	}
	//}


	// load into memory pog
	//glNamedBufferSubData(chunk_types_buf, 0, CHUNK_SIZE * sizeof(uint8_t), chunks[0]); // proj matrix
	glNamedBufferSubData(chunk_types_buf, 0, CHUNK_SIZE * sizeof(uint8_t), get_chunk(0, 0)->data); // proj matrix
}

void App::render(float time) {
	// change in time
	const float dt = time - last_render_time;
	last_render_time = time;

	// update player movement
	update_player_movement(dt);

	// generate nearby chunks
	gen_nearby_chunks();

	// Create Model->World matrix
	float f = (float)time * (float)M_PI * 0.1f;
	mat4 model_world_matrix =
		translate(0.0f, -PLAYER_HEIGHT * 0.9f, 0.0f);

	// Create World->View matrix
	mat4 world_view_matrix =
		rotate_pitch_yaw(char_pitch, char_yaw) *
		translate(-char_position[0], -char_position[1], -char_position[2]); // move relative to you

	// Combine them into Model->View matrix
	mat4 model_view_matrix = world_view_matrix * model_world_matrix;

	// Update projection matrix too, in case if width/height changed
	// NOTE: Careful, if (nearplane/farplane) ratio is too small, things get fucky.
	mat4 proj_matrix = perspective(
		(float)info.vfov, // virtual fov
		(float)info.width / (float)info.height, // aspect ratio
		PLAYER_RADIUS,  // blocks are always at least PLAYER_RADIUS away from camera
		16 * CHUNK_WIDTH // only support 32 chunks for now
	);

	// Draw background color
	const GLfloat color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	glClearBufferfv(GL_COLOR, 0, color);
	glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0); // used for depth test somehow

	// Update transformation buffer with matrices
	glNamedBufferSubData(trans_buf, 0, sizeof(model_view_matrix), model_view_matrix);
	glNamedBufferSubData(trans_buf, sizeof(model_view_matrix), sizeof(proj_matrix), proj_matrix); // proj matrix

	// Update chunk types buffer with chunk types!
	//glNamedBufferSubData(chunk_types_buf, 0, CHUNK_SIZE * sizeof(uint8_t), chunks[0]); // proj matrix

	char buf[256];
	sprintf(buf, "Drawing (took %d ms)\n", (int)(dt * 1000));
	//OutputDebugString(buf);
	vec4 direction = rotate_pitch_yaw(char_pitch, char_yaw) * NORTH_0;
	sprintf(buf, "Position: (%.1f, %.1f, %.1f) | Facing: (%.1f, %.1f, %.1f)\n", char_position[0], char_position[1], char_position[2], direction[0], direction[1], direction[2]);
	//OutputDebugString(buf);

	//// Draw our chunks!
	//glBindVertexArray(vao_cube);
	//glDrawArraysInstanced(GL_TRIANGLES, 0, 36, CHUNK_SIZE);

	// Draw ALL our chunks!
	glBindVertexArray(vao_cube);
	for (auto &[coords_p, chunk] : chunk_map) {
		ivec2 coords = { coords_p.first , coords_p.second };

		glNamedBufferSubData(chunk_types_buf, 0, CHUNK_SIZE * sizeof(uint8_t), chunk->data); // proj matrix
		glNamedBufferSubData(trans_buf, sizeof(model_view_matrix) + sizeof(proj_matrix), sizeof(ivec2), coords); // Add base chunk coordinates to transformation data (temporary solution)
		glDrawArraysInstanced(GL_TRIANGLES, 0, 36, CHUNK_SIZE);

		sprintf(buf, "Drawing at (%d, %d)\n", coords[0], coords[1]);
		//OutputDebugString(buf);
	}
}

void App::update_player_movement(const float dt) {
	char buf[256];
	// update player's movement based on how much time has passed since we last did it

	// Velocity falloff
	//   TODO: Handle walking on blocks, in water, etc. Maybe do it based on friction.
	//   TODO: Tweak values.
	char_velocity *= (float)pow(0.5, dt);
	vec4 norm = normalize(char_velocity);
	for (int i = 0; i < 4; i++) {
		if (char_velocity[i] > 0.0f) {
			char_velocity[i] = (float)fmaxf(0.0f, char_velocity[i] - (10.0f * norm[i] * dt));
		}
		else if (char_velocity[i] < 0.0f) {
			char_velocity[i] = (float)fmin(0.0f, char_velocity[i] - (10.0f * norm[i] * dt));
		}
	}


	// Calculate char's yaw rotation direction
	mat4 dir_rotation = rotate_pitch_yaw(0.0f, char_yaw);

	// calculate acceleration
	vec4 acceleration = { 0.0f };


	if (held_keys[GLFW_KEY_W]) {
		acceleration += dir_rotation * vec4(0.0f, 0.0f, -1.0f, 0.0f);
	}
	if (held_keys[GLFW_KEY_S]) {
		acceleration += dir_rotation * vec4(0.0f, 0.0f, 1.0f, 0.0f);
	}
	if (held_keys[GLFW_KEY_A]) {
		acceleration += dir_rotation * vec4(-1.0f, 0.0f, 0.0f, 0.0f);
	}
	if (held_keys[GLFW_KEY_D]) {
		acceleration += dir_rotation * vec4(1.0f, 0.0f, 0.0f, 0.0f);
	}
	if (held_keys[GLFW_KEY_SPACE]) {
		acceleration += vec4(0.0f, 1.0f, 0.0f, 0.0f);
	}
	if (held_keys[GLFW_KEY_LEFT_SHIFT]) {
		acceleration += dir_rotation * vec4(0.0f, -1.0f, 0.0f, 0.0f);
	}

	// Velocity change via acceleration
	char_velocity += acceleration * dt * 50.0f;
	if (length(char_velocity) > 10.0f) {
		char_velocity = 10.0f * normalize(char_velocity);
	}
	char_velocity[3] = 0.0f; // Just in case

	sprintf(buf, "Accel: (%.3f, %.3f, %.3f) | Velocity: (%.3f, %.3f, %.3f)\n", acceleration[0], acceleration[1], acceleration[2], char_velocity[0], char_velocity[1], char_velocity[2]);
	//OutputDebugString(buf);


	// Calculate our change-in-position
	vec4 position_change = char_velocity * dt;

	// Fix it to avoid collisions, if noclip is not on
	vec4 fixed_position_change = position_change;
	if (!noclip) {
		fixed_position_change = prevent_collisions(position_change);
	}

	// Snap to walls to cancel velocity and to stay at a constant value while moving along wall
	ivec4 ipos = vec2ivec(char_position);

	// if removed east, snap to east wall
	if (position_change[0] > fixed_position_change[0]) {
		char_velocity[0] = 0;
		char_position[0] = fmin(char_position[0], ipos[0] + 1.0f - PLAYER_RADIUS); // RESET EAST
	}
	// west
	if (position_change[0] < fixed_position_change[0]) {
		char_velocity[0] = 0;
		char_position[0] = fmaxf(char_position[0], ipos[0] + PLAYER_RADIUS); // RESET WEST
	}
	// north
	if (position_change[2] < fixed_position_change[2]) {
		char_velocity[2] = 0;
		char_position[2] = fmaxf(char_position[2], ipos[2] + PLAYER_RADIUS); // RESET NORTH
	}
	// south
	if (position_change[2] > fixed_position_change[2]) {
		char_velocity[2] = 0;
		char_position[2] = fmin(char_position[2], ipos[2] + 1.0f - PLAYER_RADIUS); // RESET SOUTH
	}
	// up
	if (position_change[1] > fixed_position_change[1]) {
		char_velocity[1] = 0;
		char_position[1] = fmin(char_position[1], ipos[1] + 2.0f - PLAYER_HEIGHT); // RESET UP
	}
	// down
	if (position_change[1] < fixed_position_change[1]) {
		char_velocity[1] = 0;
		char_position[1] = fmaxf(char_position[1], (float)ipos[1]); // RESET DOWN
	}

	// Update position
	char_position += fixed_position_change;

	ivec4 below = vec2ivec(char_position + DOWN_0);
	auto type = get_type(below);
	auto name = block_name(type);
	sprintf(buf, "Block below: %s\n", name.c_str());
	//OutputDebugString(buf);

	sprintf(buf, "Velocity: (%.2f, %.2f, %.2f)\n", char_velocity[0], char_velocity[1], char_velocity[2]);
	//OutputDebugString(buf);
}

// given a player's change-in-position, modify the change to optimally prevent collisions
vec4 App::prevent_collisions(const vec4 position_change) {
	// Get all blocks we might be intersecting with
	auto blocks = get_intersecting_blocks(char_position + position_change);

	// if all blocks are air, we done
	if (all_of(begin(blocks), end(blocks), [this](const auto &block) { return get_type(block) == Block::Air; })) {
		return position_change;
	}

	// indices of position-change array
	int indices[3] = { 0, 1, 2 };

	// sort indices by position_change value, smallest absolute value to largest absolute value
	sort(begin(indices), end(indices), [position_change](const int i1, const int i2) {
		return abs(position_change[i1]) < abs(position_change[i2]);
	});

	// TODO: Instead of removing 1 or 2 separately, group them together, and remove the ones with smallest length.
	// E.g. if velocity is (2, 2, 10), and have to either remove (2,2) or (10), remove (2,2) because sqrt(2^2+2^2) = sqrt(8) < 10.

	// try removing just one velocity
	for (int i = 0; i < 3; i++) {
		vec4 position_change_fixed = position_change;
		position_change_fixed[indices[i]] = 0.0f;
		blocks = get_intersecting_blocks(char_position + position_change_fixed);

		// if all blocks are air, we done
		if (all_of(begin(blocks), end(blocks), [this](const auto &block) { return get_type(block[0], block[1], block[2]) == Block::Air; })) {
			return position_change_fixed;
		}
	}

	// indices for pairs of velocities
	ivec2 pair_indices[3] = {
		{0, 1},
		{0, 2},
		{1, 2},
	};

	// sort again, this time based on 2d-vector length
	sort(begin(pair_indices), end(pair_indices), [position_change](const auto pair1, const auto pair2) {
		return length(vec2(position_change[pair1[0]], position_change[pair1[1]])) < length(vec2(position_change[pair2[0]], position_change[pair2[1]]));
	});

	// try removing two velocities
	for (int i = 0; i < 3; i++) {
		vec4 position_change_fixed = position_change;
		position_change_fixed[pair_indices[i][0]] = 0.0f;
		position_change_fixed[pair_indices[i][1]] = 0.0f;
		blocks = get_intersecting_blocks(char_position + position_change_fixed);

		// if all blocks are air, we done
		if (all_of(begin(blocks), end(blocks), [this](const auto &block) { return get_type(block[0], block[1], block[2]) == Block::Air; })) {
			return position_change_fixed;
		}
	}

	// after all this we still can't fix it? Frick, just don't move player then.
	OutputDebugString("Holy fuck it's literally unfixable.\n");
	return { 0 };
}

// given a player's position, what blocks does he intersect with?
vector<ivec4> App::get_intersecting_blocks(vec4 player_position) {
	// get x/y/z min/max
	ivec3 xyzMin = { (int)floorf(player_position[0] - PLAYER_RADIUS), (int)floorf(player_position[1]), (int)floorf(player_position[2] - PLAYER_RADIUS) };
	ivec3 xyzMax = { (int)floorf(player_position[0] + PLAYER_RADIUS), (int)floorf(player_position[1] + PLAYER_HEIGHT), (int)floorf(player_position[2] + PLAYER_RADIUS) };

	// TODO: use set for duplicate-removal

	// get all blocks that our player intersects with
	vector<ivec4> blocks;
	for (int x = xyzMin[0]; x <= xyzMax[0]; x++) {
		for (int y = xyzMin[1]; y <= xyzMax[1]; y++) {
			for (int z = xyzMin[2]; z <= xyzMax[2]; z++)
			{
				blocks.push_back({ x, y, z, 0 });
			}
		}
	}

	return blocks;
}

void App::onKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	// ignore unknown keys
	if (key == GLFW_KEY_UNKNOWN) {
		return;
	}

	// handle key presses
	if (action == GLFW_PRESS) {
		held_keys[key] = true;

		// N = toggle noclip
		if (key == GLFW_KEY_N) {
			noclip = !noclip;
		}
	}

	// handle key releases
	if (action == GLFW_RELEASE) {
		held_keys[key] = false;
	}
}

void App::onMouseMove(GLFWwindow* window, double x, double y)
{
	// bonus of using deltas for yaw/pitch:
	// - can cap easily -- if we cap without deltas, and we move 3000x past the cap, we'll have to move 3000x back before mouse moves!
	// - easy to do mouse sensitivity
	double delta_x = x - last_mouse_x;
	double delta_y = y - last_mouse_y;

	// update pitch/yaw
	char_yaw += (float)(info.mouseX_Sensitivity * delta_x);
	char_pitch += (float)(info.mouseY_Sensitivity * delta_y);

	// cap pitch
	char_pitch = clamp(char_pitch, -90.0f, 90.0f);

	// update old values
	last_mouse_x = x;
	last_mouse_y = y;
}

void App::onResize(GLFWwindow* window, int width, int height) {
	info.width = width;
	info.height = height;

	int px_width, px_height;
	glfwGetFramebufferSize(window, &px_width, &px_height);
	glViewport(0, 0, px_width, px_height);
}

void App::onDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message) {
	char buf[4096];
	char *bufp = buf;

	// ignore non-significant error/warning codes (e.g. 131185 = "buffer created")
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

	bufp += sprintf(bufp, "---------------\n");
	bufp += sprintf(bufp, "OpenGL debug message (%d): %s\n", id, message);

	switch (source)
	{
	case GL_DEBUG_SOURCE_API:             bufp += sprintf(bufp, "Source: API"); break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   bufp += sprintf(bufp, "Source: Window System"); break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER: bufp += sprintf(bufp, "Source: Shader Compiler"); break;
	case GL_DEBUG_SOURCE_THIRD_PARTY:     bufp += sprintf(bufp, "Source: Third Party"); break;
	case GL_DEBUG_SOURCE_APPLICATION:     bufp += sprintf(bufp, "Source: Application"); break;
	case GL_DEBUG_SOURCE_OTHER:           bufp += sprintf(bufp, "Source: Other"); break;
	} bufp += sprintf(bufp, "\n");

	switch (type)
	{
	case GL_DEBUG_TYPE_ERROR:               bufp += sprintf(bufp, "Type: Error"); break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: bufp += sprintf(bufp, "Type: Deprecated Behaviour"); break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  bufp += sprintf(bufp, "Type: Undefined Behaviour"); break;
	case GL_DEBUG_TYPE_PORTABILITY:         bufp += sprintf(bufp, "Type: Portability"); break;
	case GL_DEBUG_TYPE_PERFORMANCE:         bufp += sprintf(bufp, "Type: Performance"); break;
	case GL_DEBUG_TYPE_MARKER:              bufp += sprintf(bufp, "Type: Marker"); break;
	case GL_DEBUG_TYPE_PUSH_GROUP:          bufp += sprintf(bufp, "Type: Push Group"); break;
	case GL_DEBUG_TYPE_POP_GROUP:           bufp += sprintf(bufp, "Type: Pop Group"); break;
	case GL_DEBUG_TYPE_OTHER:               bufp += sprintf(bufp, "Type: Other"); break;
	} bufp += sprintf(bufp, "\n");

	switch (severity)
	{
	case GL_DEBUG_SEVERITY_HIGH:         bufp += sprintf(bufp, "Severity: high"); break;
	case GL_DEBUG_SEVERITY_MEDIUM:       bufp += sprintf(bufp, "Severity: medium"); break;
	case GL_DEBUG_SEVERITY_LOW:          bufp += sprintf(bufp, "Severity: low"); break;
	case GL_DEBUG_SEVERITY_NOTIFICATION: bufp += sprintf(bufp, "Severity: notification"); break;
	} bufp += sprintf(bufp, "\n");
	bufp += sprintf(bufp, "\n");

	OutputDebugString(buf);
	throw buf; // for now
}

namespace {
	/* GLFW/GL callback functions */

	void glfw_onError(int error, const char* description) {
		MessageBox(NULL, description, "GLFW error", MB_OK);
	}

	void glfw_onKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
		App::app->onKey(window, key, scancode, action, mods);
	}

	void glfw_onMouseMove(GLFWwindow* window, double x, double y) {
		App::app->onMouseMove(window, x, y);
	}

	void glfw_onResize(GLFWwindow* window, int width, int height) {
		App::app->onResize(window, width, height);
	}

	void APIENTRY gl_onDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, GLvoid* userParam) {
		App::app->onDebugMessage(source, type, id, severity, length, message);
	}
}
