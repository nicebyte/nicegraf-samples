/**
Copyright (c) 2018 nicegraf contributors
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "common.h"
#include <nicegraf.h>
#include <nicegraf_wrappers.h>
#include <imgui.h>
#include <assert.h>
#include <stdint.h>

struct app_state {
  ngf::render_target default_rt;
  ngf::cmd_buffer cmd_buf;
};

// Called upon application initialization.
init_result on_initialized(uintptr_t native_handle,
                           uint32_t initial_width,
                           uint32_t initial_height) {
  app_state *state = new app_state;

  // Create a nicegraf context.
  ngf_clear clear;
  clear.clear_color[0] = 0.6f;
  clear.clear_color[1] = 0.7f;
  clear.clear_color[2] = 0.8f;
  clear.clear_color[3] = 1.0f;
  ngf_swapchain_info swapchain_info = {
    NGF_IMAGE_FORMAT_BGRA8, // color format
    NGF_IMAGE_FORMAT_UNDEFINED, // depth format (none)
    0, // number of MSAA samples (0, non-multisampled)
    2u, // swapchain capacity hint
    initial_width, // swapchain image width
    initial_height, // swapchain image height
    native_handle,
    NGF_PRESENTATION_MODE_IMMEDIATE, // turn off vsync
  };
  ngf_context_info ctx_info = {
    &swapchain_info, // swapchain_info
    nullptr, // shared_context (nullptr, no shared context)
  };
  ngf::context nicegraf_context;
  ngf_error err = nicegraf_context.initialize(ctx_info);
  assert(err == NGF_ERROR_OK);

  // Set the newly created context as current.
  err = ngf_set_context(nicegraf_context);
  assert(err == NGF_ERROR_OK);

  // Obtain the default render target.
  ngf_render_target rt;
  ngf_default_render_target(NGF_LOAD_OP_CLEAR,
                            NGF_LOAD_OP_DONTCARE,
                            NGF_STORE_OP_STORE,
                            NGF_STORE_OP_DONTCARE,
                            &clear,
                            NULL,
                            &rt);
  state->default_rt = ngf::render_target(rt);

  // Create a command buffer.
  ngf_cmd_buffer_info cmd_buf_info {0u};
  err = state->cmd_buf.initialize(cmd_buf_info);
  assert(err == NGF_ERROR_OK);

  return { std::move(nicegraf_context), state};
}

// Called every frame.
void on_frame(uint32_t, uint32_t, float, void *userdata) {
  app_state *state = (app_state*)userdata;
  ngf::cmd_buffer &cmd_buf = state->cmd_buf;
  ngf_start_cmd_buffer(cmd_buf);
  ngf_render_encoder enc;
  ngf_cmd_buffer_start_render(cmd_buf, &enc);
  ngf_cmd_begin_pass(enc, state->default_rt);
  ngf_cmd_end_pass(enc);
  ngf_cmd_buffer b = cmd_buf.get();
  ngf_submit_cmd_buffers(1u, &b);
}

// Called every time the application has to draw an ImGUI overlay.
void on_ui(void*) {
  ImGui::ShowDemoWindow();
}

// Called when the app is about to close.
void on_shutdown(void *userdata) {
  delete (app_state*)userdata;
}
