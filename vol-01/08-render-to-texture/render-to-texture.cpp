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
#define _CRT_SECURE_NO_WARNINGS
#include "common.h"
#include <nicegraf.h>
#include <nicegraf_util.h>
#include <nicegraf_wrappers.h>
#include <imgui.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct app_state {
  ngf::render_target default_rt;
  ngf::render_target offscreen_rt;
  ngf::shader_stage blit_vert_stage;
  ngf::shader_stage blit_frag_stage;
  ngf::shader_stage offscreen_vert_stage;
  ngf::shader_stage offscreen_frag_stage;
  ngf::graphics_pipeline blit_pipeline;
  ngf::graphics_pipeline offscreen_pipeline;
  ngf::image rt_texture;
  ngf::sampler sampler;
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
  clear.clear_color[0] = 0.6f;
  clear.clear_color[1] = 0.7f;
  clear.clear_color[2] = 0.8f;
  clear.clear_color[3] = 1.0f;
  
  // Create the image.
  const ngf_extent3d img_size { 512u, 512u, 1u };
  const ngf_image_info img_info {
    NGF_IMAGE_TYPE_IMAGE_2D,
    img_size,
    1u,
    NGF_IMAGE_FORMAT_BGRA8,
    0u,
    NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_ATTACHMENT
  };
  ngf_error err = state->rt_texture.initialize(img_info);
  assert(err == NGF_ERROR_OK);
  
  // Obtain the default render target.
  ngf_render_target rt;
  err = ngf_default_render_target(NGF_LOAD_OP_CLEAR, NGF_LOAD_OP_DONTCARE,
                                  NGF_STORE_OP_STORE, NGF_STORE_OP_DONTCARE,
                                  &clear, NULL, &rt);
  assert(err == NGF_ERROR_OK);
  state->default_rt = ngf::render_target(rt);
  
  // Create the offscreen render target.
  ngf_attachment  offscreen_color_attachment{
    {state->rt_texture.get(), 0u, 0u, NGF_CUBEMAP_FACE_POSITIVE_X},
    NGF_ATTACHMENT_COLOR,
    NGF_LOAD_OP_CLEAR,
    NGF_STORE_OP_STORE,
    {{0.0f}}
  };
  ngf_render_target_info rt_info {
    &offscreen_color_attachment,
    1u
  };
  err = state->offscreen_rt.initialize(rt_info);
  assert(err == NGF_ERROR_OK);

  // Load shader stages and pipeline metadata.
  state->blit_vert_stage =
      load_shader_stage("fullscreen-triangle", "VSMain", NGF_STAGE_VERTEX);
  state->blit_frag_stage =
      load_shader_stage("simple-texture", "PSMain", NGF_STAGE_FRAGMENT);
  state->offscreen_vert_stage =
      load_shader_stage("small-triangle", "VSMain", NGF_STAGE_VERTEX);
  state->offscreen_frag_stage =
      load_shader_stage("small-triangle", "PSMain", NGF_STAGE_FRAGMENT);
  ngf_plmd *pipeline_metadata = load_pipeline_metadata("simple-texture");

  // Create pipeline for blit pass.
  ngf_util_graphics_pipeline_data blit_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(nullptr,
                                                 &blit_pipeline_data);
  ngf_graphics_pipeline_info &blit_pipe_info =
      blit_pipeline_data.pipeline_info;
  blit_pipe_info.nshader_stages = 2u;
  blit_pipe_info.shader_stages[0] = state->blit_vert_stage.get();
  blit_pipe_info.shader_stages[1] = state->blit_frag_stage.get();
  blit_pipe_info.compatible_render_target = state->default_rt.get();
  blit_pipe_info.image_to_combined_map =
      ngf_plmd_get_image_to_cis_map(pipeline_metadata);
  blit_pipe_info.sampler_to_combined_map =
      ngf_plmd_get_sampler_to_cis_map(pipeline_metadata);

  // Create a simple pipeline layout.
  err = ngf_util_create_pipeline_layout_from_metadata(
    ngf_plmd_get_layout(pipeline_metadata), &blit_pipeline_data.layout_info);
  assert(err == NGF_ERROR_OK);
  err = state->blit_pipeline.initialize(blit_pipe_info);
  assert(err == NGF_ERROR_OK);

  // Create pipeline for offscreen pass.
  ngf_util_graphics_pipeline_data offscreen_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(nullptr,
                                                 &offscreen_pipeline_data);
  ngf_graphics_pipeline_info &offscreen_pipe_info =
      blit_pipeline_data.pipeline_info;
  offscreen_pipe_info.nshader_stages = 2u;
  offscreen_pipe_info.shader_stages[0] = state->offscreen_vert_stage.get();
  offscreen_pipe_info.shader_stages[1] = state->offscreen_frag_stage.get();
  offscreen_pipe_info.compatible_render_target = state->offscreen_rt.get();
  err = state->offscreen_pipeline.initialize(offscreen_pipe_info);
  assert(err == NGF_ERROR_OK);

  // Create sampler.
  ngf_sampler_info samp_info {
    NGF_FILTER_LINEAR,
    NGF_FILTER_LINEAR,
    NGF_FILTER_NEAREST,
    NGF_WRAP_MODE_CLAMP_TO_EDGE,
    NGF_WRAP_MODE_CLAMP_TO_EDGE,
    NGF_WRAP_MODE_CLAMP_TO_EDGE,
    0.0f,
    0.0f,
    0.0f,
    {0.0f},
    1.0f,
    false
  };
  err = state->sampler.initialize(samp_info);
  assert(err == NGF_ERROR_OK);

  return { std::move(ctx), state};
}

// Called every frame.
void on_frame(uint32_t w, uint32_t h, float, void *userdata) {
  app_state *state = (app_state*)userdata;
  ngf_irect2d offsc_viewport { 0, 0, 512, 512 };
  ngf_irect2d onsc_viewport {0, 0, w, h };
  ngf_cmd_buffer cmd_buf = nullptr;
  ngf_cmd_buffer_info cmd_info;
  ngf_create_cmd_buffer(&cmd_info, &cmd_buf);
  ngf_start_cmd_buffer(cmd_buf);
  {
  ngf::render_encoder renc { cmd_buf };
  ngf_cmd_begin_pass(renc, state->offscreen_rt);
  ngf_cmd_bind_gfx_pipeline(renc, state->offscreen_pipeline);
  ngf_cmd_viewport(renc, &offsc_viewport);
  ngf_cmd_scissor(renc, &offsc_viewport);
  ngf_cmd_draw(renc, false, 0u, 3u, 1u);
  ngf_cmd_end_pass(renc);
  ngf_cmd_begin_pass(renc, state->default_rt);
  ngf_cmd_bind_gfx_pipeline(renc, state->blit_pipeline);
  ngf_cmd_viewport(renc, &onsc_viewport);
  ngf_cmd_scissor(renc, &onsc_viewport);
  ngf::cmd_bind_resources(renc,
    ngf::descriptor_set<0>::binding<1>::texture(state->rt_texture.get()),
    ngf::descriptor_set<0>::binding<2>::sampler(state->sampler.get()));
  ngf_cmd_draw(renc, false, 0u, 3u, 1u);
  ngf_cmd_end_pass(renc);
  ngf_render_encoder_end(renc);
  }
  ngf_submit_cmd_buffers(1u, &cmd_buf);
  ngf_destroy_cmd_buffer(cmd_buf);
}

// Called every time the application has to dra an ImGUI overlay.
void on_ui(void*) { }

// Called when the app is about to close.
void on_shutdown(void *userdata) {
  delete (app_state*)userdata;
}
