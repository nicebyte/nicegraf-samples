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
#include <nicegraf_util.h>
#include <nicemath.h>
#include <imgui.h>
#include <assert.h>

using nm::float4x4;
using nm::float3;

constexpr uint32_t NUM_CUBES_H = 220u;
constexpr uint32_t NUM_CUBES_V = 220u;

union mtw {
  float4x4 matrix;
  uint8_t padding[256];
};

struct app_state {
  ngf::render_target     default_render_target;
  ngf::shader_stage      blit_vert_stage;
  ngf::shader_stage      frag_stage;
  ngf::graphics_pipeline pipeline;
  ngf::attrib_buffer     attr_buf;
  ngf::index_buffer      idx_buf;
  ngf::uniform_buffer    world_to_clip_ub;
  ngf::image             texture;
  ngf::sampler           sampler;
  ngf::cmd_buffer        cmdbuf;
  ngf::resource_dispose_queue dispose_queue;
  bool                   resources_uploaded = false;
};

init_result on_initialized(uintptr_t native_window_handle,
                           uint32_t  initial_window_width,
                           uint32_t  initial_window_height) {
  app_state *state = new app_state;

  // Create and activate a nicegraf context with default settings.
  ngf::context ctx = create_default_context(native_window_handle,
                                            initial_window_width,
                                            initial_window_height);
  
  // Obtain the default render target from the context that we just created.
  ngf_render_target default_render_target = nullptr;
  ngf_clear clear_color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
  ngf_clear clear_depth;
  clear_depth.clear_depth = 1.0f;
  ngf_error err =
      ngf_default_render_target(NGF_LOAD_OP_CLEAR,
                                NGF_LOAD_OP_CLEAR,
                                NGF_STORE_OP_STORE,
                                NGF_STORE_OP_DONTCARE,
                                &clear_color,
                                &clear_depth,
                                &default_render_target);
  assert(err == NGF_ERROR_OK);
  state->default_render_target.reset(default_render_target);
  
  // Load shader stages.
  state->blit_vert_stage = load_shader_stage("cubes-instanced", "VSMainInstanced", NGF_STAGE_VERTEX);
  state->frag_stage = load_shader_stage("cubes-instanced", "PSMain", NGF_STAGE_FRAGMENT);
  
  // Create the initial pipeline configuration with OpenGL-style defaults.
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(nullptr,
    &pipeline_data);
  ngf_graphics_pipeline_info &pipe_info = pipeline_data.pipeline_info;

  // Set up shader stages.
  pipe_info.nshader_stages = 2u;
  pipe_info.shader_stages[0] = state->blit_vert_stage.get();
  pipe_info.shader_stages[1] = state->frag_stage.get();
  
  // Set compatible render target.
  pipe_info.compatible_render_target = state->default_render_target.get();

  // Enable depth testing and writing.
  pipeline_data.depth_stencil_info.depth_test = true;
  pipeline_data.depth_stencil_info.depth_write = true;

  // Set up multisampling.
  pipeline_data.multisample_info.multisample = true;
  pipeline_data.multisample_info.alpha_to_coverage = false;

  // Set up pipeline's vertex input.
  const ngf_vertex_attrib_desc attrib_descs[] = {
    {0, 0, 0, NGF_TYPE_FLOAT, 3, false},
    {1, 0, sizeof(float) * 3, NGF_TYPE_FLOAT, 2, false}
  };
  const ngf_vertex_buf_binding_desc binding_desc = {
    0, sizeof(float) * 5u, NGF_INPUT_RATE_VERTEX 
  };
  pipeline_data.vertex_input_info.nattribs = 2u;
  pipeline_data.vertex_input_info.attribs = attrib_descs;
  pipeline_data.vertex_input_info.nvert_buf_bindings = 1u;
  pipeline_data.vertex_input_info.vert_buf_bindings = &binding_desc;
  
  // Create pipeline layout from metadata.
  ngf_plmd *pipeline_metadata = load_pipeline_metadata("cubes-instanced");
  assert(pipeline_metadata);
  ngf_util_create_pipeline_layout_from_metadata(
      ngf_plmd_get_layout(pipeline_metadata),
      &pipeline_data.layout_info);
  assert(pipeline_data.layout_info.ndescriptor_set_layouts == 1);
  pipe_info.image_to_combined_map =
      ngf_plmd_get_image_to_cis_map(pipeline_metadata);
  pipe_info.sampler_to_combined_map =
      ngf_plmd_get_sampler_to_cis_map(pipeline_metadata);
  state->pipeline.reset(nullptr);
  state->pipeline.initialize(pipe_info);
  ngf_plmd_destroy(pipeline_metadata, nullptr);

  // Create the texture image.
  const ngf_extent3d img_size { 512u, 512u, 1u };
  const ngf_image_info img_info {
    NGF_IMAGE_TYPE_IMAGE_2D,
    img_size,
    1u,
    NGF_IMAGE_FORMAT_RGBA8,
    0u,
    NGF_IMAGE_USAGE_SAMPLE_FROM
  };
  err = state->texture.initialize(img_info);
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

  // Create a command buffer.
  state->cmdbuf.initialize(ngf_cmd_buffer_info{});

  return { std::move(ctx), state };
}

void on_frame(uint32_t w, uint32_t h, float, void *userdata) {
  app_state      *state = (app_state*)userdata;
  ngf_cmd_buffer  b     = state->cmdbuf.get();

  ngf_start_cmd_buffer(b);
  if (!state->resources_uploaded) {
    // Create the vertex attribute and index buffers.
    const float cube_vert_attribs[] = {
      // Front side.
      -1.f, -1.f,  1.f,
       0.f,  0.f,
       1.f, -1.f,  1.f,
       1.f,  0.f,
       1.f,  1.f,  1.f,
       1.f,  1.f,
      -1.f,  1.f,  1.f,
       0.f,  1.f,

      // Back side.
      -1.f, -1.f, -1.f,
       0.f,  0.f,
       1.f, -1.f, -1.f,
       0.f,  1.f,
       1.f,  1.f, -1.f,
       1.f,  1.f,
      -1.f,  1.f, -1.f,
       0.f,  1.f,

      // Left side.
      -1.f, -1.f, -1.f,
       0.f,  0.f,
      -1.f, -1.f,  1.f,
       1.f,  0.f,
      -1.f,  1.f,  1.f,
       1.f,  1.f,
      -1.f,  1.f, -1.f,
       0.f,  1.f,

      // Right side.
       1.f, -1.f, -1.f,
       0.f,  0.f,
       1.f, -1.f,  1.f,
       1.f,  0.f,
       1.f,  1.f,  1.f,
       1.f,  1.f,
       1.f,  1.f, -1.f,
       0.f,  1.f,

      // Top side.
      -1.f,  1.f,  1.f,
       0.f,  0.f,
       1.f,  1.f,  1.f,
       1.f,  0.f,
       1.f,  1.f, -1.f,
       1.f,  1.f,
      -1.f,  1.f, -1.f,
       0.f,  1.f,

      // Bottom side.
      -1.f, -1.f,  1.f,
       0.f,  0.f,
       1.f, -1.f,  1.f,
       1.f,  0.f,
       1.f, -1.f, -1.f,
       1.f,  1.f,
      -1.f, -1.f, -1.f,
       0.f,  1.f
    };
    const uint16_t cube_indices[] = {
       2,  1,  0,  3,  2,  0, // front
       5,  6,  4,  6,  7,  4, // back
      11,  9,  8, 11, 10,  9, // left
      13, 15, 12, 13, 14, 15, // right
      18, 17, 16, 19, 18, 16, // top
      20, 21, 22, 20, 22, 23  // bottom
    };
    ngf_attrib_buffer_info attr_info = {
      sizeof(cube_vert_attribs),
      NGF_BUFFER_STORAGE_PRIVATE,
      NGF_BUFFER_USAGE_XFER_DST
    };
    ngf_index_buffer_info index_info = {
      sizeof(cube_indices),
      NGF_BUFFER_STORAGE_PRIVATE,
      NGF_BUFFER_USAGE_XFER_DST
    };
    ngf_error err = state->attr_buf.initialize(attr_info);
    assert(err == NGF_ERROR_OK);
    err = state->idx_buf.initialize(index_info);
    assert(err == NGF_ERROR_OK);
    ngf::xfer_encoder xfenc { b };
    state->dispose_queue.write_buffer(
        xfenc,
        state->attr_buf,
        (void*)cube_vert_attribs,
        sizeof(cube_vert_attribs),
        0,
        0);
    state->dispose_queue.write_buffer(
        xfenc,
        state->idx_buf,
        (void*)cube_indices,
        sizeof(cube_indices),
        0,
        0);
    // Create a uniform buffer.
    const ngf_uniform_buffer_info world_to_clip_ub_info =
        { sizeof(float4x4), NGF_BUFFER_STORAGE_PRIVATE, NGF_BUFFER_USAGE_XFER_DST };
    err = state->world_to_clip_ub.initialize(world_to_clip_ub_info);
    assert(err == NGF_ERROR_OK);

    // Set up transforms.
    const float  aspect_ratio   = (float)w / (float)h;
    const float4x4 clip_from_view =
        nm::perspective(70.0f, aspect_ratio, 0.01f, 1000.0f);
    const float4x4 view_from_world =
        nm::look_at(nm::float3 { 110.0f, 110.0f, 150.0f },
                    nm::float3 { 110.0f, 110.0f, 0.0f },
                    nm::float3 {0.0f, 1.0f, 0.0f});
    const float4x4 world_to_clip = clip_from_view * view_from_world;
    err = state->dispose_queue.write_buffer(xfenc,
                                            state->world_to_clip_ub,
                                            (void*)&world_to_clip,
                                            sizeof(float4x4),
                                            0,
                                            0);
    assert(err == NGF_ERROR_OK);
    // Create texture and load data into it.
    FILE *image = fopen("textures/LENA0.DATA", "rb");
    assert(image != NULL);
    fseek(image, 0, SEEK_END);
    const uint32_t image_data_size = (uint32_t)ftell(image);
    fseek(image, 0, SEEK_SET);
    uint8_t *image_data = new uint8_t[image_data_size];
    fread(image_data, 1, image_data_size, image);
    fclose(image);
    err = state->dispose_queue.write_image(xfenc,
                                           image_data,
                                           image_data_size,
                                           0,
                                           ngf::image_ref(state->texture.get()),
                                           ngf_offset3d {   0u,   0u, 0u},
                                           ngf_extent3d { 512u, 512u, 1u});
    delete[] image_data;
    assert(err == NGF_ERROR_OK);
    state->resources_uploaded = true;
  }
  {
  ngf::render_encoder renc{ b };
  ngf_cmd_begin_pass(renc, state->default_render_target.get());
  ngf_cmd_bind_gfx_pipeline(renc, state->pipeline.get());

  ngf_resource_bind_op rbops[3];
  rbops[0].target_set = 0u;
  rbops[0].target_binding = 0u;
  rbops[0].type = NGF_DESCRIPTOR_UNIFORM_BUFFER;
  rbops[0].info.uniform_buffer.buffer = state->world_to_clip_ub.get();
  rbops[0].info.uniform_buffer.offset = 0u;
  rbops[0].info.uniform_buffer.range = sizeof(float4x4);
  rbops[1].target_set = 0u;
  rbops[1].target_binding = 2u;
  rbops[1].type = NGF_DESCRIPTOR_TEXTURE;
  rbops[1].info.image_sampler.image_subresource.image = state->texture.get();
  rbops[2].target_set = 0u;
  rbops[2].target_binding = 3u;
  rbops[2].type = NGF_DESCRIPTOR_SAMPLER;
  rbops[2].info.image_sampler.sampler = state->sampler.get();
  ngf_cmd_bind_gfx_resources(renc, rbops, 3u);
  const ngf_irect2d viewport_rect{
    0, 0, w, h
  };
  ngf_cmd_viewport(renc, &viewport_rect);
  ngf_cmd_scissor(renc, &viewport_rect);
  ngf_cmd_bind_attrib_buffer(renc, state->attr_buf.get(), 0, 0);
  ngf_cmd_bind_index_buffer(renc, state->idx_buf.get(), NGF_TYPE_UINT16);
  ngf_cmd_draw(renc, true, 0, 36, NUM_CUBES_H* NUM_CUBES_V);

  ngf_cmd_end_pass(renc);
  }
  ngf_submit_cmd_buffers(1u, &b);
}

void on_ui(void*) { }

void on_shutdown(void*) {}

