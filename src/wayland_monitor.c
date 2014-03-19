//========================================================================
// GLFW 3.1 Wayland - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2014 Jonas Ådahl <jadahl@gmail.com>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


struct _GLFWvidmodeWayland {
    GLFWvidmode         base;
    uint32_t            flags;
};

static void geometry(void* data,
                     struct wl_output* output,
                     int32_t x,
                     int32_t y,
                     int32_t physicalWidth,
                     int32_t physicalHeight,
                     int32_t subpixel,
                     const char* make,
                     const char* model,
                     int32_t transform)
{
    struct _GLFWmonitor *monitor = data;

    monitor->wayland.x = x;
    monitor->wayland.y = y;
    monitor->widthMM = physicalWidth;
    monitor->heightMM = physicalHeight;
}

static void mode(void* data,
                 struct wl_output* output,
                 uint32_t flags,
                 int32_t width,
                 int32_t height,
                 int32_t refresh)
{
    struct _GLFWmonitor *monitor = data;
    _GLFWvidmodeWayland mode = { { 0 }, };

    mode.base.width = width;
    mode.base.height = height;
    mode.base.refreshRate = refresh;
    mode.flags = flags;

    if (monitor->wayland.modesCount + 1 >= monitor->wayland.modesSize)
    {
        int size = monitor->wayland.modesSize * 2;
        _GLFWvidmodeWayland* modes =
            realloc(monitor->wayland.modes,
                    monitor->wayland.modesSize * sizeof(_GLFWvidmodeWayland));
        if (!modes)
        {
            return;
        }
        monitor->wayland.modes = modes;
        monitor->wayland.modesSize = size;
    }

    monitor->wayland.modes[monitor->wayland.modesCount++] = mode;
}

static void done(void* data,
                 struct wl_output* output)
{
    struct _GLFWmonitor *monitor = data;

    monitor->wayland.done = GL_TRUE;
}

static void scale(void* data,
                  struct wl_output* output,
                  int32_t factor)
{
}

static const struct wl_output_listener output_listener = {
    geometry,
    mode,
    done,
    scale,
};


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwAddOutput(uint32_t name, uint32_t version)
{
    _GLFWmonitor *monitor;
    struct wl_output *output;
    char name_str[80];

    memset(name_str, 0, 80 * sizeof(char));
    snprintf(name_str, 79, "wl_output@%u", name);

    if (version < 2)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "Unsupported wl_output interface version");
        return;
    }

    monitor = _glfwAllocMonitor(name_str, 0, 0);

    output = wl_registry_bind(_glfw.wayland.registry,
                              name,
                              &wl_output_interface,
                              2);
    if (!output)
    {
        _glfwFreeMonitor(monitor);
        return;
    }

    monitor->wayland.modes = calloc(4, sizeof(_GLFWvidmodeWayland));
    monitor->wayland.modesSize = 4;

    monitor->wayland.output = output;
    wl_output_add_listener(output, &output_listener, monitor);

    if (_glfw.wayland.monitorsCount + 1 >= _glfw.wayland.monitorsSize)
    {
        _GLFWmonitor** monitors = _glfw.wayland.monitors;
        int size = _glfw.wayland.monitorsSize * 2;

        monitors = realloc(monitors, size * sizeof(_GLFWmonitor*));
        if (!monitors)
        {
            wl_output_destroy(output);
            _glfwFreeMonitor(monitor);
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "Failed to retrieve monitor information");
            return;
        }

        _glfw.wayland.monitors = monitors;
        _glfw.wayland.monitorsSize = size;
    }

    _glfw.wayland.monitors[_glfw.wayland.monitorsCount++] = monitor;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

_GLFWmonitor** _glfwPlatformGetMonitors(int* count)
{
    _GLFWmonitor** monitors;
    _GLFWmonitor* monitor;
    int monitorsCount = _glfw.wayland.monitorsCount;
    int i;

    if (_glfw.wayland.monitorsCount == 0)
        goto err;

    monitors = calloc(monitorsCount, sizeof(_GLFWmonitor*));
    if (!monitors)
        goto err;

    for (i = 0; i < monitorsCount; i++)
    {
	_GLFWmonitor* origMonitor = _glfw.wayland.monitors[i];
        monitor = malloc(sizeof(_GLFWmonitor));
        if (!monitor)
        {
            if (i > 0)
            {
                *count = i;
                return monitors;
            }
            else
            {
                goto err_free;
            }
        }

	monitor->modes =
	    _glfwPlatformGetVideoModes(origMonitor,
				       &origMonitor->wayland.modesCount);
	*monitor = *_glfw.wayland.monitors[i];
        monitors[i] = monitor;
    }

    *count = monitorsCount;
    return monitors;

err_free:
    free(monitors);

err:
    *count = 0;
    return NULL;
}

GLboolean _glfwPlatformIsSameMonitor(_GLFWmonitor* first, _GLFWmonitor* second)
{
    return first->wayland.output == second->wayland.output;
}

void _glfwPlatformGetMonitorPos(_GLFWmonitor* monitor, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = monitor->wayland.x;
    if (ypos)
        *ypos = monitor->wayland.y;
}

GLFWvidmode* _glfwPlatformGetVideoModes(_GLFWmonitor* monitor, int* found)
{
    GLFWvidmode *modes;
    int modesCount = monitor->wayland.modesCount;
    int i;

    modes = calloc(modesCount, sizeof(GLFWvidmode));
    if (!modes)
    {
            *found = 0;;
            return NULL;
    }

    for (i = 0; i < modesCount; i++)
        modes[i] = monitor->wayland.modes[i].base;

    *found = modesCount;
    return modes;
}

void _glfwPlatformGetVideoMode(_GLFWmonitor* monitor, GLFWvidmode* mode)
{
    int i;

    for (i = 0; i < monitor->wayland.modesCount; i++)
    {
        if (monitor->wayland.modes[i].flags & WL_OUTPUT_MODE_CURRENT)
        {
            *mode = monitor->wayland.modes[i].base;
            return;
        }
    }
}