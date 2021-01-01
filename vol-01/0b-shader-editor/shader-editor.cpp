/**
Copyright (c) 2019 nicegraf contributors
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
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
#include <nicegraf_util.h>
#include <imgui.h>
#include <TextEditor.h>
#include <assert.h>

#if defined(_MSC_VER_)
#define SED_PATH_SEPARATOR "\\"
#else
#define SED_PATH_SEPARATOR "/"
#endif

struct uniform_data {
  float time;
  float time_delta;
  float width;
  float height;
};

struct app_state {
  ngf::render_target     default_render_target;
  ngf::shader_stage      blit_vert_stage;
  ngf::shader_stage      frag_stage;
  ngf::graphics_pipeline pipeline;
  ngf::cmd_buffer        cmdbuf;
  ngf::streamed_uniform<uniform_data> uniform_data;
  TextEditor             editor;
  bool                   err_flag = false;
  bool                   force_update = true;
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
  ngf_error err =
      ngf_default_render_target(NGF_LOAD_OP_CLEAR,
                                NGF_LOAD_OP_DONTCARE,
                                NGF_STORE_OP_STORE,
                                NGF_STORE_OP_DONTCARE,
                                &clear_color,
                                nullptr,
                                &default_render_target);
  assert(err == NGF_ERROR_OK);
  state->default_render_target.reset(default_render_target);

  // Create a command buffer.
  state->cmdbuf.initialize(ngf_cmd_buffer_info{});

  // Create a streamed uniform buffer.
  std::optional<ngf::streamed_uniform<uniform_data>> maybe_streamed_uniform;
  std::tie(maybe_streamed_uniform, err) =
      ngf::streamed_uniform<uniform_data>::create(3);
  assert(err == NGF_ERROR_OK);
  state->uniform_data = std::move(maybe_streamed_uniform.value());

  state->editor.SetLanguageDefinition( TextEditor::LanguageDefinition::HLSL());
  state->editor.SetText(R"SHADER(#include "shaders/hlsl/editor-preamble.hlsl"

float4 PSMain(Triangle_PSInput ps_in) : SV_TARGET {
  return float4(ps_in.position * 0.5 + 0.5, 1.0);
})SHADER");
  return { std::move(ctx), state };
}

void on_frame(uint32_t w, uint32_t h, float time, void *userdata) {
  static float prev_time = time;

  app_state      *state = (app_state*)userdata;
  ngf_cmd_buffer  b     = state->cmdbuf.get();
  state->uniform_data.write(uniform_data {
    time,
    time - prev_time,
    (float)w,
    (float)h
  });
  ngf_start_cmd_buffer(b);
  ngf::render_encoder renc { b };
  ngf_cmd_begin_pass(renc, state->default_render_target.get());
  if (state->pipeline.get() != nullptr) {
    ngf_cmd_bind_gfx_pipeline(renc, state->pipeline.get());
    ngf_resource_bind_op rbop =
        state->uniform_data.bind_op_at_current_offset(0, 0);
    ngf_cmd_bind_gfx_resources(renc, &rbop, 1u);
    const ngf_irect2d viewport_rect{
      0, 0, w, h
    };
    ngf_cmd_viewport(renc, &viewport_rect);
    ngf_cmd_scissor(renc, &viewport_rect);
    ngf_cmd_draw(renc, false, 0, 3, 1);
  }
  ngf_cmd_end_pass(renc);
  ngf_render_encoder_end(renc);
  ngf_submit_cmd_buffers(1u, &b);
}

void on_ui(void *userdata) {
  app_state *state = (app_state*)userdata;
  ImGui::Begin("Shader Editor", nullptr, 0u);
  if (state->force_update) {
    ImGui::Button("Hold on...");
    state->err_flag = false;
    FILE *hlsl_file = fopen("live.hlsl", "wb");
    fprintf(hlsl_file, "%s\n//T: live vs:VSMain ps:PSMain\n",
            state->editor.GetText().c_str());
    fclose(hlsl_file);
    int status =
        system(".." SED_PATH_SEPARATOR "nicegraf-shaderc" SED_PATH_SEPARATOR
               "nicegraf_shaderc live.hlsl -t gl430 -t msl12");
    if (status == 0) {
      state->blit_vert_stage = load_shader_stage("live", "VSMain", NGF_STAGE_VERTEX,
                                            "./");
      state->frag_stage = load_shader_stage("live", "PSMain",
                                             NGF_STAGE_FRAGMENT, "./");

      // Initial pipeline configuration with OpenGL-style defaults.
      ngf_util_graphics_pipeline_data pipeline_data;
      ngf_util_create_default_graphics_pipeline_data(nullptr,
        &pipeline_data);
      ngf_graphics_pipeline_info &pipe_info = pipeline_data.pipeline_info;
      pipe_info.nshader_stages = 2u;
      pipe_info.shader_stages[0] = state->blit_vert_stage.get();
      pipe_info.shader_stages[1] = state->frag_stage.get();
      pipe_info.compatible_render_target = state->default_render_target.get();
      // Create pipeline layout from metadata.
      ngf_plmd *pipeline_metadata = load_pipeline_metadata("textured-quad");
      assert(pipeline_metadata);
      ngf_util_create_pipeline_layout_from_metadata(
          ngf_plmd_get_layout(pipeline_metadata),
          &pipeline_data.layout_info);
      assert(pipeline_data.layout_info.ndescriptor_set_layouts == 2);
      pipe_info.image_to_combined_map =
          ngf_plmd_get_image_to_cis_map(pipeline_metadata);
      pipe_info.sampler_to_combined_map =
          ngf_plmd_get_sampler_to_cis_map(pipeline_metadata);
      state->pipeline.reset(nullptr);
      state->pipeline.initialize(pipe_info);
      ngf_plmd_destroy(pipeline_metadata, nullptr);
    } else {
      state->err_flag = true;
    }
    state->force_update = false;
  } else if (ImGui::Button("Update")) {
    state->force_update = true;
  }
  if (state->err_flag) {
    ImGui::SameLine();
    ImGui::TextColored(
      ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
     "Error occurred, check console!\n");
  }
  state->editor.Render("Shader Editor");
  ImGui::End();
}

void on_shutdown(void*) {}

