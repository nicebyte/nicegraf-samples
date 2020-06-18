//T: spec-consts vs:VSMain ps:PSMain

[[vk::constant_id(0)]] const float red = 1.0;
[[vk::constant_id(1)]] const float green = 1.0;

#include "triangle.hlsl"

float4 PSMain(Triangle_PSInput a) : SV_TARGET {
  return float4(red, green, 1.0, 1.0);
}

Triangle_PSInput VSMain(uint vid : SV_VertexID) {
  return Triangle(vid, 1.0, 0.0, 0.0);
}
