//T: cubes ps:PSMain vs:VSMain
//T: cubes-instanced ps:PSMain vs:VSMainInstanced

[[vk::binding(0, 0)]] cbuffer ClipTransformMatrix {
  float4x4 u_WorldToClip;
};

[[vk::binding(1, 0)]] cbuffer WorldTransformMatrix {
  float4x4 u_ModelToWorld;
};

[[vk::binding(2, 0)]] uniform Texture2D tex;
[[vk::binding(3, 0)]] uniform sampler   smp;

struct VertexData {
  float3 position : SV_POSITION0;
  float2 uv       : ATTR0;
  uint   iid      : SV_InstanceID;
};

struct PSInput {
  float4 clip_pos : SV_POSITION;
  float2 uv : ATTR0;
};

PSInput VSMain(VertexData vertex) {
  PSInput result = {
    mul(u_WorldToClip * u_ModelToWorld, float4(vertex.position, 1.0)),
    vertex.uv
  };
  return result;
}

PSInput VSMainInstanced(VertexData vertex) {
  uint    col    =  vertex.iid % 200;
  uint    row    = (vertex.iid - col) / 220;
  float4  pos    =  float4(vertex.position + float3((float)col * 5.0, (float)row * 5.0, 0.0), 1.0);
  PSInput result = {
    mul(u_WorldToClip, pos),
    vertex.uv
  };
  return result;
}

float4 PSMain(PSInput ps_in) : SV_TARGET {
  return tex.Sample(smp, ps_in.uv);
}

