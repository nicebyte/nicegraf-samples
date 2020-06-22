/**
 * Copyright (c) 2019 nicegraf contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define _CRT_SECURE_NO_WARNINGS
#include <GLFW/glfw3.h>
#if defined(_WIN32) || defined(_WIN64)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GET_GLFW_NATIVE_HANDLE(w) glfwGetWin32Window(w)
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include "get_glfw_contentview.h"
#define GET_GLFW_NATIVE_HANDLE(w) get_glfw_contentview(w)
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GET_GLFW_NATIVE_HANDLE(w) glfwGetX11Window(w)
#endif
#include <GLFW/glfw3native.h>
#include <assert.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <fstream>

#include "common.h"
#include "imgui_ngf_backend.h"
#include <examples/imgui_impl_glfw.h>

void debugmsg_cb(const char *msg, const void*) {
  // TODO: surface these logs in a GUI console.
  printf("%s\n", msg);
}

init_result on_initialized(uintptr_t handle, uint32_t w, uint32_t h);
void on_frame(uint32_t w, uint32_t h, float time, void *userdata);
void on_ui(void *userdata);
void on_shutdown(void *userdata);

#if defined(__APPLE__)
#define ENTRYFN apple_main
#else
#define ENTRYFN main
#endif

// This is the "common main" for desktop apps.
int ENTRYFN(int, char **) {
  // Initialize GLFW.
  glfwInit();
 
  // Initialize nicegraf.
  const ngf_init_info init_info = {
     NGF_DEVICE_PREFERENCE_DONTCARE,
    {
    #ifndef NDEBUG
      NGF_DIAGNOSTICS_VERBOSITY_DETAILED,
    #else
      NGF_DIAGNOSTICS_VERBOSITY_DEFAULT,
    #endif
      nullptr,
      nullptr
    }
  };
  ngf_error err = ngf_initialize(&init_info);
  if (err != NGF_ERROR_OK) {
    exit(1);
  }

  // Tell GLFW not to attempt to create an API context for the
  // window we're about to create (nicegraf does it for us).
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  // Create a GLFW window.
  GLFWwindow *win = glfwCreateWindow(1024,
                                     768,
                                     "nicegraf sample",
                                     nullptr,
                                     nullptr);
  assert(win != nullptr);

  // Notify the app.
  int w, h;
  glfwGetFramebufferSize(win, &w, &h);
  {
  init_result init_data = on_initialized((uintptr_t)GET_GLFW_NATIVE_HANDLE(win),
                                         (uint32_t)w,
                                         (uint32_t)h);

  // Create an ImGui context and initialize ImGui GLFW i/o backend and nicegraf
  // rendering backend for imgui.
  ImGui::SetCurrentContext(ImGui::CreateContext());
  ImGui_ImplGlfw_InitForOpenGL(win, true);
  ngf_imgui ui; // ImGui nicegraf rendering backend.

  // Style ImGui controls.
  ImGui::StyleColorsLight();
  ImGuiStyle &gui_style = ImGui::GetStyle();
  gui_style.WindowRounding = 0.0f;
  gui_style.ScrollbarRounding = 0.0f;
  gui_style.FrameBorderSize = 1.0f;
  gui_style.ScrollbarSize = 20.0f;
  gui_style.WindowTitleAlign.x = 0.5f;

  // Create a command buffer the UI rendering commands.
  ngf::cmd_buffer uibuf;
  ngf_cmd_buffer_info uibuf_info {0u};
  uibuf.initialize(uibuf_info);

  // Obtain the default render target.
  ngf_render_target defaultrt = nullptr;
  ngf_default_render_target(NGF_LOAD_OP_DONTCARE,
                            NGF_LOAD_OP_DONTCARE,
                            NGF_STORE_OP_STORE,
                            NGF_STORE_OP_DONTCARE,
                            NULL,
                            NULL,
                            &defaultrt);
  int old_win_width = 0, old_win_height = 0;
  bool imgui_font_uploaded = false;
  while (!glfwWindowShouldClose(win)) { // Main loop.
    glfwPollEvents(); // Get input events.
    
    // Update renderable area size.
    int new_win_width = 0, new_win_height = 0;
    glfwGetFramebufferSize(win, &new_win_width, &new_win_height);
    if (new_win_width != old_win_width || new_win_height != old_win_height) {
      old_win_width = new_win_width; old_win_height = new_win_height;
      ngf_resize_context(init_data.context,
                         (uint32_t)new_win_width,
                         (uint32_t)new_win_height);
    }
    
    if (ngf_begin_frame() == NGF_ERROR_OK) {
      // Notify application.
      on_frame((uint32_t)old_win_width, (uint32_t)old_win_height,
                (float)glfwGetTime(),
                init_data.userdata);
#if !defined(NGF_NO_IMGUI)
      // Give application a chance to submit its UI drawing commands.
      // TODO: make toggleable.
      ImGui::GetIO().DisplaySize.x = (float)new_win_width;
      ImGui::GetIO().DisplaySize.y = (float)new_win_height;
      ImGui::NewFrame();
      ImGui_ImplGlfw_NewFrame();
      on_ui(init_data.userdata);
      // TODO: draw debug console window.

      // Draw the UI.
      ngf_start_cmd_buffer(uibuf);
      if (!imgui_font_uploaded) {
        ui.upload_font_texture(uibuf);
        imgui_font_uploaded = true;
      }
      ngf::render_encoder enc { uibuf };
      ngf_cmd_begin_pass(enc, defaultrt);
      ui.record_rendering_commands(enc);
      ngf_cmd_end_pass(enc);
      ngf_cmd_buffer b = uibuf.get();
      ngf_submit_cmd_buffers(1u, &b);
#endif
      // End frame.
      ngf_end_frame();
    }
  }
  ngf_destroy_render_target(defaultrt);
  on_shutdown(init_data.userdata);
  }
  glfwTerminate();
  return 0;
}

#if defined(NGF_BACKEND_OPENGL)
#define SHADER_EXTENSION ".430.glsl"
#elif defined(NGF_BACKEND_VULKAN)
#define SHADER_EXTENSION ".spv"
#else
#define SHADER_EXTENSION ".12.msl"
#endif

ngf::shader_stage load_shader_stage(const char *root_name,
                                    const char *entry_point_name,
                                    ngf_stage_type type,
                                    const char *prefix) {
  static const char *stage_names[] = {
    "vs", "ps"
  };
  std::string file_name =
       prefix + std::string(root_name) + "." + stage_names[type] +
       SHADER_EXTENSION;
  std::ifstream fs(file_name, std::ios::binary | std::ios::in);
  assert(fs.is_open());
  std::vector<char> content((std::istreambuf_iterator<char>(fs)),
                       std::istreambuf_iterator<char>());
  ngf_shader_stage_info stage_info;
  stage_info.type = type;
  stage_info.content = content.data();
  stage_info.content_length = (uint32_t)content.size();
  stage_info.debug_name = "";
  stage_info.entry_point_name = entry_point_name;
  ngf::shader_stage stage;
  ngf_error err = stage.initialize(stage_info);
  assert(err == NGF_ERROR_OK); err = NGF_ERROR_OK;
  return stage;
}

ngf_plmd* load_pipeline_metadata(const char *name, const char *prefix) {
  std::string file_name = prefix + std::string(name) + ".pipeline";
  std::vector<char> content = load_raw_data(file_name.c_str());
  ngf_plmd *m;
  ngf_plmd_error err = ngf_plmd_load(content.data(), content.size(), NULL, &m);
  assert(err == NGF_PLMD_ERROR_OK); err = NGF_PLMD_ERROR_OK;
  return m;
}

ngf::context create_default_context(uintptr_t handle, uint32_t w, uint32_t h) {
  // Create a nicegraf context.
  ngf_swapchain_info swapchain_info = {
    NGF_IMAGE_FORMAT_BGRA8, // color format
    NGF_IMAGE_FORMAT_DEPTH24_STENCIL8, // depth format (24bit)
    8u, // MSAA 8x
    2u, // swapchain capacity hint
    w, // swapchain image width
    h, // swapchain image height
    handle,
    NGF_PRESENTATION_MODE_FIFO,
  };
  ngf_context_info ctx_info = {
    &swapchain_info, // swapchain_info
    nullptr // shared_context (nullptr, no shared context)
  };
  ngf::context nicegraf_context;
  ngf_error err = nicegraf_context.initialize(ctx_info);
  assert(err == NGF_ERROR_OK);

  // Set the newly created context as current.
  err = ngf_set_context(nicegraf_context);
  assert(err == NGF_ERROR_OK);

#if !defined(NDEBUG)
  // Install debug message callback in debug mode only.
  // ngf_debug_message_callback(nullptr, debugmsg_cb);
#endif

  return nicegraf_context;
}


std::vector<char> load_raw_data(const char *file_path) {
  std::ifstream fs(file_path, std::ios::binary);
  std::vector<char> content((std::istreambuf_iterator<char>(fs)),
                             std::istreambuf_iterator<char>());
  return content;
}
