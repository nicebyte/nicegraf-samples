//T: mvp vs:VSMain ps:PSMain

[[vk::binding(0, 0)]] cbuffer UniformData {
  float4x4 u_Transform;
};

struct PSInput {
  float4 position : SV_POSITION;
  float3 barycentric : ATTR0;
};

PSInput VSMain(float3 model_pos : ATTRIBUTE0, uint vid : SV_VertexID) {
  const float3 barycentrics[] = {
    { 1.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 }
  };
  const PSInput result = {
    mul(u_Transform, float4(model_pos, 1.0)),
    barycentrics[vid % 3]
  };
  return result;
}

float4 PSMain(PSInput ps_in) : SV_TARGET {
  const float3 bary = ps_in.barycentric;
  const float3 bary_deriv = max(abs(ddx(bary)), abs(ddy(bary)));
  const float3 bary_step = smoothstep(0.0, bary_deriv * 1.3, bary);
  const float min_bary = min(bary_step.x, min(bary_step.y, bary_step.z));
  const float3 color = lerp(float3(0.9, 0.1, 0.2), float3(1.0, 1.0, 1.0), min_bary);
  return float4(color, 1.0);
}

