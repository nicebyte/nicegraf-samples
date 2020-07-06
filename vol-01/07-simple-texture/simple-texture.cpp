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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
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
  ngf::shader_stage blit_vert_stage;
  ngf::shader_stage frag_stage;
  ngf::graphics_pipeline pipeline;
  ngf::image image;
  ngf::pixel_buffer pbuffer;
  ngf::sampler sampler;
  bool pixel_data_uploaded = false;
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
  
  // Obtain the default render target.
  ngf_render_target rt;
  ngf_error err =
      ngf_default_render_target(NGF_LOAD_OP_CLEAR, NGF_LOAD_OP_DONTCARE,
                                NGF_STORE_OP_STORE, NGF_STORE_OP_DONTCARE,
                                &clear, NULL, &rt);
  assert(err == NGF_ERROR_OK);
  state->default_rt = ngf::render_target(rt);

  // Load shader stages.
  state->blit_vert_stage =
      load_shader_stage("simple-texture", "VSMain", NGF_STAGE_VERTEX);
  state->frag_stage =
      load_shader_stage("simple-texture", "PSMain", NGF_STAGE_FRAGMENT);
  ngf_plmd* pipeline_metadata = load_pipeline_metadata("simple-texture");
  assert(pipeline_metadata);

  // Initial pipeline configuration with OpenGL-style defaults.
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(nullptr,
                                                 &pipeline_data);
  ngf_graphics_pipeline_info &pipe_info = pipeline_data.pipeline_info;
  pipe_info.nshader_stages = 2u;
  pipe_info.shader_stages[0] = state->blit_vert_stage.get();
  pipe_info.shader_stages[1] = state->frag_stage.get();
  pipe_info.compatible_render_target = state->default_rt.get();
  pipe_info.image_to_combined_map =
      ngf_plmd_get_image_to_cis_map(pipeline_metadata);
  pipe_info.sampler_to_combined_map =
      ngf_plmd_get_sampler_to_cis_map(pipeline_metadata);

  // Create a pipeline layout from the loaded metadata.
  err = ngf_util_create_pipeline_layout_from_metadata(
     ngf_plmd_get_layout(pipeline_metadata), &pipeline_data.layout_info);
  assert(err == NGF_ERROR_OK);
  err = state->pipeline.initialize(pipe_info);
  assert(err == NGF_ERROR_OK);

  // Done with the metadata.
  ngf_plmd_destroy(pipeline_metadata, NULL);

  // Create the image.
  const ngf_extent3d img_size { 512u, 512u, 1u };
  const ngf_image_info img_info {
    NGF_IMAGE_TYPE_IMAGE_2D,
    img_size,
    1u,
    NGF_IMAGE_FORMAT_RGBA8,
    0u,
    NGF_IMAGE_USAGE_SAMPLE_FROM
  };
  err = state->image.initialize(img_info);
  assert(err == NGF_ERROR_OK);

  // Create the staging pixel buffer.
  const ngf_pixel_buffer_info pbuffer_info = {
    512u * 512u * 4u,
    NGF_PIXEL_BUFFER_USAGE_WRITE
  };
  err = state->pbuffer.initialize(pbuffer_info);
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
  ngf_irect2d viewport { 0, 0, w, h };
  ngf_cmd_buffer cmd_buf = nullptr;
  ngf_cmd_buffer_info cmd_info;
  ngf_create_cmd_buffer(&cmd_info, &cmd_buf);
  ngf_start_cmd_buffer(cmd_buf);
  if (state->pixel_data_uploaded && state->pbuffer.get() != nullptr) {
    state->pbuffer.reset(nullptr);
  } else if (!state->pixel_data_uploaded) {
    // Populate image with data.
    FILE *image = fopen("textures/LENA0.DATA", "rb");
    assert(image != NULL);
    fseek(image, 0, SEEK_END);
    const uint32_t image_data_size = (uint32_t)ftell(image);
    fseek(image, 0, SEEK_SET);
    void *mapped_pbuf = ngf_pixel_buffer_map_range(state->pbuffer,
                                                   0,
                                                   image_data_size,
                                                   NGF_BUFFER_MAP_WRITE_BIT);
    size_t read_bytes = fread(mapped_pbuf, 1, image_data_size, image);
    assert(read_bytes == 512u * 512u * 4u);
    ngf_pixel_buffer_flush_range(state->pbuffer, 0, image_data_size);
    ngf_pixel_buffer_unmap(state->pbuffer);
    fclose(image);
    ngf_image_ref img_ref = {
      state->image,
      0,
      0,
      NGF_CUBEMAP_FACE_POSITIVE_X
    };
    ngf_offset3d offset { 0, 0, 0 };
    ngf_extent3d extent { 512, 512, 1};
    ngf::xfer_encoder xfenc { cmd_buf };
    ngf_cmd_write_image(xfenc, state->pbuffer, 0, img_ref, &offset, &extent);
    state->pixel_data_uploaded = true;
  }
  ngf::render_encoder renc { cmd_buf };
  ngf_cmd_begin_pass(renc, state->default_rt);
  ngf_cmd_bind_gfx_pipeline(renc, state->pipeline);
  ngf_cmd_viewport(renc, &viewport);
  ngf_cmd_scissor(renc, &viewport);
  ngf::cmd_bind_resources(
    renc,
    ngf::descriptor_set<0>::binding<1>::texture(state->image.get()),
    ngf::descriptor_set<0>::binding<2>::sampler(state->sampler.get()));
  ngf_cmd_draw(renc, false, 0u, 3u, 1u); 
  ngf_cmd_end_pass(renc);
  ngf_submit_cmd_buffers(1u, &cmd_buf);
  ngf_destroy_cmd_buffer(cmd_buf);
}

// Called every time the application has to dra an ImGUI overlay.
void on_ui(void*) { }

// Called when the app is about to close.
void on_shutdown(void *userdata) {
  delete (app_state*)userdata;
}
