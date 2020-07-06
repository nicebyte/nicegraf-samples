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
#include <nicegraf_util.h>
#include <nicegraf_wrappers.h>
#include <imgui.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct app_state {
  ngf::render_target default_rt;
  ngf::shader_stage blit_vert_stage;
  ngf::shader_stage frag_stage;
  ngf::graphics_pipeline pipelines[2];
};

// Called upon application initialization.
init_result on_initialized(uintptr_t native_handle,
                           uint32_t initial_width,
                           uint32_t initial_height) {
  app_state *state = new app_state;

  ngf::context ctx = create_default_context(native_handle,
                                            initial_width, initial_height);

  // Set up a render pass.
  ngf_clear clear;
  clear.clear_color[0] = 0.0f;
  clear.clear_color[1] = 0.0f;
  clear.clear_color[2] = 0.0f;
  clear.clear_color[3] = 0.0f;
  
  // Obtain the default render target.
  ngf_render_target rt;
  ngf_error err = ngf_default_render_target(
                    NGF_LOAD_OP_CLEAR, NGF_LOAD_OP_DONTCARE,
                    NGF_STORE_OP_STORE, NGF_STORE_OP_DONTCARE,
                    &clear, NULL, &rt);
  assert(err == NGF_ERROR_OK);
  state->default_rt = ngf::render_target(rt);

  // Load shader stages.
  state->blit_vert_stage =
      load_shader_stage("fullscreen-triangle", "VSMain", NGF_STAGE_VERTEX);
  state->frag_stage =
      load_shader_stage("spec-consts", "PSMain", NGF_STAGE_FRAGMENT);

  // Initial pipeline configuration with OpenGL-style defaults.
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(nullptr,
                                                 &pipeline_data);
  ngf_graphics_pipeline_info &pipe_info = pipeline_data.pipeline_info;
  
  // Construct the specialization entries.
  ngf_specialization_info spec_info;
  spec_info.nspecializations = 2u;
  ngf_constant_specialization specs[] = {
    {0u /*constant_id*/, 0u /*offset*/, NGF_TYPE_FLOAT /*type*/},
    {1u /*constant_id*/, sizeof(float) /*offset*/, NGF_TYPE_FLOAT /*type*/},
  };
  float spec_data[2] = {1.0f, 1.0f};
  spec_info.specializations = specs;
  spec_info.value_buffer = spec_data;
  pipe_info.spec_info = &spec_info;
  pipe_info.compatible_render_target = state->default_rt.get();
  
  // Configure the first pipeline.
  pipe_info.nshader_stages = 2u;
  pipe_info.shader_stages[0] = state->blit_vert_stage.get();
  pipe_info.shader_stages[1] = state->frag_stage.get();
  err = state->pipelines[0].initialize(pipe_info);
  assert(err == NGF_ERROR_OK);

  // Configure the second pipeline.
  spec_data[0] = 0.5f;
  spec_data[1] = 0.7f;
  err = state->pipelines[1].initialize(pipe_info);
  assert(err == NGF_ERROR_OK);

  return { std::move(ctx), state};
}

// Called every frame.
void on_frame(uint32_t w, uint32_t h, float, void *userdata) {
  static uint32_t frame = 1u;
  static uint32_t pipe = 0u;
  app_state *state = (app_state*)userdata;
  ngf_irect2d viewport { 0, 0, w, h };
  ngf_cmd_buffer cmd_buf = nullptr;
  ngf_cmd_buffer_info cmd_info;
  ngf_create_cmd_buffer(&cmd_info, &cmd_buf);
  ngf_start_cmd_buffer(cmd_buf);
  ngf::render_encoder enc { cmd_buf };
  ngf_cmd_begin_pass(enc, state->default_rt);
  ngf_cmd_bind_gfx_pipeline(enc, state->pipelines[pipe]);
  // Switch between pipelines every 120 frames.
  frame = (frame + 1u) % 120u;
  if (frame == 0u) pipe = (pipe + 1u) % 2u;
  ngf_cmd_viewport(enc, &viewport);
  ngf_cmd_scissor(enc, &viewport);
  ngf_cmd_draw(enc, false, 0u, 3u, 1u); 
  ngf_cmd_end_pass(enc);
  ngf_submit_cmd_buffers(1u, &cmd_buf);
  ngf_destroy_cmd_buffer(cmd_buf);
}

// Called every time the application has to dra an ImGUI overlay.
void on_ui(void*) {
}

// Called when the app is about to close.
void on_shutdown(void *userdata) {
  delete (app_state*)userdata;
}
