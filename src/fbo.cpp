#include "fbo.h"

#include "util.h"

#include "GL/gl3w.h"

#include <sstream>

// can only run after glfw (and maybe opengl) have been initialized
GLenum get_default_framebuffer_depth_attachment_type() {
	GLint result;
	glGetNamedFramebufferAttachmentParameteriv(0, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &result);

	switch (result) {
	case 16:
		return GL_DEPTH_COMPONENT16;
	case 24:
		return GL_DEPTH_COMPONENT24;
	case 32:
		return GL_DEPTH_COMPONENT32;
	default:
		std::ostringstream buf;
		buf << "Unable to retrieve default framebuffer attachment size. (Got " << result << ".)";
		WindowsException(buf.str().c_str());
		exit(-1);
	}

	return result;
}

void assert_fbo_not_incomplete(GLuint fbo) {
	GLenum status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		OutputDebugString("\nFBO completeness error: ");

		switch (status) {
		case GL_FRAMEBUFFER_UNDEFINED:
			OutputDebugString("GL_FRAMEBUFFER_UNDEFINED\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
			OutputDebugString("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
			OutputDebugString("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
			OutputDebugString("GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
			OutputDebugString("GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER\n");
			break;
		case GL_FRAMEBUFFER_UNSUPPORTED:
			OutputDebugString("GL_FRAMEBUFFER_UNSUPPORTED\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
			OutputDebugString("GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
			OutputDebugString("GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS\n");
			break;
		default:
			OutputDebugString("UNKNOWN ERROR\n");
			break;
		}

		OutputDebugString("\n");
		exit(-1);
	}
}

FBO::FBO() : width(0), height(0) {}

FBO::FBO(const GLsizei width, const GLsizei height) : width(width), height(height)
{
	// Create FBO
	glCreateFramebuffers(1, &fbo);

	// Fill it in
	update_fbo();

	// Tell FBO to draw into its one color buffer
	glNamedFramebufferDrawBuffer(fbo, GL_COLOR_ATTACHMENT0);

#ifdef _DEBUG
	// Make sure it's complete
	assert_fbo_not_incomplete(fbo);
#endif // _DEBUG
}

FBO::~FBO()
{
	// delete textures
	glDeleteTextures(1, &color_buf);
	color_buf = 0;

	glDeleteTextures(1, &depth_buf);
	depth_buf = 0;

	// delete fbo
	glDeleteFramebuffers(1, &fbo);
	fbo = 0;
}

FBO& FBO::operator=(FBO&& rhs) noexcept {
	// swap everything so that we own their textures/fbos and they clean up our textures/fbos
	std::swap(width, rhs.width);
	std::swap(height, rhs.height);

	std::swap(color_buf, rhs.color_buf);
	std::swap(depth_buf, rhs.depth_buf);
	std::swap(fbo, rhs.fbo);

	return *this;
}

GLsizei FBO::get_width() const {
	return width;
}

GLsizei FBO::get_height() const {
	return height;
}

void FBO::set_dimensions(const GLsizei width, const GLsizei height) {
	this->width = width;
	this->height = height;
}

GLuint FBO::get_color_buf() const {
	return color_buf;
}

void FBO::set_color_buf(GLuint color_buf) {
	this->color_buf = color_buf;
}

GLuint FBO::get_depth_buf() const {
	return depth_buf;
}

void FBO::set_depth_buf(GLuint depth_buf) {
	this->depth_buf = depth_buf;
}

GLuint FBO::get_fbo() const {
	return fbo;
}

// update our OpenGL FBO to match this object
void FBO::update_fbo() {
	// delete textures
	glDeleteTextures(1, &color_buf);
	glDeleteTextures(1, &depth_buf);

	// Create color texture, allocate
	glCreateTextures(GL_TEXTURE_2D, 1, &color_buf);
	glTextureStorage2D(color_buf, 1, GL_RGBA32F, width, height);
	glTextureParameteri(color_buf, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // TODO: if stuff breaks, switch to GL_NEAREST
	glTextureParameteri(color_buf, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // TODO: if stuff breaks, switch to GL_NEAREST
	glTextureParameteri(color_buf, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(color_buf, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Create depth texture, allocate
	glCreateTextures(GL_TEXTURE_2D, 1, &depth_buf);
	glTextureStorage2D(depth_buf, 1, get_default_framebuffer_depth_attachment_type(), width, height);
	glTextureParameteri(depth_buf, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTextureParameteri(depth_buf, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTextureParameteri(depth_buf, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(depth_buf, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Bind color / depth textures to FBO
	glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color_buf, 0);
	glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, depth_buf, 0);
}

