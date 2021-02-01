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
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

using nm::float4x4;
using nm::float3;
using nm::float4;

union uniform_data {
  float4x4 matrix;
  uint8_t padding[256];
};

struct app_state {
  ngf::render_target     default_render_target;
  ngf::shader_stage      blit_vert_stage;
  ngf::shader_stage      frag_stage;
  ngf::graphics_pipeline pipeline;
  float4x4               world_from_model;
  float4x4               view_from_world;
  float4x4               clip_from_view;
  float                  persp_fovy =  65.00f;
  float                  persp_near =   0.01f;
  float                  persp_far  = 100.00f;
  float3                 camera_pos_world {   0.0f,   0.0f,   6.0f };
  float3                 model_pos_world  {   0.0f, -80.0f, -40.0f };
  float3                 model_rot_world  {   0.0f,   0.0f,   0.0f };
  ngf::cmd_buffer        cmdbuf;
  ngf::attrib_buffer     attr_buf;
  ngf::index_buffer      idx_buf;
  uint16_t               num_elements = 0u;
  bool                   buffers_uploaded = false;
  ngf::resource_dispose_queue dispose_queue;
  ngf::streamed_uniform<uniform_data> uniform_buffer;
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
  state->blit_vert_stage = load_shader_stage("mvp", "VSMain", NGF_STAGE_VERTEX);
  state->frag_stage = load_shader_stage("mvp", "PSMain", NGF_STAGE_FRAGMENT);
  
  // Create the initial pipeline configuration with OpenGL-style defaults.
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(nullptr, &pipeline_data);
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
  // We only have vertex positions for this sample.
  const ngf_vertex_attrib_desc attrib_descs[] = {
    {0, 0, 0, NGF_TYPE_FLOAT, 3, false},
  };
  const ngf_vertex_buf_binding_desc binding_desc = {
    0, sizeof(float) * 3u, NGF_INPUT_RATE_VERTEX 
  };
  pipeline_data.vertex_input_info.nattribs = 1u;
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

  // Create a command buffer.
  state->cmdbuf.initialize(ngf_cmd_buffer_info{});

  // Create a streamed uniform buffer.
  std::optional<ngf::streamed_uniform<uniform_data>> maybe_streamed_uniform;
  std::tie(maybe_streamed_uniform, err) =
    ngf::streamed_uniform<uniform_data>::create(3);
  assert(err == NGF_ERROR_OK);
  state->uniform_buffer = std::move(maybe_streamed_uniform.value());

  return { std::move(ctx), state };
}

void on_frame(uint32_t w, uint32_t h, float, void *userdata, ngf_frame_token frame_token) {
  app_state      *state = (app_state*)userdata;
  ngf_cmd_buffer  b     = state->cmdbuf.get();

  ngf_start_cmd_buffer(b, frame_token);
  if (!state->buffers_uploaded) {
    ngf::xfer_encoder xfer_enc { b };
    tinyobj::attrib_t obj_attribs;
    std::vector<float3> vert_data;
    std::vector<tinyobj::shape_t>  obj_shapes;
    bool obj_load_success = tinyobj::LoadObj(&obj_attribs, 
                                             &obj_shapes, 
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              "models/teapot.obj");
    if(!obj_load_success) exit(1);
    for (const tinyobj::shape_t &obj_shape : obj_shapes) {
      for (const tinyobj::index_t &idx : obj_shape.mesh.indices) {
        const unsigned vidx = (unsigned)idx.vertex_index;
        const unsigned vi = 3u * vidx;
        vert_data.push_back(float3 { obj_attribs.vertices[vi + 0u],
                                     obj_attribs.vertices[vi + 1u],
                                     obj_attribs.vertices[vi + 2u]});
      }
    }
    ngf_attrib_buffer_info attr_info = {
      sizeof(float3) * vert_data.size(),
      NGF_BUFFER_STORAGE_PRIVATE,
      NGF_BUFFER_USAGE_XFER_DST
    };
    state->attr_buf.initialize(attr_info);
    state->dispose_queue.write_buffer(xfer_enc,
                                      state->attr_buf,
                               (void*)vert_data.data(),
                                      attr_info.size,
                                      0, 0);
    state->num_elements = (uint16_t)vert_data.size();
    state->buffers_uploaded = true;
  }
  state->world_from_model = nm::scale(float4 { 0.059f, 0.059f, 0.059f, 1.0f })
                          * nm::rotation_z(state->model_rot_world[2])
                          * nm::rotation_y(state->model_rot_world[1])
                          * nm::rotation_x(state->model_rot_world[0])
                          * nm::translation(state->model_pos_world);
  state->view_from_world  = nm::look_at(state->camera_pos_world,
                                        float3 { 0.0f },
                                        float3 {0.0f, 1.0f, 0.0f});
  state->clip_from_view   = nm::perspective(nm::deg2rad(state->persp_fovy),
                                            (float)w / (float)h,
                                            state->persp_near,
                                            state->persp_far);
  uniform_data final_transform {
      state->clip_from_view * state->view_from_world * state->world_from_model
  };
  state->uniform_buffer.write(final_transform);
  {
    ngf::render_encoder render_enc{ b };
    ngf_cmd_begin_pass(render_enc, state->default_render_target.get());
    ngf_cmd_bind_gfx_pipeline(render_enc, state->pipeline.get());
    ngf::cmd_bind_resources(
      render_enc,
      state->uniform_buffer.bind_op_at_current_offset(0, 0));
    const ngf_irect2d viewport_rect{
      0, 0, w, h
    };
    ngf_cmd_viewport(render_enc, &viewport_rect);
    ngf_cmd_scissor(render_enc, &viewport_rect);
    ngf_cmd_bind_attrib_buffer(render_enc, state->attr_buf.get(), 0, 0);
    ngf_cmd_draw(render_enc, false, 0, state->num_elements, 1u);
    ngf_cmd_end_pass(render_enc);
  }
  ngf_submit_cmd_buffers(1u, &b);
}

void on_ui(void *userdata) { 
  app_state *state = (app_state*)userdata;
  ImGui::Begin("Model-View-Projection", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::SliderFloat("Model X",
                      &state->model_pos_world.data[0], -100.0, 100.0);
  ImGui::SliderFloat("Model Y",
                      &state->model_pos_world.data[1], -100.0, 100.0);
  ImGui::SliderFloat("Model Z",
                      &state->model_pos_world.data[2], -100.0, 100.0);
  ImGui::SliderFloat("Model Pitch",
                     &state->model_rot_world[0], -3.14f, 3.14f);
  ImGui::SliderFloat("Model Yaw",
                     &state->model_rot_world[1], -3.14f, 3.14f);
  ImGui::SliderFloat("Model Roll",
                     &state->model_rot_world[2], -3.14f, 3.14f);
  ImGui::SliderFloat("Camera X",
                      &state->camera_pos_world.data[0], -100.0, 100.0);
  ImGui::SliderFloat("Camera Y",
                     &state->camera_pos_world.data[1], -100.0, 100.0);
  ImGui::SliderFloat("Camera Z",
                     &state->camera_pos_world.data[2], -100.0, 100.0);
  ImGui::SliderFloat("Verical FOV", &state->persp_fovy, 1.0, 180.0);
  ImGui::End();
}

void on_shutdown(void *userdata) {
  delete (app_state*)userdata;
}

