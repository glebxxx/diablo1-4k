/**
 * @file gl1_gl.h
 *
 * Includes the system OpenGL 1.x header.
 */
#pragma once

#ifdef __APPLE__
// OpenGL is deprecated on macOS, but the legacy 2.1 context is still available.
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
