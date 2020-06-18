#include "common.h"
#include <nicegraf.h>
#include <nicegraf_util.h>
#include <nicegraf_wrappers.h>
#include <assert.h>

struct triangle_data {
  float scale;
  float offset_x;
  float offset_y;
  float depth;
  float color[4];
};

struct app_state {
  ngf::render_target default_rt;
  ngf::shader_stage vert_stage;
  ngf::shader_stage frag_stage;
  ngf::graphics_pipeline pipeline;
  ngf::resource_dispose_queue discard_queue;
  ngf::uniform_buffer uniform_data[2];
  bool uniform_data_uploaded = false;
};

init_result on_initialized(uintptr_t native_handle,
                           uint32_t initial_width,
                           uint32_t initial_height) {
  // Create a new nicegraf context.
  // Swapchain configuration:
  ngf_swapchain_info swapchain_info {
    NGF_IMAGE_FORMAT_BGRA8, // 8 bit per channel BGRA for color
    NGF_IMAGE_FORMAT_UNDEFINED,// NGF_IMAGE_FORMAT_DEPTH24_STENCIL8, // 24 bit depth, 8 bit stencil
    4u, // 4x MSAA
    2u, // swapchain capacity
    initial_width,
    initial_height,
    native_handle,
    NGF_PRESENTATION_MODE_FIFO
  };
  // Context configuration:
  ngf_context_info ctx_info{
    &swapchain_info, // use the above swapchain configuration.
    nullptr, // no shared context
    false // not a debug context.
  };
  ngf::context ctx;
  ngf_error err = ctx.initialize(ctx_info);
  assert(err == NGF_ERROR_OK);

  // Set the newly created context as current.
  err = ngf_set_context(ctx.get());
  assert(err == NGF_ERROR_OK);

  app_state *state = new app_state;

  // Acquire the default render target.
  ngf_render_target default_rt;
  // At the start of a render pass, we shall clear the color to transparent
  // black, and the depth to 1.0.
  ngf_clear cc, cd;
  cc.clear_color[0] =
    cc.clear_color[1] = cc.clear_color[2] = cc.clear_color[3] = 0.0f;
  cd.clear_depth = 1.0f;
  err = ngf_default_render_target(NGF_LOAD_OP_CLEAR, NGF_LOAD_OP_CLEAR,
                                  NGF_STORE_OP_STORE, NGF_STORE_OP_DONTCARE,
                                  &cc,
                                  &cd,
                                  &default_rt);
  assert(err == NGF_ERROR_OK);
  state->default_rt.reset(default_rt);

  // Load shaders.
  state->vert_stage = load_shader_stage("depth", "VSMain", NGF_STAGE_VERTEX);
  state->frag_stage = load_shader_stage("depth", "PSMain", NGF_STAGE_FRAGMENT);
   
  // Initial pipeline configuration with OpenGL-style defaults.
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(nullptr,
                                                 &pipeline_data);
  ngf_graphics_pipeline_info &pipe_info = pipeline_data.pipeline_info;
  pipe_info.nshader_stages = 2u;
  pipe_info.shader_stages[0] = state->vert_stage.get();
  pipe_info.shader_stages[1] = state->frag_stage.get();
  pipe_info.compatible_render_target = state->default_rt.get();
  
  // Set up depth test.
  pipeline_data.depth_stencil_info.depth_test = true;
  pipeline_data.depth_stencil_info.depth_compare = NGF_COMPARE_OP_LESS;
  pipeline_data.depth_stencil_info.depth_write = true;
  // Initialize the pipeline layout - we will have just one descriptor set
  // with one binding for a uniform buffer.
  ngf_descriptor_info desc_info {
    NGF_DESCRIPTOR_UNIFORM_BUFFER,
    0u,
    NGF_DESCRIPTOR_VERTEX_STAGE_BIT | NGF_DESCRIPTOR_FRAGMENT_STAGE_BIT
  };
  err = ngf_util_create_simple_layout(&desc_info, 1u,
                                      &pipeline_data.layout_info);
  assert(err == NGF_ERROR_OK);

  // Create the pipeline!
  err = state->pipeline.initialize(pipe_info);
  assert(err == NGF_ERROR_OK); 

  return {std::move(ctx), state};
 }

void on_frame(uint32_t w, uint32_t h, float, void *userdata) {
  auto state = (app_state*)userdata;
  ngf_irect2d viewport { 0, 0, w, h };
  ngf_cmd_buffer cmd_buf = nullptr;
  ngf_cmd_buffer_info cmd_info;
  ngf_create_cmd_buffer(&cmd_info, &cmd_buf);
  ngf_start_cmd_buffer(cmd_buf);
  if (!state->uniform_data_uploaded) {
    // Initialize uniform buffers.
    ngf_uniform_buffer_info ubo_info {
      sizeof(triangle_data),
      NGF_BUFFER_STORAGE_PRIVATE,
      NGF_BUFFER_USAGE_XFER_DST
    };
    ngf_error err = NGF_ERROR_OK;
    for (uint32_t i = 0u; i < 2u; ++i) {
      err = state->uniform_data[i].initialize(ubo_info);
      assert(err == NGF_ERROR_OK);
    }
    // Populate triangles' uniform data
    triangle_data red_triangle {
      0.25f, -0.1f, 0.1f, 0.1f, {1.0f, 0.5f, 0.1f, 1.0f}
    }, blue_triangle { 0.25f, 0.1f, -0.1f, 0.5f, {0.1f, 0.5f, 1.0f, 1.0f}};
    ngf::xfer_encoder xfenc { cmd_buf };
    err  = state->discard_queue.write_buffer(xfenc,
                                             state->uniform_data[0],
                                             &red_triangle,
                                             sizeof(red_triangle),
                                             0,
                                             0);
    assert(err == NGF_ERROR_OK);
    err = state->discard_queue.write_buffer(xfenc,
                                            state->uniform_data[1],
                                            &blue_triangle,
                                            sizeof(blue_triangle),
                                            0,
                                            0);
    assert(err == NGF_ERROR_OK);
    state->uniform_data_uploaded = true;
  }
  ngf::render_encoder renc { cmd_buf };
  ngf_cmd_begin_pass(renc, state->default_rt);
  ngf_cmd_bind_gfx_pipeline(renc, state->pipeline);
  ngf_cmd_viewport(renc, &viewport);
  ngf_cmd_scissor(renc, &viewport);

  for (uint32_t i = 0u; i < 2u; ++i) {
    ngf_resource_bind_op bind_op;
    bind_op.type = NGF_DESCRIPTOR_UNIFORM_BUFFER;
    bind_op.target_set = 0u;
    bind_op.target_binding = 0u;
    bind_op.info.uniform_buffer.buffer = state->uniform_data[i].get();
    bind_op.info.uniform_buffer.offset = 0u;
    bind_op.info.uniform_buffer.range = sizeof(triangle_data);
    ngf_cmd_bind_gfx_resources(renc, &bind_op, 1u);
    ngf_cmd_draw(renc, false, 0u, 3u, 1u); 
  }
  ngf_cmd_end_pass(renc);
  ngf_submit_cmd_buffers(1u, &cmd_buf);
  ngf_destroy_cmd_buffer(cmd_buf);
}

void on_ui(void*) {}
void on_shutdown(void *userdata) {
  delete (app_state*)userdata;
}
