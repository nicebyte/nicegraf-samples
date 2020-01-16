//T: hexagon ps:PSMain vs:VSMain
//T: hexagon-animated ps:PSMain vs:VSMain define:ANIMATE=1

struct HexagonVSInput {
  float2 position : ATTR0;
  float3 color : ATTR1;
};

struct HexagonPSInput {
  float4 position : SV_POSITION;
  float3 color : COLOR;
};

#if defined(ANIMATE)
[vk::binding(0, 0)] cbuffer Uniforms : register(b0) {
  float u_Time;
  float u_AspectRatio;
};
#endif

HexagonPSInput VSMain(HexagonVSInput input) {
  HexagonPSInput output = {
    float4(input.position, 0.0, 1.0),
    input.color
  };
#if defined(ANIMATE)
  float theta = u_Time / 2.0;
  float2x2 rot_mtx = { cos(theta), -sin(theta), sin(theta), cos(theta) } ;
  output.position = float4(rot_mtx * input.position * float2(1.0, u_AspectRatio), 0.0, 1.0);
#endif
  return output;
}

float4 PSMain(HexagonPSInput input) : SV_TARGET {
  return float4(input.color, 1.0);
}
